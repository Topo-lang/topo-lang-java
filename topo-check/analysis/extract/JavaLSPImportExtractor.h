#ifndef TOPO_CHECK_JAVALSPIMPORTEXTRACTOR_H
#define TOPO_CHECK_JAVALSPIMPORTEXTRACTOR_H

#include "JavaImportExtractor.h"

#include <string>
#include <vector>

namespace topo::check {

/// Clean line-based Java import extractor.
///
/// Java `import` is deterministic syntax -- no need for LSP or regex.
/// This extractor does the same job as JavaImportExtractor but with
/// a cleaner, more maintainable implementation:
///   - Direct string matching instead of regex
///   - Same block-comment and string state machine
///   - JavaUnsafeCatalog classification for each import
///
/// Functionally equivalent to JavaImportExtractor.
class JavaLSPImportExtractor {
public:
    /// Extract all import paths from a single file.
    std::vector<HostImport> extractImports(const std::string& filePath);

    /// Extract imports from multiple files.
    std::vector<HostImport> extractAll(const std::vector<std::string>& files);
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVALSPIMPORTEXTRACTOR_H
