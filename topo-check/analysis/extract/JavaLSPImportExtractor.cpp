// JavaLSPImportExtractor -- Clean line-based import extraction for Java.
//
// Java `import` is deterministic syntax (no need for regex or LSP).
// This extractor uses direct string matching with the same block-comment
// and string state machine as JavaImportExtractor.

#include "JavaLSPImportExtractor.h"
#include "JavaUnsafeCatalog.h"

#include <cctype>
#include <fstream>
#include <string>

namespace topo::check {

namespace {

/// Try to parse "import [static] pkg.path[.*];" from a line.
/// Returns the import path, or empty string if the line is not an import.
std::string tryParseImport(const std::string& line) {
    // Find first non-whitespace
    size_t pos = 0;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    // Must start with "import"
    if (pos + 6 > line.size()) return "";
    if (line.compare(pos, 6, "import") != 0) return "";
    pos += 6;

    // Must be followed by whitespace
    if (pos >= line.size() || (line[pos] != ' ' && line[pos] != '\t')) return "";

    // Skip whitespace
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    // Skip optional "static" keyword
    if (pos + 6 <= line.size() && line.compare(pos, 6, "static") == 0) {
        size_t afterStatic = pos + 6;
        if (afterStatic < line.size() && (line[afterStatic] == ' ' || line[afterStatic] == '\t')) {
            pos = afterStatic;
            // Skip whitespace after "static"
            while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        }
    }

    // Extract the import path (alphanumeric, '.', '_', '*')
    size_t pathStart = pos;
    while (pos < line.size()) {
        char c = line[pos];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '*') {
            ++pos;
        } else {
            break;
        }
    }

    if (pos == pathStart) return "";

    std::string path = line.substr(pathStart, pos - pathStart);

    // Skip whitespace before semicolon
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;

    // Must end with semicolon
    if (pos >= line.size() || line[pos] != ';') return "";

    return path;
}

} // anonymous namespace

std::vector<HostImport> JavaLSPImportExtractor::extractImports(const std::string& filePath) {
    std::vector<HostImport> results;
    std::ifstream file(filePath);
    if (!file.is_open()) return results;

    std::string line;
    int lineNum = 0;
    bool inBlockComment = false;

    while (std::getline(file, line)) {
        ++lineNum;

        // --- State machine: block comment tracking ---
        if (inBlockComment) {
            auto closePos = line.find("*/");
            if (closePos != std::string::npos) {
                inBlockComment = false;
            }
            continue;
        }

        // Scan for block comment openings and line comments
        {
            bool skipLine = false;
            for (size_t i = 0; i < line.size(); ++i) {
                char c = line[i];
                // Line comment -- rest of line is comment, stop scanning
                if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
                // Block comment start
                if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                    auto closePos = line.find("*/", i + 2);
                    if (closePos != std::string::npos) {
                        // Same-line block comment: skip past it
                        i = closePos + 1;
                        continue;
                    }
                    // Multiline block comment
                    inBlockComment = true;
                    skipLine = true;
                    break;
                }
                // String literal -- skip past it
                if (c == '"') {
                    ++i;
                    while (i < line.size() && line[i] != '"') {
                        if (line[i] == '\\') ++i; // skip escaped char
                        ++i;
                    }
                    continue;
                }
            }
            if (skipLine) continue;
        }

        // Try to parse import
        std::string importPath = tryParseImport(line);
        if (!importPath.empty()) {
            // Normalize wildcard: java.io.* -> java.io
            if (importPath.size() >= 2 &&
                importPath[importPath.size() - 1] == '*' &&
                importPath[importPath.size() - 2] == '.') {
                importPath = importPath.substr(0, importPath.size() - 2);
            }

            HostImport imp;
            imp.normalizedPath = importPath;
            imp.file = filePath;
            imp.line = lineNum;
            imp.unsafeLevel = JavaUnsafeCatalog::classifyImport(importPath);
            results.push_back(std::move(imp));
        }
    }

    return results;
}

std::vector<HostImport> JavaLSPImportExtractor::extractAll(const std::vector<std::string>& files) {
    std::vector<HostImport> results;
    for (const auto& f : files) {
        auto imports = extractImports(f);
        results.insert(results.end(),
                       std::make_move_iterator(imports.begin()),
                       std::make_move_iterator(imports.end()));
    }
    return results;
}

} // namespace topo::check
