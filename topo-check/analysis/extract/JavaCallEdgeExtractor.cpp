// JavaCallEdgeExtractor — L1 regex extractor for caller→callee edges in Java.
//
// Mirrors JavaCallSiteExtractor's scope-tracking state machine (package /
// classStack / braceDepth / inFunction). For each identifier call in a method
// body, emits a CallEdge with the caller qualified by the current
// package + class scope (using `::` separator) and the callee normalized to
// the same `::` convention.
//
// L1 callee patterns:
//   `compute(...)`            -> "compute"            (same-class call)
//   `this.compute(...)`       -> "compute"            (same-class via this)
//   `super.compute(...)`      -> "compute"            (parent class)
//   `App.compute(...)`        -> "App::compute"       (static or class-prefixed)
//   `pkg.App.compute(...)`    -> "pkg::App::compute"  (FQN)
//
// Lowercase-prefixed instance calls (`obj.compute(...)`) are skipped because
// L1 cannot resolve the instance type. Java keywords and `new Type(...)`
// construction sites are also skipped.

#include "JavaCallEdgeExtractor.h"

#include <cctype>
#include <fstream>
#include <regex>
#include <stack>
#include <string>
#include <unordered_set>

namespace topo::check {

namespace {

/// Build a caller qualified name from package + class stack + method name.
/// Mirrors JavaCallSiteExtractor::buildQualified.
std::string buildQualified(const std::string& packageName,
                           const std::stack<std::string>& classStack,
                           const std::string& funcName) {
    std::vector<std::string> parts;
    if (!packageName.empty()) {
        parts.push_back(packageName);
    }
    {
        std::stack<std::string> tmp = classStack;
        std::vector<std::string> classes;
        while (!tmp.empty()) {
            classes.push_back(tmp.top());
            tmp.pop();
        }
        for (auto it = classes.rbegin(); it != classes.rend(); ++it) {
            parts.push_back(*it);
        }
    }
    parts.push_back(funcName);

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += "::";
        result += parts[i];
    }
    return result;
}

/// Convert a dotted Java qualified name to Topo `::` separator.
std::string dotToColonColon(const std::string& dotted) {
    std::string out;
    out.reserve(dotted.size());
    for (size_t i = 0; i < dotted.size(); ++i) {
        if (dotted[i] == '.') {
            out += "::";
        } else {
            out += dotted[i];
        }
    }
    return out;
}

bool isCommentLine(const std::string& line) {
    size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) return false;
    return (line.size() > pos + 1 && line[pos] == '/' && line[pos + 1] == '/');
}

/// Java keywords that look like function names (regex would also match).
const std::unordered_set<std::string>& javaKeywords() {
    static const std::unordered_set<std::string> kws = {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "return", "throw", "try", "catch", "finally", "break", "continue",
        "new", "instanceof", "synchronized", "yield", "assert",
        "class", "interface", "enum", "record", "extends", "implements",
        "public", "private", "protected", "static", "final", "abstract",
        "native", "transient", "volatile", "strictfp", "sealed",
        "this", "super", "void", "true", "false", "null",
        "boolean", "byte", "short", "int", "long", "float", "double", "char",
        "package", "import", "throws",
    };
    return kws;
}

bool isJavaKeyword(const std::string& name) {
    return javaKeywords().count(name) > 0;
}

} // anonymous namespace

std::vector<CallEdge> JavaCallEdgeExtractor::extractCallEdges(const std::string& filePath) {
    std::vector<CallEdge> results;
    std::ifstream file(filePath);
    if (!file.is_open()) return results;

    // Scope tracking
    std::string packageName;
    std::stack<std::string> classStack;
    std::stack<int> classDepths;

    int braceDepth = 0;
    bool inFunction = false;
    int functionDepth = -1;
    std::string currentFunction;

    // Allman brace fix: remember a pending method signature when { is on next line.
    bool pendingSignature = false;
    std::string pendingFunctionName;

    // Block comment + text block state
    bool inBlockComment = false;
    bool inTextBlock = false;

    // Regex patterns (mirror JavaCallSiteExtractor)
    std::regex packageRegex(R"(^\s*package\s+([\w.]+)\s*;)");
    std::regex classRegex(
        R"(^\s*(?:(?:public|private|protected|static|abstract|final|sealed|non-sealed|strictfp)\s+)*(?:class|interface|enum|record)\s+(\w+))");
    // Method signatures: modifiers + return type + method name + open paren.
    std::regex methodRegex(
        R"(^\s*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)\s+)*(?:[\w<>\[\],\s?]+)\s+(\w+)\s*\()");

    // Match call-like tokens: optional dotted prefix, then identifier, then `(`.
    // Captures the full callee text in group 1 (e.g., "compute", "this.compute",
    // "App.compute", "pkg.App.compute"). The outer scanner classifies it.
    std::regex callRegex(R"(((?:[\w]+\s*\.\s*)*[\w]+)\s*\()");

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        // Text block state machine — must be checked first
        if (inTextBlock) {
            auto closePos = line.find("\"\"\"");
            if (closePos != std::string::npos) {
                inTextBlock = false;
                line = line.substr(closePos + 3);
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            } else {
                continue;
            }
        }

        // The peak brace depth reached on this line. For a compact single-line
        // body (`void f() { bar(); }`) the depth rises entering the body and
        // falls closing it within one line; we remember the peak so the
        // method-entry path can set `functionDepth` correctly even when the
        // closing `}` is on the same line, and we defer the same-line scope
        // exit until AFTER this line's call-scan so the body's calls are still
        // attributed and `inFunction` does not leak into the next method.
        int peakBraceDepth = braceDepth;
        bool deferredFunctionExit = false;

        // Build effective line: strip comments, string/char literals, text-block markers.
        std::string effective;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];

            if (inBlockComment) {
                if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            // Single-line comment
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;

            // Block comment start
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                inBlockComment = true;
                ++i;
                continue;
            }

            // Text block ("""...""")
            if (c == '"' && i + 2 < line.size() && line[i + 1] == '"' && line[i + 2] == '"') {
                auto closePos = line.find("\"\"\"", i + 3);
                if (closePos != std::string::npos) {
                    i = closePos + 2; // loop will ++
                    continue;
                }
                inTextBlock = true;
                break;
            }

            // String literal
            if (c == '"') {
                ++i;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\') ++i;
                    ++i;
                }
                continue;
            }

            // Character literal
            if (c == '\'') {
                ++i;
                while (i < line.size() && line[i] != '\'') {
                    if (line[i] == '\\') ++i;
                    ++i;
                }
                continue;
            }

            // Track braces here for accurate scope state
            if (c == '{') {
                ++braceDepth;
                if (braceDepth > peakBraceDepth) peakBraceDepth = braceDepth;
            } else if (c == '}') {
                --braceDepth;
                if (braceDepth < 0) braceDepth = 0;
                if (!classDepths.empty() && braceDepth == classDepths.top()) {
                    classStack.pop();
                    classDepths.pop();
                }
                if (inFunction && braceDepth == functionDepth) {
                    // Defer the scope exit: the body statements on this same
                    // line are scanned first, then the exit is applied at the
                    // end of the iteration.
                    deferredFunctionExit = true;
                }
            }

            effective += c;
        }

        if (effective.empty()) continue;
        if (isCommentLine(effective)) continue;

        // Detect package
        std::smatch pkgMatch;
        if (packageName.empty() && std::regex_search(effective, pkgMatch, packageRegex)) {
            packageName = dotToColonColon(pkgMatch[1].str());
            continue;
        }

        // Detect class/interface/enum/record
        std::smatch classMatch;
        if (!inFunction && std::regex_search(effective, classMatch, classRegex)) {
            classStack.push(classMatch[1].str());
            classDepths.push(braceDepth - 1);
            continue;
        }

        // Set true when the method body opens on THIS line: its statements
        // (and, for a compact single-line body, its closing brace) are on this
        // same line, so the line must be scanned regardless of where the
        // post-loop `braceDepth` lands.
        bool enteredFunctionThisLine = false;

        // Allman: pending method signature resolved on next line with `{`
        if (pendingSignature && !inFunction && effective.find('{') != std::string::npos) {
            inFunction = true;
            // Use the peak depth so the entry depth is correct even when the
            // matching `}` is on the same line and the brace loop already
            // brought `braceDepth` back down.
            functionDepth = peakBraceDepth - 1;
            currentFunction = pendingFunctionName;
            pendingSignature = false;
            pendingFunctionName.clear();
            enteredFunctionThisLine = true;
        }

        // Detect method definition
        if (!inFunction && !classStack.empty()) {
            std::smatch methMatch;
            if (std::regex_search(effective, methMatch, methodRegex)) {
                std::string mname = methMatch[1].str();
                if (!isJavaKeyword(mname)) {
                    bool hasBrace = effective.find('{') != std::string::npos;
                    // Forward decl (interface method, abstract method): line ends with `;`.
                    bool isForwardDecl = false;
                    {
                        auto tail = effective.find_last_not_of(" \t");
                        if (tail != std::string::npos && effective[tail] == ';') {
                            isForwardDecl = true;
                        }
                    }
                    if (!isForwardDecl) {
                        if (hasBrace) {
                            inFunction = true;
                            // peak depth: correct even for a single-line body
                            // whose closing `}` already decremented braceDepth.
                            functionDepth = peakBraceDepth - 1;
                            currentFunction = mname;
                            enteredFunctionThisLine = true;
                        } else {
                            pendingSignature = true;
                            pendingFunctionName = mname;
                        }
                    }
                }
            }
        }

        // A complete single-line body opens AND closes on this line: we just
        // entered the method yet the running depth is already back at/below the
        // opening depth. Schedule the scope-exit so it is applied after this
        // line is scanned and `inFunction` does not leak into the next method.
        if (enteredFunctionThisLine && braceDepth <= functionDepth) {
            deferredFunctionExit = true;
        }

        // Inside a method body: scan for call targets. The running `braceDepth`
        // is normally strictly deeper than the opening depth; the exception is
        // a body that opens on this very line whose closing `}` is on the same
        // line (`enteredFunctionThisLine`), where the depth may already have
        // returned to/below `functionDepth`.
        if (inFunction && (enteredFunctionThisLine || braceDepth > functionDepth)) {
            std::string callerName = buildQualified(packageName, classStack, currentFunction);

            // Iterate call-like tokens on the effective line.
            std::string remaining = effective;
            size_t absOffset = 0;
            while (true) {
                std::smatch m;
                if (!std::regex_search(remaining, m, callRegex)) break;
                std::string callee = m[1].str();
                size_t matchPos = absOffset + static_cast<size_t>(m.position(1));
                size_t matchLen = m[1].length();

                // Strip whitespace around `.` for a clean callee token.
                std::string normalized;
                normalized.reserve(callee.size());
                for (char ch : callee) {
                    if (ch != ' ' && ch != '\t') normalized += ch;
                }

                // Extract the simple (last) name for keyword filtering.
                std::string simple;
                auto dotPos = normalized.rfind('.');
                if (dotPos != std::string::npos) {
                    simple = normalized.substr(dotPos + 1);
                } else {
                    simple = normalized;
                }

                bool skip = false;
                if (simple.empty() ||
                    (!std::isalpha(static_cast<unsigned char>(simple[0])) && simple[0] != '_')) {
                    skip = true;
                }
                if (!skip && isJavaKeyword(simple)) {
                    skip = true;
                }

                // Skip the method's own definition signature (e.g. `int foo() {`
                // produces a call-like match for `foo`). Only the occurrence
                // BEFORE the opening `{` is the signature; a same-named call
                // AFTER the brace (a recursive call on a single-line body such
                // as `int fib() { return fib(); }`) is a real edge and must be
                // kept.
                if (!skip && simple == currentFunction) {
                    size_t bracePos = effective.find('{');
                    if (bracePos != std::string::npos && matchPos < bracePos) {
                        skip = true;
                    }
                }

                // Skip `new Type(...)` constructor calls — preceded by `new `.
                if (!skip && matchPos >= 4) {
                    // Look backwards for "new " before the match
                    size_t backScan = matchPos;
                    while (backScan > 0 && (effective[backScan - 1] == ' ' || effective[backScan - 1] == '\t')) {
                        --backScan;
                    }
                    if (backScan >= 3 && effective.compare(backScan - 3, 3, "new") == 0) {
                        // Confirm "new" is not part of a longer identifier
                        bool isWord = (backScan == 3) ||
                                      (!std::isalnum(static_cast<unsigned char>(effective[backScan - 4])) &&
                                       effective[backScan - 4] != '_');
                        if (isWord) skip = true;
                    }
                }

                // Determine if the dotted prefix is a lowercase variable (skip)
                // vs an uppercase Type, package-prefixed class, or `this`/`super`
                // (emit).
                std::string normalizedForEmit = normalized;
                if (!skip && dotPos != std::string::npos) {
                    // Split prefix into segments.
                    std::string prefix = normalized.substr(0, dotPos);
                    std::vector<std::string> segs;
                    {
                        size_t segStart = 0;
                        for (size_t pi = 0; pi <= prefix.size(); ++pi) {
                            if (pi == prefix.size() || prefix[pi] == '.') {
                                segs.push_back(prefix.substr(segStart, pi - segStart));
                                segStart = pi + 1;
                            }
                        }
                    }

                    if (!segs.empty() && (segs[0] == "this" || segs[0] == "super")) {
                        // `this.compute(` / `super.compute(` — strip the receiver,
                        // emit just the simple method name (same-class semantics).
                        if (segs.size() == 1) {
                            normalizedForEmit = simple;
                        } else {
                            // e.g., `this.inner.compute(` — strip leading `this.`
                            normalizedForEmit = normalized.substr(segs[0].size() + 1);
                        }
                    } else {
                        // Otherwise, look for any uppercase segment in the prefix —
                        // if none, treat as instance variable / method chain on a
                        // local and skip. Examples:
                        //   `obj.compute(`     -> segs=[obj]    -> all lowercase, skip
                        //   `App.compute(`     -> segs=[App]    -> uppercase, emit
                        //   `app.App.compute(` -> segs=[app,App]-> has uppercase, emit
                        //   `getX().y(`        -> getX has '(' so it won't be a clean
                        //                        identifier — handled elsewhere
                        bool hasUpper = false;
                        for (const auto& seg : segs) {
                            if (!seg.empty() && std::isupper(static_cast<unsigned char>(seg[0]))) {
                                hasUpper = true;
                                break;
                            }
                        }
                        if (!hasUpper) {
                            skip = true;
                        }
                    }
                }

                if (!skip) {
                    // Convert `.` to `::` in the emitted callee.
                    std::string finalCallee = dotToColonColon(normalizedForEmit);
                    CallEdge edge;
                    edge.caller = callerName;
                    edge.callee = finalCallee;
                    edge.file = filePath;
                    edge.line = lineNum;
                    results.push_back(std::move(edge));
                }

                size_t advance = static_cast<size_t>(m.position(1)) + matchLen;
                if (advance == 0) advance = 1;
                remaining = remaining.substr(advance);
                absOffset += advance;
            }
        }

        // Apply the deferred same-line method-scope-exit now that this line's
        // body (open + calls + close on one line) has been scanned, so the
        // next method is not misattributed.
        if (deferredFunctionExit) {
            inFunction = false;
            functionDepth = -1;
            currentFunction.clear();
        }
    }

    return results;
}

} // namespace topo::check
