#ifndef TOPO_CHECK_JAVALSPECALLSITEEXTRACTOR_H
#define TOPO_CHECK_JAVALSPECALLSITEEXTRACTOR_H

#include "JavaCallSiteExtractor.h"

#include <string>
#include <vector>

// Forward declaration
namespace topo::lsp { class JdtBridge; }

namespace topo::check {

/// LSP-based Java call site extractor using JDT semantic tokens and hover.
///
/// Extracts function/method call references by:
/// 1. Opening the document for JDT analysis
/// 2. Requesting semantic tokens to find call references (non-declaration tokens)
/// 3. Using hover to resolve qualified callee names
/// 4. Classifying each callee via JavaUnsafeCatalog
///
/// This extractor handles what regex cannot: qualified name resolution across
/// packages and inner classes.
/// Language escape constructs (reflection, etc.) are left to the
/// regex-based JavaCallSiteExtractor as a supplement.
class JavaLSPCallSiteExtractor {
public:
    explicit JavaLSPCallSiteExtractor(lsp::JdtBridge& bridge);

    /// Extract call sites from a single source file.
    /// Returns only function/method call references with resolved qualified names.
    std::vector<DetectedCallSite> extractCallSites(const std::string& filePath);

private:
    lsp::JdtBridge& bridge_;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVALSPECALLSITEEXTRACTOR_H
