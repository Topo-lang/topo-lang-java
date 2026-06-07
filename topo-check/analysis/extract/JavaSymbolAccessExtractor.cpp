// JavaSymbolAccessExtractor — L1 regex extractor for Java static field writes.
//
// Strategy:
//   Pass 1: scan the file once to collect static field declarations inside
//           class bodies. Instance fields (no `static` modifier) are NOT
//           shared across threads in the same way and are filtered out.
//           `final static` constants are also filtered — the compiler
//           guarantees no reassignment, so they are not a parallel-purity
//           hazard.
//   Pass 2: re-scan and emit SymbolAccess{isWrite=true} for writes to the
//           known static fields inside method bodies. Writes include:
//             - simple assignment `name = ...`
//             - qualified assignment `ClassName.name = ...`
//             - compound assignment `name += ...`, `name -= ...`, ...
//             - pre/post increment/decrement `++name` / `name++` / `--name` /
//               `name--`
//
// Reads are deferred — the load-bearing signal for PurityCheck is writes in
// parallel stages.

#include "JavaSymbolAccessExtractor.h"

#include <cctype>
#include <fstream>
#include <regex>
#include <stack>
#include <string>
#include <unordered_set>

namespace topo::check {

namespace {

/// Build a caller qualified name from package + class stack + method name.
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

/// Convert dot-separated package name to `::` separator.
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

/// Java type/control keywords that must not be treated as field names when
/// scanning for static field declarations.
const std::unordered_set<std::string>& typeKeywords() {
    static const std::unordered_set<std::string> kws = {
        "void", "boolean", "byte", "short", "int", "long", "float", "double",
        "char", "String", "var", "this", "super",
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "return", "throw", "try", "catch", "finally", "break", "continue",
        "new", "instanceof", "assert", "synchronized",
        "class", "interface", "enum", "record", "extends", "implements",
        "public", "private", "protected", "static", "final", "abstract",
        "native", "transient", "volatile", "strictfp", "sealed",
        "true", "false", "null", "package", "import", "throws",
    };
    return kws;
}

bool isTypeKeyword(const std::string& name) {
    return typeKeywords().count(name) > 0;
}

/// Strip Java line + block comments from a line. Updates inBlockComment.
std::string stripComments(const std::string& line, bool& inBlockComment, bool& inTextBlock) {
    if (inTextBlock) {
        auto closePos = line.find("\"\"\"");
        if (closePos != std::string::npos) {
            inTextBlock = false;
            return line.substr(closePos + 3);
        }
        return "";
    }

    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inBlockComment) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            break;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        // Text block ("""...""")
        if (c == '"' && i + 2 < line.size() && line[i + 1] == '"' && line[i + 2] == '"') {
            auto closePos = line.find("\"\"\"", i + 3);
            if (closePos != std::string::npos) {
                i = closePos + 2;
                continue;
            }
            inTextBlock = true;
            break;
        }
        // Regular string literal: copy through to the matching close quote so a
        // comment marker INSIDE the string (`"http://x"`, `"/* x"`) does not
        // truncate the line. maskStringLiterals (run after this) blanks the
        // contents; here we only need to prevent comment detection inside it.
        if (c == '"') {
            out += c;
            ++i;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    out += line[i];
                    out += line[i + 1];
                    i += 2;
                    continue;
                }
                out += line[i];
                ++i;
            }
            if (i < line.size()) out += line[i]; // closing quote
            continue;
        }
        // Character literal: same rationale (`'/'`, `'"'`).
        if (c == '\'') {
            out += c;
            ++i;
            while (i < line.size() && line[i] != '\'') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    out += line[i];
                    out += line[i + 1];
                    i += 2;
                    continue;
                }
                out += line[i];
                ++i;
            }
            if (i < line.size()) out += line[i]; // closing quote
            continue;
        }
        out += c;
    }
    return out;
}

/// Mask string and char literal contents so their internals don't trip up
/// the regex scanner.
std::string maskStringLiterals(const std::string& line) {
    std::string out = line;
    for (size_t i = 0; i < out.size(); ++i) {
        char c = out[i];
        if (c == '"') {
            out[i] = ' ';
            ++i;
            while (i < out.size() && out[i] != '"') {
                if (out[i] == '\\' && i + 1 < out.size()) {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                out[i] = ' ';
                ++i;
            }
            if (i < out.size()) out[i] = ' ';
        } else if (c == '\'') {
            out[i] = ' ';
            ++i;
            while (i < out.size() && out[i] != '\'') {
                if (out[i] == '\\' && i + 1 < out.size()) {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                out[i] = ' ';
                ++i;
            }
            if (i < out.size()) out[i] = ' ';
        }
    }
    return out;
}

/// Per-class set of static field names.
struct StaticGlobals {
    // Map: fully-qualified-class -> set of static field simple names declared
    // inside that class.  Used to look up whether a bare-name reference inside
    // a class body refers to one of its own statics.
    std::unordered_set<std::string> simpleNames; // all static names (across classes)
};

/// Pass 1: scan the file and collect static field names.
StaticGlobals collectStaticFields(const std::string& filePath) {
    StaticGlobals out;
    std::ifstream file(filePath);
    if (!file.is_open()) return out;

    std::stack<std::string> classStack;
    std::stack<int> classDepths;
    int braceDepth = 0;
    bool inFunction = false;
    int functionDepth = -1;

    bool inBlockComment = false;
    bool inTextBlock = false;

    std::regex classRegex(
        R"(^\s*(?:(?:public|private|protected|static|abstract|final|sealed|non-sealed|strictfp)\s+)*(?:class|interface|enum|record)\s+(\w+))");
    std::regex methodRegex(
        R"(^\s*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)\s+)*(?:[\w<>\[\],\s?]+)\s+(\w+)\s*\()");

    // Static field declarations.  Capture the field name in group 1.
    // Examples that should match:
    //   static int counter;
    //   static int counter = 0;
    //   private static int gCount;
    //   public static volatile long gValue = 42L;
    //   static String[] gArr = new String[10];
    //
    // Examples that must NOT match (filtered post-match):
    //   final static int MAX_VALUE = 100;            (final → constant)
    //   public static final int CONST = 1;
    //
    // The regex requires `static` somewhere in the modifier sequence and that
    // the line ends with `;` (not `{`, since `{` would be a method body).
    std::regex staticFieldRegex(
        R"(^\s*((?:public|private|protected|static|final|volatile|transient)\s+)+[\w<>\[\],?]+(?:\s*\[\s*\])*\s+(\w+)\s*(?:\[[^\]]*\])?\s*(?:=\s*[^;]*)?\s*;)");

    std::string line;
    while (std::getline(file, line)) {
        std::string stripped = stripComments(line, inBlockComment, inTextBlock);
        if (stripped.empty()) continue;
        if (isCommentLine(stripped)) continue;

        std::string masked = maskStringLiterals(stripped);

        // Track braces first
        for (size_t i = 0; i < masked.size(); ++i) {
            char c = masked[i];
            if (c == '{') {
                ++braceDepth;
            } else if (c == '}') {
                --braceDepth;
                if (braceDepth < 0) braceDepth = 0;
                if (!classDepths.empty() && braceDepth == classDepths.top()) {
                    classStack.pop();
                    classDepths.pop();
                }
                if (inFunction && braceDepth == functionDepth) {
                    inFunction = false;
                    functionDepth = -1;
                }
            }
        }

        // Detect class/interface/enum/record
        std::smatch classMatch;
        if (!inFunction && std::regex_search(masked, classMatch, classRegex)) {
            classStack.push(classMatch[1].str());
            classDepths.push(braceDepth - 1);
            continue;
        }

        // Detect method definition — opens a body that disables field detection.
        if (!inFunction && !classStack.empty()) {
            std::smatch methMatch;
            if (std::regex_search(masked, methMatch, methodRegex)) {
                std::string mname = methMatch[1].str();
                if (!isTypeKeyword(mname)) {
                    if (masked.find('{') != std::string::npos) {
                        inFunction = true;
                        functionDepth = braceDepth - 1;
                        continue;
                    }
                    // Abstract / interface methods end with `;`
                    auto tail = masked.find_last_not_of(" \t");
                    if (tail != std::string::npos && masked[tail] == ';') {
                        continue;
                    }
                }
            }
        }

        // Static field detection: only at class scope (not inside method), and
        // the modifier sequence must include `static` but NOT `final`.
        if (!inFunction && !classStack.empty()) {
            std::smatch fieldMatch;
            if (std::regex_search(masked, fieldMatch, staticFieldRegex)) {
                std::string modifiers = fieldMatch[1].str();
                bool hasStatic = modifiers.find("static") != std::string::npos;
                bool hasFinal = modifiers.find("final") != std::string::npos;
                if (hasStatic && !hasFinal) {
                    std::string name = fieldMatch[2].str();
                    if (!isTypeKeyword(name)) {
                        out.simpleNames.insert(name);
                    }
                }
            }
        }
    }

    return out;
}

} // anonymous namespace

std::vector<SymbolAccess> JavaSymbolAccessExtractor::extractSymbolAccesses(const std::string& filePath) {
    std::vector<SymbolAccess> results;

    // Pass 1: collect static field names.
    auto globals = collectStaticFields(filePath);
    if (globals.simpleNames.empty()) return results;

    // Pass 2: scan method bodies for writes to known static fields.
    std::ifstream file(filePath);
    if (!file.is_open()) return results;

    std::string packageName;
    std::stack<std::string> classStack;
    std::stack<int> classDepths;

    int braceDepth = 0;
    bool inFunction = false;
    int functionDepth = -1;
    std::string currentFunction;

    bool pendingSignature = false;
    std::string pendingFunctionName;

    bool inBlockComment = false;
    bool inTextBlock = false;

    std::regex packageRegex(R"(^\s*package\s+([\w.]+)\s*;)");
    std::regex classRegex(
        R"(^\s*(?:(?:public|private|protected|static|abstract|final|sealed|non-sealed|strictfp)\s+)*(?:class|interface|enum|record)\s+(\w+))");
    std::regex methodRegex(
        R"(^\s*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)\s+)*(?:[\w<>\[\],\s?]+)\s+(\w+)\s*\()");

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        std::string stripped = stripComments(line, inBlockComment, inTextBlock);
        if (stripped.empty()) continue;
        if (isCommentLine(stripped)) continue;

        std::string masked = maskStringLiterals(stripped);

        // The maximum brace depth reached on this line. For a compact
        // single-line body such as `long f(long v) { g += v; return v; }`
        // the depth rises (entering the body) and falls (closing it) within
        // one line; the statements between the braces still belong to the
        // method body and must be scanned. We therefore (a) remember the peak
        // depth so the method-entry path below can place `functionDepth`
        // correctly even when the closing brace is on the same line, and (b)
        // defer the method-scope-exit caused by a same-line closing brace
        // until AFTER this line's write-scan, so a body that opens and closes
        // on one line is still scanned and `inFunction` does not leak into
        // the next method (which would misattribute every subsequent method's
        // writes to this one — a purity false negative). Same fix the C++
        // sibling extractor already carries.
        int peakBraceDepth = braceDepth;
        bool deferredFunctionExit = false;

        // Track braces
        for (size_t i = 0; i < masked.size(); ++i) {
            char c = masked[i];
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
                    // Defer the actual scope-exit: the body statements on
                    // this same line are scanned first, then the exit is
                    // applied at the end of the iteration.
                    deferredFunctionExit = true;
                }
            }
        }

        // Package
        std::smatch pkgMatch;
        if (packageName.empty() && std::regex_search(masked, pkgMatch, packageRegex)) {
            packageName = dotToColonColon(pkgMatch[1].str());
            continue;
        }

        // Class / interface / enum / record
        std::smatch classMatch;
        if (!inFunction && std::regex_search(masked, classMatch, classRegex)) {
            classStack.push(classMatch[1].str());
            classDepths.push(braceDepth - 1);
            continue;
        }

        // Set true when the method body opens on THIS line. The body's
        // statements (and, for a compact single-line body, its closing brace)
        // are on this same line, so this line must be scanned regardless of
        // where the post-brace-loop `braceDepth` lands.
        bool enteredFunctionThisLine = false;

        // Allman-style: method body opens on next line
        if (pendingSignature && !inFunction && masked.find('{') != std::string::npos) {
            inFunction = true;
            // Use the peak depth seen on this line so the entry depth is
            // correct even when the matching `}` is on the same line and the
            // brace loop has already brought `braceDepth` back down.
            functionDepth = peakBraceDepth - 1;
            currentFunction = pendingFunctionName;
            pendingSignature = false;
            pendingFunctionName.clear();
            enteredFunctionThisLine = true;
        }

        // Method definition
        if (!inFunction && !classStack.empty()) {
            std::smatch methMatch;
            if (std::regex_search(masked, methMatch, methodRegex)) {
                std::string mname = methMatch[1].str();
                if (!isTypeKeyword(mname)) {
                    bool hasBrace = masked.find('{') != std::string::npos;
                    bool isForwardDecl = false;
                    auto tail = masked.find_last_not_of(" \t");
                    if (tail != std::string::npos && masked[tail] == ';') {
                        isForwardDecl = true;
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
        // entered the method, yet the running depth is already back at or
        // below the opening depth (the closing `}` was on this same line).
        // Schedule the scope-exit so it is applied after this line is scanned
        // and `inFunction` does not leak into the next method.
        if (enteredFunctionThisLine && braceDepth <= functionDepth) {
            deferredFunctionExit = true;
        }

        // Only scan inside method bodies. Normally that means the running
        // `braceDepth` is strictly deeper than the method's opening depth.
        // The exception is a body that opens on this very line: its
        // statements are on this line even though `braceDepth` may have
        // already returned to (or below) `functionDepth` because the closing
        // `}` is on the same line. `enteredFunctionThisLine` keeps that
        // single-line / same-line case scannable without affecting the
        // multi-line path.
        if (!inFunction || (!enteredFunctionThisLine && braceDepth <= functionDepth)) {
            // Still apply any deferred same-line method-scope-exit so a
            // single-line body that we skip here does not leak `inFunction`.
            if (deferredFunctionExit) {
                inFunction = false;
                functionDepth = -1;
                currentFunction.clear();
            }
            continue;
        }

        std::string callerName = buildQualified(packageName, classStack, currentFunction);

        // For each known static field name, look for write candidates on the line.
        for (const auto& name : globals.simpleNames) {
            size_t pos = 0;
            bool emittedThisLine = false;
            while (pos < masked.size() && !emittedThisLine) {
                size_t found = masked.find(name, pos);
                if (found == std::string::npos) break;

                // Word boundary before
                bool leftOK = true;
                if (found > 0) {
                    char prev = masked[found - 1];
                    if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') leftOK = false;
                    // `obj.name` — skip if obj is lowercase (instance var); allow if
                    // it looks like a class qualifier (`ClassName.name`) or `this.`.
                    if (prev == '.') {
                        // Look back further: get the segment before the dot.
                        size_t segEnd = found - 1; // points to `.`
                        size_t segStart = segEnd;
                        while (segStart > 0 &&
                               (std::isalnum(static_cast<unsigned char>(masked[segStart - 1])) ||
                                masked[segStart - 1] == '_')) {
                            --segStart;
                        }
                        std::string prefix = masked.substr(segStart, segEnd - segStart);
                        if (prefix.empty()) {
                            leftOK = false;
                        } else if (prefix == "this") {
                            // `this.name = ...` — we know we filter instance fields
                            // out in pass 1, so a `this.<staticName>` access is
                            // unusual but possible in non-static methods of the
                            // declaring class.  Allow it.
                            leftOK = true;
                        } else if (std::isupper(static_cast<unsigned char>(prefix[0]))) {
                            // `ClassName.name` — qualified static access; allow.
                            leftOK = true;
                        } else {
                            // `obj.name` — instance field on a non-this receiver,
                            // not the static we tracked.  Skip.
                            leftOK = false;
                        }
                    }
                }

                // Word boundary after
                size_t end = found + name.size();
                bool rightOK = true;
                if (end < masked.size()) {
                    char nxt = masked[end];
                    if (std::isalnum(static_cast<unsigned char>(nxt)) || nxt == '_') rightOK = false;
                }

                if (!leftOK || !rightOK) {
                    pos = found + 1;
                    continue;
                }

                // Now check the right-hand context for a write operator.
                size_t after = end;
                while (after < masked.size() && (masked[after] == ' ' || masked[after] == '\t')) ++after;

                bool isWrite = false;
                if (after < masked.size()) {
                    char c = masked[after];
                    // `=` but not `==`
                    if (c == '=' && (after + 1 >= masked.size() || masked[after + 1] != '=')) {
                        isWrite = true;
                    }
                    // Compound: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`,
                    // `<<=`, `>>=`, `>>>=`
                    if (!isWrite && after + 1 < masked.size() && masked[after + 1] == '=') {
                        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                            c == '&' || c == '|' || c == '^') {
                            isWrite = true;
                        }
                    }
                    if (!isWrite && after + 2 < masked.size() && masked[after + 2] == '=') {
                        if ((c == '<' && masked[after + 1] == '<') ||
                            (c == '>' && masked[after + 1] == '>')) {
                            isWrite = true;
                        }
                    }
                    if (!isWrite && after + 3 < masked.size() && masked[after + 3] == '=') {
                        if (c == '>' && masked[after + 1] == '>' && masked[after + 2] == '>') {
                            isWrite = true;
                        }
                    }
                    // Postfix ++ / --
                    if (!isWrite && after + 1 < masked.size()) {
                        if ((c == '+' && masked[after + 1] == '+') ||
                            (c == '-' && masked[after + 1] == '-')) {
                            isWrite = true;
                        }
                    }
                }

                // Prefix ++/--: `++name` / `--name`
                if (!isWrite && found >= 2) {
                    char p1 = masked[found - 1];
                    char p2 = masked[found - 2];
                    if ((p1 == '+' && p2 == '+') || (p1 == '-' && p2 == '-')) {
                        isWrite = true;
                    }
                }

                if (isWrite) {
                    SymbolAccess access;
                    access.function = callerName;
                    access.symbol = name;
                    access.isWrite = true;
                    access.file = filePath;
                    access.line = lineNum;
                    results.push_back(std::move(access));
                    emittedThisLine = true; // avoid double-counting per-name per-line
                }
                pos = found + name.size();
            }
        }

        // Apply the deferred same-line method-scope-exit now that this
        // line's body statements have been scanned. This keeps a compact
        // single-line body (open + statements + close on one line) both
        // scanned AND correctly closed, so the next method is not
        // misattributed.
        if (deferredFunctionExit) {
            inFunction = false;
            functionDepth = -1;
            currentFunction.clear();
        }
    }

    return results;
}

} // namespace topo::check
