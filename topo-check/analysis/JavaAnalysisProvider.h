#ifndef TOPO_CHECK_JAVAANALYSISPROVIDER_H
#define TOPO_CHECK_JAVAANALYSISPROVIDER_H

#include "topo/Check/LanguageAnalysisProvider.h"

#include <memory>
#include <string>

// Forward declaration
namespace topo::lsp { class JdtBridge; }

namespace topo::check {
class JavaSourceFacts;

class JavaAnalysisProvider : public LanguageAnalysisProvider {
public:
    JavaAnalysisProvider();
    ~JavaAnalysisProvider() override;
    bool configureAuditExecution(const AuditExecutionOptions& options, std::string& diagnostic) override;
    void configureAnalysis(const std::string& projectDir, const std::string& projectModelPath) override;
    std::vector<CheckDiagnostic> analysisDiagnostics() const override;
    AuditFacts auditFacts(const std::vector<std::string>& sourceFiles) const override;
    void configureNativeEvidence(const std::string& manifest, const std::map<std::string, std::string>& roots) override;

    std::unique_ptr<SymbolExtractor> createSymbolExtractor() override;
    std::unique_ptr<ImportExtractor> createImportExtractor() override;
    std::unique_ptr<CallSiteExtractor> createCallSiteExtractor() override;
    std::unique_ptr<CallEdgeExtractor> createCallEdgeExtractor() override;
    std::unique_ptr<SymbolAccessExtractor> createSymbolAccessExtractor() override;
    std::vector<std::string> collectSourceFiles(
        const std::string& projectDir,
        const std::vector<std::string>& includeDirs) const override;

    /// Initialize the JDT LSP bridge. Returns true on success.
    std::optional<CheckResult> runDeepContainment(
        const SymbolTable& symbols,
        const std::vector<std::string>& sourceFiles,
        const ContainmentConfig& config,
        const std::string& projectDir,
        bool verbose) override;

    bool initLSP(const std::string& projectDir, bool verbose) override;

    /// Shut down the JDT LSP bridge.
    void shutdownLSP() override;

    /// Check if the LSP bridge is available.
    bool isLSPReady() const override;

private:
    std::unique_ptr<lsp::JdtBridge> bridge_;
    std::shared_ptr<JavaSourceFacts> facts_;
    std::string nativeManifest_;
    std::map<std::string, std::string> nativeRoots_;
};

/// Factory function (avoids incomplete-type issues when constructing via make_unique).
std::unique_ptr<LanguageAnalysisProvider> createJavaAnalysisProvider();

} // namespace topo::check

#endif // TOPO_CHECK_JAVAANALYSISPROVIDER_H
