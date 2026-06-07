// JavaStubGenerator — Stub method bodies in Java source files.
//
// Strategy:
// 1. Read the source file
// 2. Search for the method name followed by '(' to locate the definition
// 3. Skip past the parameter list and optional throws clause to find '{'
// 4. Use brace-balancing to find the complete body
// 5. Replace the body with a trivial stub based on return type
// 6. Write the modified source back
// 7. Preserve original content for restoration

#include "JavaStubGenerator.h"

#include <fstream>
#include <sstream>

namespace topo::check {

namespace {

/// Read entire file into string.
bool readFile(const std::string& path, std::string& content) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    content = ss.str();
    return true;
}

/// Write string to file, replacing contents.
bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << content;
    return ofs.good();
}

/// Check if position is inside a comment or string literal.
bool isInsideStringOrComment(const std::string& source, size_t pos) {
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;
    char prev = 0;

    for (size_t i = 0; i < pos && i < source.size(); ++i) {
        char c = source[i];

        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            prev = c;
            continue;
        }
        if (inBlockComment) {
            if (prev == '*' && c == '/') inBlockComment = false;
            prev = c;
            continue;
        }
        if (inString) {
            if (c == '"' && prev != '\\') inString = false;
            prev = c;
            continue;
        }
        if (inChar) {
            if (c == '\'' && prev != '\\') inChar = false;
            prev = c;
            continue;
        }

        if (c == '/' && i + 1 < source.size()) {
            if (source[i + 1] == '/') {
                inLineComment = true;
                prev = c;
                continue;
            }
            if (source[i + 1] == '*') {
                inBlockComment = true;
                prev = c;
                continue;
            }
        }
        if (c == '"') {
            inString = true;
            prev = c;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            prev = c;
            continue;
        }

        prev = c;
    }

    return inLineComment || inBlockComment || inString || inChar;
}

/// Find the start of the current method's signature: the position just after
/// the nearest preceding statement/scope delimiter (`;`, `}`, or `{`) that is
/// NOT inside a string/char literal or comment. Anchoring the return-type scan
/// here keeps it from crossing into the PREVIOUS method's signature/body and
/// picking up that method's return type (or a stray primitive/void keyword).
size_t findSignatureStart(const std::string& source, size_t bodyStart) {
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;
    char prev = 0;
    size_t sigStart = 0;

    for (size_t i = 0; i < bodyStart && i < source.size(); ++i) {
        char c = source[i];

        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            prev = c;
            continue;
        }
        if (inBlockComment) {
            if (prev == '*' && c == '/') inBlockComment = false;
            prev = c;
            continue;
        }
        if (inString) {
            if (c == '"' && prev != '\\') inString = false;
            prev = c;
            continue;
        }
        if (inChar) {
            if (c == '\'' && prev != '\\') inChar = false;
            prev = c;
            continue;
        }

        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            inLineComment = true;
            prev = c;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            inBlockComment = true;
            prev = c;
            continue;
        }
        if (c == '"') {
            inString = true;
            prev = c;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            prev = c;
            continue;
        }

        // A code-level delimiter ends the previous declaration/scope; the
        // current signature starts right after it.
        if (c == ';' || c == '{' || c == '}') {
            sigStart = i + 1;
        }
        prev = c;
    }
    return sigStart;
}

/// Extract the region holding the current method's signature, from the start
/// of that signature up to its opening brace. Bounded at the previous
/// statement/scope delimiter so return-type detection never crosses into an
/// adjacent method.
std::string extractReturnTypeRegion(const std::string& source, size_t bodyStart) {
    size_t sigStart = findSignatureStart(source, bodyStart);
    return source.substr(sigStart, bodyStart - sigStart);
}

/// Check if a word at `pos` of length `len` is a whole-word match in `region`.
bool isWholeWord(const std::string& region, size_t pos, size_t len) {
    bool leftOk = (pos == 0) || !std::isalnum(static_cast<unsigned char>(region[pos - 1]));
    bool rightOk = (pos + len >= region.size()) || !std::isalnum(static_cast<unsigned char>(region[pos + len]));
    return leftOk && rightOk;
}

} // anonymous namespace

size_t JavaStubGenerator::findMethodBodyStart(const std::string& source, const std::string& methodName) {
    size_t searchStart = 0;

    while (searchStart < source.size()) {
        size_t namePos = source.find(methodName, searchStart);
        if (namePos == std::string::npos) return std::string::npos;

        // Verify whole-word boundary before the name
        if (namePos > 0) {
            char before = source[namePos - 1];
            if (std::isalnum(static_cast<unsigned char>(before)) || before == '_') {
                searchStart = namePos + methodName.size();
                continue;
            }
        }

        size_t afterName = namePos + methodName.size();
        if (afterName >= source.size()) return std::string::npos;

        // Character after must be '(' (with optional whitespace)
        size_t parenPos = afterName;
        while (parenPos < source.size() && std::isspace(static_cast<unsigned char>(source[parenPos])))
            ++parenPos;

        if (parenPos >= source.size() || source[parenPos] != '(') {
            searchStart = afterName;
            continue;
        }

        // Skip matches inside strings or comments
        if (isInsideStringOrComment(source, namePos)) {
            searchStart = afterName;
            continue;
        }

        // Balance parentheses to find closing ')'
        int parenDepth = 1;
        size_t i = parenPos + 1;
        while (i < source.size() && parenDepth > 0) {
            char c = source[i];
            if (c == '(')
                ++parenDepth;
            else if (c == ')')
                --parenDepth;
            else if (c == '"') {
                ++i;
                while (i < source.size() && source[i] != '"') {
                    if (source[i] == '\\') ++i;
                    ++i;
                }
            } else if (c == '\'') {
                ++i;
                while (i < source.size() && source[i] != '\'') {
                    if (source[i] == '\\') ++i;
                    ++i;
                }
            } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
                while (i < source.size() && source[i] != '\n')
                    ++i;
            } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
                i += 2;
                while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/'))
                    ++i;
                ++i; // skip '/'
            }
            ++i;
        }

        if (parenDepth != 0) {
            searchStart = i;
            continue;
        }

        // After closing ')': skip whitespace, optional 'throws' clause, find '{'
        size_t afterParen = i;
        while (afterParen < source.size()) {
            char c = source[afterParen];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++afterParen;
                continue;
            }
            if (c == '{') {
                return afterParen;
            }
            if (c == ';') {
                // Abstract method or declaration — skip this match
                break;
            }
            // Skip 'throws' keyword and exception type list
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                while (afterParen < source.size() &&
                       (std::isalnum(static_cast<unsigned char>(source[afterParen])) || source[afterParen] == '_'))
                    ++afterParen;
                // After 'throws', continue to skip the exception list
                // (identifiers separated by commas, possibly qualified with '.')
                continue;
            }
            // Skip commas in throws list (e.g. throws A, B)
            if (c == ',') {
                ++afterParen;
                continue;
            }
            // Skip dots in qualified names (e.g. java.io.IOException)
            if (c == '.') {
                ++afterParen;
                continue;
            }
            // Skip annotations in throws clause (e.g. @NonNull)
            if (c == '@') {
                ++afterParen;
                while (afterParen < source.size() && (std::isalnum(static_cast<unsigned char>(source[afterParen])) ||
                                                      source[afterParen] == '_' || source[afterParen] == '.'))
                    ++afterParen;
                continue;
            }
            // Skip generic type parameters (e.g. <T>)
            if (c == '<') {
                int depth = 1;
                ++afterParen;
                while (afterParen < source.size() && depth > 0) {
                    if (source[afterParen] == '<')
                        ++depth;
                    else if (source[afterParen] == '>')
                        --depth;
                    ++afterParen;
                }
                continue;
            }
            // Unrecognized — stop
            break;
        }

        searchStart = afterParen;
    }

    return std::string::npos;
}

size_t JavaStubGenerator::findMatchingBrace(const std::string& source, size_t openPos) {
    if (openPos >= source.size() || source[openPos] != '{') return std::string::npos;

    int depth = 1;
    size_t i = openPos + 1;

    while (i < source.size() && depth > 0) {
        char c = source[i];

        // Handle string literals (including text blocks """)
        if (c == '"') {
            // Check for text block (Java 15+): """
            if (i + 2 < source.size() && source[i + 1] == '"' && source[i + 2] == '"') {
                i += 3;
                // Text block ends at next """ that is not escaped
                while (i + 2 < source.size()) {
                    if (source[i] == '"' && source[i + 1] == '"' && source[i + 2] == '"') {
                        i += 3;
                        break;
                    }
                    if (source[i] == '\\') ++i;
                    ++i;
                }
                continue;
            }
            // Regular string literal
            ++i;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\') ++i;
                ++i;
            }
            if (i < source.size()) ++i;
            continue;
        }

        // Handle char literals
        if (c == '\'') {
            ++i;
            while (i < source.size() && source[i] != '\'') {
                if (source[i] == '\\') ++i;
                ++i;
            }
            if (i < source.size()) ++i;
            continue;
        }

        // Handle line comments
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n')
                ++i;
            continue;
        }

        // Handle block comments
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/'))
                ++i;
            i += 2; // skip */
            continue;
        }

        if (c == '{')
            ++depth;
        else if (c == '}')
            --depth;

        ++i;
    }

    return (depth == 0) ? (i - 1) : std::string::npos;
}

bool JavaStubGenerator::isVoidReturn(const std::string& source, size_t bodyStart) {
    std::string region = extractReturnTypeRegion(source, bodyStart);

    // Search backwards for "void" as the return type
    size_t pos = region.rfind("void");
    while (pos != std::string::npos) {
        if (isWholeWord(region, pos, 4)) {
            // Verify "void" precedes a '(' (i.e. it is the return type, not a param)
            size_t parenPos = region.find('(', pos);
            if (parenPos != std::string::npos) {
                return true;
            }
        }
        if (pos == 0) break;
        pos = region.rfind("void", pos - 1);
    }

    return false;
}

bool JavaStubGenerator::isBooleanReturn(const std::string& source, size_t bodyStart) {
    std::string region = extractReturnTypeRegion(source, bodyStart);

    size_t pos = region.rfind("boolean");
    while (pos != std::string::npos) {
        if (isWholeWord(region, pos, 7)) {
            size_t parenPos = region.find('(', pos);
            if (parenPos != std::string::npos) {
                return true;
            }
        }
        if (pos == 0) break;
        pos = region.rfind("boolean", pos - 1);
    }

    return false;
}

bool JavaStubGenerator::isPrimitiveReturn(const std::string& source, size_t bodyStart) {
    std::string region = extractReturnTypeRegion(source, bodyStart);

    // Java primitive numeric types + char
    static const std::vector<std::string> primitives = {"int", "long", "short", "byte", "float", "double", "char"};

    for (const auto& prim : primitives) {
        size_t pos = region.rfind(prim);
        while (pos != std::string::npos) {
            if (isWholeWord(region, pos, prim.size())) {
                size_t parenPos = region.find('(', pos);
                if (parenPos != std::string::npos) {
                    return true;
                }
            }
            if (pos == 0) break;
            pos = region.rfind(prim, pos - 1);
        }
    }

    return false;
}

StubResult JavaStubGenerator::stubFunction(const std::string& filePath, const std::string& funcName) {
    StubResult result;

    if (!readFile(filePath, result.originalContent)) {
        result.error = "failed to read file: " + filePath;
        return result;
    }

    size_t bodyStart = findMethodBodyStart(result.originalContent, funcName);
    if (bodyStart == std::string::npos) {
        result.error = "method '" + funcName + "' not found in " + filePath;
        return result;
    }

    size_t bodyEnd = findMatchingBrace(result.originalContent, bodyStart);
    if (bodyEnd == std::string::npos) {
        result.error = "unmatched brace for method '" + funcName + "' in " + filePath;
        return result;
    }

    // Determine stub body based on return type
    std::string stubBody;
    if (isVoidReturn(result.originalContent, bodyStart)) {
        stubBody = "{ }";
    } else if (isBooleanReturn(result.originalContent, bodyStart)) {
        stubBody = "{ return false; }";
    } else if (isPrimitiveReturn(result.originalContent, bodyStart)) {
        stubBody = "{ return 0; }";
    } else {
        // Object type — return null
        stubBody = "{ return null; }";
    }

    // Replace the body
    std::string modified =
        result.originalContent.substr(0, bodyStart) + stubBody + result.originalContent.substr(bodyEnd + 1);

    if (!writeFile(filePath, modified)) {
        result.error = "failed to write modified file: " + filePath;
        return result;
    }

    result.success = true;
    return result;
}

bool JavaStubGenerator::restoreFile(const std::string& filePath, const StubResult& result) {
    if (result.originalContent.empty()) return false;
    return writeFile(filePath, result.originalContent);
}

} // namespace topo::check
