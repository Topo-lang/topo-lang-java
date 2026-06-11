// JavaSymbolExtractor -- Regex-based L1 Java symbol extraction.
//
// Line-by-line scanning with brace-depth tracking for scope.
// Tracks: package, class/interface/enum declarations, method signatures.
// This is a safety-net fallback when JDT Language Server is unavailable.

#include "JavaSymbolExtractor.h"

#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace topo::check {

namespace {

// --- Regex patterns ---

// Package: package com.example.foo;
const std::regex packageRegex(R"(^\s*package\s+([\w.]+)\s*;)");

// Class/interface/enum/record declaration.
// Captures: (1) the keyword group, (2) the type name.
// Handles modifiers like public, private, protected, static, abstract, final, sealed, non-sealed.
const std::regex classRegex(
    R"(^\s*(?:(?:public|private|protected|static|abstract|final|sealed|non-sealed|strictfp)\s+)*(class|interface|enum|record)\s+(\w+))");

// Method signature — modifiers + return type + method name + open paren.
// This is intentionally broad: it matches lines that look like method declarations.
// Captures: (1) the return-type token group, (2) the method name. The type group
// also matches pure whitespace, which is what lets package-private constructors
// (`Main(int v) {`) match — and what lets statement keywords leak into it
// (see prefixHasStatementKeyword below).
const std::regex methodRegex(
    R"(^\s*(?:(?:public|private|protected|static|final|abstract|synchronized|native|default|strictfp)\s+)*([\w<>\[\],\s?]+)\s+(\w+)\s*\()");

// Access modifier on a line (used to detect per-method visibility).
const std::regex accessModRegex(R"(\b(public|private|protected)\b)");

// Static modifier on a method line.
const std::regex staticModRegex(R"(\bstatic\b)");

// Annotation line (to avoid matching annotation arguments as methods).
const std::regex annotationRegex(R"(^\s*@\w+)");

/// Convert a Java dot-separated qualified name to Topo '::' format.
std::string dotToColonColon(const std::string& dotName) {
    std::string result;
    result.reserve(dotName.size());
    for (char c : dotName) {
        if (c == '.') {
            result += "::";
        } else {
            result += c;
        }
    }
    return result;
}

/// Map Java access modifier string to Topo Visibility.
std::optional<Visibility> mapVisibility(const std::string& mod) {
    if (mod == "public") return Visibility::Public;
    if (mod == "protected") return Visibility::Protected;
    if (mod == "private") return Visibility::Private;
    return std::nullopt; // Java package-private has no direct Topo mapping
}

/// Count net brace delta on a line, skipping characters inside strings,
/// char literals, and comments.
int braceBalance(const std::string& line, bool& inBlockComment) {
    int balance = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        // Block comment continuation
        if (inBlockComment) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                inBlockComment = false;
                ++i; // skip '/'
            }
            continue;
        }

        // Line comment
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;

        // Block comment start
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            inBlockComment = true;
            ++i; // skip '*'
            continue;
        }

        // String literal
        if (c == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') {
                if (line[i] == '\\') ++i; // skip escaped char
                ++i;
            }
            continue;
        }

        // Char literal
        if (c == '\'') {
            ++i;
            while (i < line.size() && line[i] != '\'') {
                if (line[i] == '\\') ++i;
                ++i;
            }
            continue;
        }

        if (c == '{') ++balance;
        if (c == '}') --balance;
    }
    return balance;
}

/// Check if a line is entirely inside a comment or starts with an annotation.
/// This is a pre-filter to avoid false method matches on comment lines.
bool isCommentOrBlank(const std::string& line) {
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) return true;          // blank line
    if (line[pos] == '*') return true;            // block-comment continuation line
    if (pos + 1 < line.size() && line[pos] == '/' && line[pos + 1] == '/') return true;
    return false;
}

/// Reject statement-shaped methodRegex matches: `throw new X(...)`,
/// `return new X(...)`, and statement-start `new X(...)` all satisfy the
/// regex (the type group absorbs the keywords), capturing the class name
/// as a method. Scans [0, nameBegin) as whole-word tokens; every word
/// below is reserved/restricted in Java, so none can be a type name or
/// modifier preceding a real declaration — while whole-word matching
/// keeps keyword-substring names (renewLease, newCount, caseLabel) safe.
bool prefixHasStatementKeyword(const std::string& line, size_t nameBegin) {
    static const std::vector<std::string> stmtKeywords = {
        "throw", "return", "new", "yield", "assert", "case", "else"
    };
    size_t i = 0;
    while (i < nameBegin) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if (std::isalnum(c) || line[i] == '_') {
            size_t start = i;
            while (i < nameBegin) {
                unsigned char w = static_cast<unsigned char>(line[i]);
                if (!std::isalnum(w) && line[i] != '_') break;
                ++i;
            }
            const std::string token = line.substr(start, i - start);
            for (const auto& kw : stmtKeywords) {
                if (token == kw) return true;
            }
        } else {
            ++i;
        }
    }
    return false;
}

/// Map a Java source type token to the alias name generated `.topo`
/// projects bind (`using Int = std::java::int;` ...). Unmapped types
/// (float, char, generics, user classes, ...) pass through verbatim —
/// consumers that render declarations must treat non-alias names as an
/// explicit degradation case rather than emit unresolvable types.
std::string mapJavaType(const std::string& javaType) {
    if (javaType == "int") return "Int";
    if (javaType == "boolean") return "Boolean";
    if (javaType == "long") return "Long";
    if (javaType == "double") return "Double";
    return javaType; // "String" already names its alias; rest pass through
}

/// Trim ASCII whitespace from both ends.
std::string trimWs(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

/// Parse the same-line parameter list starting at `parenPos` ('(').
/// Splits on commas at angle/paren depth 0, strips `final` and leading
/// annotations, drops the trailing parameter name, and maps each type
/// through mapJavaType. A signature whose ')' is not on this line
/// (multi-line parameter list) degrades to an empty list — the
/// pre-capture behavior — rather than guess at a partial list.
std::vector<std::string> parseParamTypes(const std::string& line, size_t parenPos) {
    int depth = 0;
    size_t closePos = std::string::npos;
    for (size_t i = parenPos + 1; i < line.size(); ++i) {
        char c = line[i];
        if (c == '(' || c == '<') ++depth;
        else if (c == '>') --depth;
        else if (c == ')') {
            if (depth == 0) { closePos = i; break; }
            --depth;
        }
    }
    if (closePos == std::string::npos) return {};

    std::vector<std::string> parts;
    {
        std::string current;
        int splitDepth = 0;
        for (size_t i = parenPos + 1; i < closePos; ++i) {
            char c = line[i];
            if (c == '<' || c == '(') ++splitDepth;
            else if (c == '>' || c == ')') --splitDepth;
            else if (c == ',' && splitDepth == 0) {
                parts.push_back(current);
                current.clear();
                continue;
            }
            current += c;
        }
        if (!trimWs(current).empty()) parts.push_back(current);
    }

    std::vector<std::string> types;
    for (auto& part : parts) {
        std::string p = trimWs(part);
        // Strip leading annotations (@Nullable ...) and the final modifier.
        while (!p.empty() && p[0] == '@') {
            size_t sp = p.find_first_of(" \t");
            if (sp == std::string::npos) { p.clear(); break; }
            p = trimWs(p.substr(sp + 1));
        }
        const std::string finalKw = "final ";
        if (p.compare(0, finalKw.size(), finalKw) == 0) {
            p = trimWs(p.substr(finalKw.size()));
        }
        if (p.empty()) continue;
        // The last whitespace-separated token is the parameter name;
        // everything before it is the type.
        size_t lastWs = p.find_last_of(" \t");
        std::string type = (lastWs == std::string::npos)
            ? p
            : trimWs(p.substr(0, lastWs));
        if (type.empty()) continue;
        types.push_back(mapJavaType(type));
    }
    return types;
}

} // anonymous namespace

std::vector<HostSymbol> JavaSymbolExtractor::extractSymbols(const std::string& filePath) {
    std::vector<HostSymbol> result;
    std::ifstream file(filePath);
    if (!file.is_open()) return result;

    std::string packageName;

    // Class scope stack: class name + brace depth at entry
    struct ClassScope {
        std::string name;    // simple class name
        int entryDepth;      // brace depth when '{' of this class was seen
    };
    std::vector<ClassScope> classStack;

    int braceDepth = 0;
    bool inBlockComment = false;
    int lineNum = 0;

    std::string line;
    while (std::getline(file, line)) {
        ++lineNum;

        // --- Block comment state ---
        if (inBlockComment) {
            auto closePos = line.find("*/");
            if (closePos != std::string::npos) {
                inBlockComment = false;
                // Continue processing the rest of the line after the comment.
                // Simplification: just update brace balance, skip pattern matching
                // on partial lines.
                bool tempBlock = false;
                braceDepth += braceBalance(line.substr(closePos + 2), tempBlock);
                if (tempBlock) inBlockComment = true;
            }
            continue;
        }

        if (isCommentOrBlank(line)) {
            // Still need to track brace balance through comment-only lines
            // (they shouldn't have braces, but be defensive)
            bool tempBlock = false;
            braceDepth += braceBalance(line, tempBlock);
            if (tempBlock) inBlockComment = true;
            continue;
        }

        // --- Package ---
        std::smatch pkgMatch;
        if (std::regex_search(line, pkgMatch, packageRegex)) {
            packageName = pkgMatch[1].str();
        }

        // --- Class/interface/enum declaration ---
        std::smatch classMatch;
        if (std::regex_search(line, classMatch, classRegex)) {
            std::string keyword = classMatch[1].str();
            std::string className = classMatch[2].str();

            // Build qualified name
            std::string qualBase;
            if (!packageName.empty()) {
                qualBase = packageName;
            }
            // Include enclosing classes
            for (const auto& encl : classStack) {
                if (qualBase.empty()) {
                    qualBase = encl.name;
                } else {
                    qualBase += "." + encl.name;
                }
            }

            std::string qualifiedDot;
            if (qualBase.empty()) {
                qualifiedDot = className;
            } else {
                qualifiedDot = qualBase + "." + className;
            }

            HostSymbol sym;
            sym.qualifiedName = dotToColonColon(qualifiedDot);
            sym.simpleName = className;
            sym.file = filePath;
            sym.line = lineNum;

            if (keyword == "enum") {
                sym.kind = HostSymbolKind::Enum;
            } else {
                // class, interface, record all map to Class
                sym.kind = HostSymbolKind::Class;
            }

            // Detect visibility from modifiers on this line
            std::smatch accMatch;
            if (std::regex_search(line, accMatch, accessModRegex)) {
                sym.hostVisibility = mapVisibility(accMatch[1].str());
            }

            if (!classStack.empty()) {
                std::string enclDot;
                if (!packageName.empty()) enclDot = packageName;
                for (const auto& encl : classStack) {
                    if (enclDot.empty()) {
                        enclDot = encl.name;
                    } else {
                        enclDot += "." + encl.name;
                    }
                }
                sym.enclosingClass = dotToColonColon(enclDot);
            }

            result.push_back(std::move(sym));

            // Update brace balance and push class scope
            bool tempBlock = false;
            int delta = braceBalance(line, tempBlock);
            braceDepth += delta;
            if (tempBlock) inBlockComment = true;

            // The class opening brace may be on this line or the next.
            // Push class scope at the current brace depth.
            classStack.push_back({className, braceDepth});
            continue;
        }

        // --- Method / constructor ---
        // Only match inside a class scope
        if (!classStack.empty()) {
            // Skip annotation-only lines (e.g. @Override)
            std::smatch annoMatch;
            bool isAnnotationLine = std::regex_search(line, annoMatch, annotationRegex);
            if (!isAnnotationLine) {
                std::smatch methMatch;
                if (std::regex_search(line, methMatch, methodRegex)) {
                    std::string methodName = methMatch[2].str();

                    // Skip common false positives: control flow keywords,
                    // plus constructor delegation (`super(...)`/`this(...)`,
                    // which match via the whitespace-only type group).
                    static const std::vector<std::string> falsePositives = {
                        "if", "else", "while", "for", "switch", "catch", "return",
                        "throw", "new", "try", "do", "assert", "super", "this"
                    };
                    bool isFalse = false;
                    for (const auto& fp : falsePositives) {
                        if (methodName == fp) { isFalse = true; break; }
                    }
                    // Statement-shaped lines (`throw new X(...)`) capture the
                    // class name, so the name-only filter above cannot see
                    // them — reject on any statement keyword in the prefix.
                    if (!isFalse && prefixHasStatementKeyword(
                            line, static_cast<size_t>(methMatch.position(2)))) {
                        isFalse = true;
                    }

                    if (!isFalse) {
                        const ClassScope& currentClass = classStack.back();

                        // Build qualified method name
                        std::string qualBase;
                        if (!packageName.empty()) qualBase = packageName;
                        for (const auto& encl : classStack) {
                            if (qualBase.empty()) {
                                qualBase = encl.name;
                            } else {
                                qualBase += "." + encl.name;
                            }
                        }

                        std::string qualifiedDot = qualBase + "." + methodName;

                        HostSymbol sym;
                        sym.qualifiedName = dotToColonColon(qualifiedDot);
                        sym.simpleName = methodName;
                        sym.file = filePath;
                        sym.line = lineNum;

                        // Enclosing class
                        sym.enclosingClass = dotToColonColon(qualBase);

                        // Constructor detection: method name == class name
                        if (methodName == currentClass.name) {
                            sym.kind = HostSymbolKind::Constructor;
                        } else {
                            // Check for static
                            std::smatch staticMatch;
                            if (std::regex_search(line, staticMatch, staticModRegex)) {
                                sym.kind = HostSymbolKind::StaticMethod;
                                sym.isStatic = true;
                            } else {
                                sym.kind = HostSymbolKind::Method;
                            }
                            // Return type from the captured type group;
                            // `void` stays empty (renders as void downstream).
                            std::string retType = trimWs(methMatch[1].str());
                            if (retType != "void") {
                                sym.returnType = mapJavaType(retType);
                            }
                        }

                        // Parameter types from the same-line list — keeps
                        // the declared/found parameter counts in agreement
                        // for consumers that verify arity (e.g. bytecode
                        // signature checks on generated projects).
                        size_t parenPos = line.find(
                            '(', static_cast<size_t>(methMatch.position(2)));
                        if (parenPos != std::string::npos) {
                            sym.paramTypes = parseParamTypes(line, parenPos);
                        }

                        // Detect visibility
                        std::smatch accMatch;
                        if (std::regex_search(line, accMatch, accessModRegex)) {
                            sym.hostVisibility = mapVisibility(accMatch[1].str());
                        }
                        // else: package-private (no explicit modifier) — leave as nullopt

                        result.push_back(std::move(sym));
                    }
                }
            }
        }

        // --- Update brace depth ---
        bool tempBlock = false;
        int delta = braceBalance(line, tempBlock);
        braceDepth += delta;
        if (tempBlock) inBlockComment = true;

        // Pop class scopes when brace depth drops below entry depth
        while (!classStack.empty() && braceDepth < classStack.back().entryDepth) {
            classStack.pop_back();
        }
    }

    return result;
}

} // namespace topo::check
