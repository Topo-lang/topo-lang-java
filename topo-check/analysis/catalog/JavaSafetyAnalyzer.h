#ifndef TOPO_CHECK_JAVASAFETYANALYZER_H
#define TOPO_CHECK_JAVASAFETYANALYZER_H

#include "JavaSafePatterns.h"
#include "topo/Check/ContainmentCheck.h"

#include <string>
#include <vector>

// Forward declarations
namespace topo::lsp { class JdtBridge; }
namespace topo { class SymbolTable; }

namespace topo::check {

/// L2 whitelist-based containment analyzer using JDT semantic analysis.
/// Resolves call targets via LSP and checks them against JavaSafePatterns.
class JavaSafetyAnalyzer {
public:
    JavaSafetyAnalyzer(lsp::JdtBridge& bridge, const JavaSafePatterns& patterns);

    /// Analyze source files for containment violations.
    /// Non-external functions calling non-whitelisted targets are reported.
    /// If JDT is available: L2 semantic analysis.
    CheckResult analyze(const SymbolTable& symbols,
                        const std::vector<std::string>& sourceFiles,
                        const ContainmentConfig& config);

private:
    /// Analyze a single source file, appending detected call sites.
    /// Returns true if JDT produced semantic tokens for this file
    /// (analysis ran, even if it found nothing). Returns false if tokens
    /// were empty, signaling that L2 could not introspect this file at
    /// all — the caller must surface this as a visible warning rather
    /// than silently treating the file as clean (principle 16).
    bool analyzeFile(const std::string& filePath,
                     const SymbolTable& symbols,
                     const ContainmentConfig& config,
                     std::vector<DetectedCallSite>& callSites);

    lsp::JdtBridge& bridge_;
    const JavaSafePatterns& patterns_;
};

} // namespace topo::check

#endif // TOPO_CHECK_JAVASAFETYANALYZER_H
