// JavaLSPSymbolExtractor -- LSP-based Java symbol extraction via JDT.
//
// Uses semantic tokens to find declaration/definition tokens, then
// resolves each via hover to get qualified names and signatures.
// Converts Java '.' separators to Topo '::' format.

#include "JavaLSPSymbolExtractor.h"
#include "JavaLSPUtils.h"
#include "JdtBridge.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace topo::check {

JavaLSPSymbolExtractor::JavaLSPSymbolExtractor(lsp::JdtBridge& bridge)
    : bridge_(bridge) {}

std::string JavaLSPSymbolExtractor::parseReturnType(const std::string& hover) {
    // Hover format: "returnType com.example.Class.method(params)"
    // Find the opening paren
    auto parenPos = hover.find('(');
    if (parenPos == std::string::npos) return "";

    // Find the function name end (just before paren, skip whitespace)
    size_t nameEnd = parenPos;
    while (nameEnd > 0 && hover[nameEnd - 1] == ' ') --nameEnd;
    if (nameEnd == 0) return "";

    // Find the function name start (scan back through qualified name chars)
    size_t nameStart = nameEnd;
    while (nameStart > 0) {
        char c = hover[nameStart - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            --nameStart;
        } else {
            break;
        }
    }

    // Everything before the function name is the return type
    std::string retType = hover.substr(0, nameStart);

    // Trim whitespace
    auto first = retType.find_first_not_of(" \t\n");
    if (first == std::string::npos) return "";
    auto last = retType.find_last_not_of(" \t\n");
    retType = retType.substr(first, last - first + 1);

    // Strip Java modifiers: public, private, protected, static, final, abstract,
    // synchronized, native, default, strictfp
    static const char* prefixes[] = {"public ", "private ", "protected ", "static ",
                                     "final ", "abstract ", "synchronized ", "native ",
                                     "default ", "strictfp "};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const char* pfx : prefixes) {
            size_t len = std::strlen(pfx);
            if (retType.size() >= len && retType.compare(0, len, pfx) == 0) {
                retType = retType.substr(len);
                changed = true;
            }
        }
        // Re-trim
        auto s = retType.find_first_not_of(" \t");
        if (s != std::string::npos) retType = retType.substr(s);
    }

    return retType;
}

std::vector<std::string> JavaLSPSymbolExtractor::parseParamTypes(const std::string& hover) {
    auto openParen = hover.find('(');
    auto closeParen = hover.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos ||
        closeParen <= openParen) {
        return {};
    }

    std::string params = hover.substr(openParen + 1, closeParen - openParen - 1);

    // Trim
    auto start = params.find_first_not_of(" \t");
    if (start == std::string::npos) return {};
    params = params.substr(start);
    auto end = params.find_last_not_of(" \t");
    params = params.substr(0, end + 1);

    if (params.empty()) return {};

    // Split by comma, respecting angle brackets and parens
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : params) {
        if (c == '<' || c == '(')
            ++depth;
        else if (c == '>' || c == ')')
            --depth;
        else if (c == ',' && depth == 0) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) parts.push_back(current);

    std::vector<std::string> types;
    for (auto& p : parts) {
        // Trim each param
        auto s = p.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        auto e = p.find_last_not_of(" \t");
        p = p.substr(s, e - s + 1);

        // Java format: "Type name" or "final Type name" or "Type... name"
        // The last token is the parameter name, everything before is the type
        auto lastSpace = p.find_last_of(" \t");
        if (lastSpace != std::string::npos && lastSpace < p.size() - 1) {
            auto nameStart = lastSpace + 1;
            if (std::isalpha(static_cast<unsigned char>(p[nameStart])) || p[nameStart] == '_') {
                std::string type = p.substr(0, nameStart);
                // Strip "final" keyword
                const std::string finalKw = "final ";
                if (type.size() >= finalKw.size() && type.compare(0, finalKw.size(), finalKw) == 0) {
                    type = type.substr(finalKw.size());
                }
                auto te = type.find_last_not_of(" \t");
                if (te != std::string::npos) type = type.substr(0, te + 1);
                types.push_back(type);
                continue;
            }
        }
        // No clear name separation -- treat entire thing as type
        types.push_back(p);
    }

    return types;
}

std::string JavaLSPSymbolExtractor::detectEnclosingClass(const std::string& qualifiedName) {
    // "com::example::App::method" -> "com::example::App"
    auto lastSep = qualifiedName.rfind("::");
    if (lastSep == std::string::npos || lastSep == 0) return "";
    return qualifiedName.substr(0, lastSep);
}

std::vector<HostSymbol> JavaLSPSymbolExtractor::extractSymbols(const std::string& filePath) {
    std::vector<HostSymbol> result;

    if (!bridge_.isAvailable()) return result;

    // 1. Open document for JDT analysis
    bridge_.openDocument(filePath);
    struct DocGuard {
        lsp::JdtBridge& b;
        const std::string& path;
        ~DocGuard() { b.closeDocument(path); }
    } guard{bridge_, filePath};

    // 2. Get semantic tokens
    auto tokens = bridge_.getSemanticTokens(filePath);
    if (tokens.empty()) {
        return result;
    }

    // 3. Filter and process declaration/definition tokens
    for (const auto& token : tokens) {
        // Only interested in declarations and definitions
        if (!hasModifier(token.modifiers, "declaration") &&
            !hasModifier(token.modifiers, "definition")) {
            continue;
        }

        // Map JDT semantic token types to HostSymbolKind
        bool isMethod = (token.type == "method");
        bool isClass = (token.type == "class");
        bool isInterface = (token.type == "interface");
        bool isEnum = (token.type == "enum");

        if (!isMethod && !isClass && !isInterface && !isEnum) continue;

        // 4. Resolve via hover to get qualified name and signature
        auto hover = bridge_.getHoverAt(filePath, token.line, token.column);
        if (!hover) continue;

        std::string qualifiedName = extractQualifiedName(*hover);
        if (qualifiedName.empty()) continue;

        HostSymbol sym;
        sym.qualifiedName = qualifiedName;
        sym.file = filePath;
        sym.line = token.line + 1; // semantic tokens are 0-based, HostSymbol is 1-based

        // Extract simple name from qualified name
        auto lastSep = qualifiedName.rfind("::");
        sym.simpleName = (lastSep != std::string::npos)
                             ? qualifiedName.substr(lastSep + 2)
                             : qualifiedName;

        // Determine kind
        if (isClass) {
            sym.kind = HostSymbolKind::Class;
        } else if (isInterface) {
            // Java interfaces map to Class kind in Topo
            sym.kind = HostSymbolKind::Class;
        } else if (isEnum) {
            sym.kind = HostSymbolKind::Enum;
        } else if (isMethod) {
            // Check for static modifier
            if (hasModifier(token.modifiers, "static")) {
                sym.kind = HostSymbolKind::StaticMethod;
                sym.isStatic = true;
            } else {
                sym.kind = HostSymbolKind::Method;
            }

            // Check if this is a constructor
            std::string enclosing = detectEnclosingClass(qualifiedName);
            if (!enclosing.empty()) {
                sym.enclosingClass = enclosing;

                // Extract the class simple name from enclosing
                auto classSep = enclosing.rfind("::");
                std::string className = (classSep != std::string::npos)
                                            ? enclosing.substr(classSep + 2)
                                            : enclosing;

                if (sym.simpleName == className) {
                    sym.kind = HostSymbolKind::Constructor;
                }
            }

            // Parse return type and params from hover
            sym.returnType = parseReturnType(*hover);
            sym.paramTypes = parseParamTypes(*hover);
        }

        result.push_back(std::move(sym));
    }

    return result;
}

} // namespace topo::check
