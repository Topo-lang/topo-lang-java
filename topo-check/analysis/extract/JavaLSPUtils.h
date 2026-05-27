#ifndef TOPO_CHECK_JAVALSPUTILS_H
#define TOPO_CHECK_JAVALSPUTILS_H

#include <cctype>
#include <string>

namespace topo::check {

/// Extract qualified name from JDT hover markdown.
///
/// JDT hover examples:
///   "void com.example.App.main(String[])"      -> "com::example::App::main"
///   "int com.example.Config.getPort()"          -> "com::example::Config::getPort"
///   "class com.example.Config"                  -> "com::example::Config"
///   "interface com.example.Service"             -> "com::example::Service"
///   "enum com.example.Mode"                     -> "com::example::Mode"
///   "String com.example.App.getName()"          -> "com::example::App::getName"
///
/// Converts Java '.' separator to Topo '::' separator in the result.
inline std::string extractQualifiedName(const std::string& hover) {
    // Try function-style first: find opening paren of parameter list
    auto parenPos = hover.find('(');
    if (parenPos != std::string::npos) {
        // Work backwards from the paren to find the function name
        // Skip whitespace before paren
        size_t end = parenPos;
        while (end > 0 && hover[end - 1] == ' ') --end;
        if (end == 0) return "";

        // Scan backwards for the qualified name (alphanumeric, '.', '_')
        // Java uses '.' instead of '::'
        size_t start = end;
        while (start > 0) {
            char c = hover[start - 1];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
                --start;
            } else {
                break;
            }
        }

        std::string name = hover.substr(start, end - start);
        // Strip leading dots
        while (!name.empty() && name[0] == '.') {
            name = name.substr(1);
        }
        // Convert '.' to '::'
        std::string result;
        for (size_t i = 0; i < name.size(); ++i) {
            if (name[i] == '.') {
                result += "::";
            } else {
                result += name[i];
            }
        }
        return result;
    }

    // Type-style hover: "class com.example.Name", "interface com.example.Name",
    // "enum com.example.Name"
    // Find the last qualified name in the hover text
    size_t end = hover.size();
    // Trim trailing whitespace
    while (end > 0 && std::isspace(static_cast<unsigned char>(hover[end - 1]))) --end;
    if (end == 0) return "";

    // Scan backwards for the qualified name
    size_t nameEnd = end;
    size_t nameStart = nameEnd;
    while (nameStart > 0) {
        char c = hover[nameStart - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            --nameStart;
        } else {
            break;
        }
    }

    if (nameStart == nameEnd) return "";

    std::string name = hover.substr(nameStart, nameEnd - nameStart);
    // Strip leading dots
    while (!name.empty() && name[0] == '.') {
        name = name.substr(1);
    }
    // Convert '.' to '::'
    std::string result;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '.') {
            result += "::";
        } else {
            result += name[i];
        }
    }
    return result;
}

/// Determine whether a semantic token modifier string contains the given modifier.
/// Modifier strings from JDT are comma-separated, e.g. "declaration,readonly".
inline bool hasModifier(const std::string& modifiers, const std::string& modifier) {
    return modifiers.find(modifier) != std::string::npos;
}

} // namespace topo::check

#endif // TOPO_CHECK_JAVALSPUTILS_H
