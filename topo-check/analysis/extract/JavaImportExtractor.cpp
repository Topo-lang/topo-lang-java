// JavaImportExtractor — Extract import declarations from Java source files.
//
// Strategy: line-by-line scanning with block comment, string, and text block tracking.
// Handles Java 13+ text blocks (""") to avoid false positives from imports
// inside multi-line string literals.
// Matches `import [static] pkg.path[.*];` statements.
// Wildcard imports (java.io.*) are normalized by removing the trailing .*
// (e.g. java.io.* -> java.io).
// Dots are preserved (not converted to ::) because classifyImport and
// the Java prefix checks operate on dot-separated Java paths.

#include "JavaImportExtractor.h"
#include "JavaUnsafeCatalog.h"
#include "topo/Check/CapabilityCatalog.h"

#include <fstream>
#include <regex>
#include <string>

namespace topo::check {

std::vector<HostImport> JavaImportExtractor::extractImports(const std::string& filePath) {
    std::vector<HostImport> results;
    std::ifstream file(filePath);
    if (!file.is_open()) return results;

    // Block comment and text block state
    bool inBlockComment = false;
    bool inTextBlock = false;

    std::regex importRegex(R"(^\s*import\s+(?:static\s+)?([\w.]+(?:\.\*)?)\s*;)");
    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        // Text block state machine — must be checked before any other processing
        if (inTextBlock) {
            auto closePos = line.find("\"\"\"");
            if (closePos != std::string::npos) {
                inTextBlock = false;
                // Process remainder of line after closing """
                line = line.substr(closePos + 3);
                if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
                // Fall through to process remainder
            } else {
                continue; // Still inside text block
            }
        }

        // Process block comment and string state character by character
        std::string effective;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];

            // Track block comment boundaries
            if (inBlockComment) {
                if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    inBlockComment = false;
                    ++i; // skip the '/'
                }
                continue;
            }

            // Skip // line comments
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                break; // rest of line is comment
            }

            // Enter block comment
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                inBlockComment = true;
                ++i; // skip the '*'
                continue;
            }

            // Text block detection — must come BEFORE single " handling
            if (c == '"' && i + 2 < line.size() && line[i + 1] == '"' && line[i + 2] == '"') {
                // Text block opening — look for closing """ on same line
                auto closePos = line.find("\"\"\"", i + 3);
                if (closePos != std::string::npos) {
                    // Same-line text block: skip content
                    i = closePos + 2; // will be incremented by loop
                    continue;
                }
                // Multi-line text block — enter text block state
                inTextBlock = true;
                break; // stop processing this line
            }

            // Track string literals
            if (c == '"') {
                ++i;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < line.size()) {
                        ++i; // skip escaped char
                    }
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

            effective += c;
        }

        // If we are still inside a block comment or text block, skip this line
        if ((inBlockComment || inTextBlock) && effective.empty()) continue;

        // Match import statement on the effective (non-commented) content
        std::smatch match;
        if (std::regex_search(effective, match, importRegex)) {
            std::string importPath = match[1].str();

            // Normalize wildcard: java.io.* -> java.io
            if (importPath.size() >= 2 &&
                importPath[importPath.size() - 1] == '*' &&
                importPath[importPath.size() - 2] == '.') {
                importPath = importPath.substr(0, importPath.size() - 2);
            }

            // Always emit a HostImport entry, regardless of capability/level.
            // ContainmentCheck Pass 1 itself filters by `cap || unsafeLevel != Safe`
            // (re-running classifyImport on the path), so benign java.util / java.time /
            // java.math imports flow through harmlessly while still letting the
            // CheckRunner empty-extraction guard distinguish "extractor produced
            // nothing" from "code has no restricted imports". Without this, every
            // safe Java fixture trips the empty-extraction error path.
            HostImport imp;
            imp.normalizedPath = importPath;
            imp.file = filePath;
            imp.line = lineNum;
            imp.unsafeLevel = JavaUnsafeCatalog::classifyImport(imp.normalizedPath);
            results.push_back(std::move(imp));
        }
    }

    return results;
}

} // namespace topo::check
