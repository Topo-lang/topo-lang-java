#ifndef TOPO_CHECK_JAVAIMPORTEXTRACTOR_H
#define TOPO_CHECK_JAVAIMPORTEXTRACTOR_H

#include "topo/Check/ImportExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// Extracts import declarations from Java source files.
/// Parses `import [static] pkg.path[.*];` statements, skipping
/// imports inside block comments and string literals.
class JavaImportExtractor : public ImportExtractor {
public:
    /// Extract all import paths from a single Java source file.
    std::vector<HostImport> extractImports(const std::string& filePath) override;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVAIMPORTEXTRACTOR_H
