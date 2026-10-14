#ifndef TOPO_JAVA_SOURCE_FACTS_H
#define TOPO_JAVA_SOURCE_FACTS_H
#include "topo/Check/LanguageAnalysisProvider.h"
#include <memory>
#include <string>
#include <vector>
namespace topo::check {
struct JavaAnalysisLimits {
    int timeoutMs = 60000;
    std::size_t maxRequestBytes = 8 * 1024 * 1024;
    std::size_t maxResponseBytes = 64 * 1024 * 1024;
    std::size_t maxDiagnosticBytes = 1024 * 1024;
};
// One immutable JDT batch per check run. All factories share the same bindings.
class JavaSourceFacts {
public:
    explicit JavaSourceFacts(JavaAnalysisLimits limits = {});
    ~JavaSourceFacts();
    bool requireAuditIsolation(const std::vector<std::string>& inputRoots, std::string& diagnostic);
    bool auditIsolationRequired() const;
    void configure(const std::string& projectDir, const std::string& modelPath);
    std::vector<std::string> sourceFiles() const;
    void load(const std::vector<std::string>& files);
    void prepareFile(const std::string& file);
    std::vector<CheckDiagnostic> diagnostics() const;
    std::vector<HostSymbol> symbols(const std::string& file) const;
    std::vector<CallEdge> edges(const std::string& file) const;
    std::vector<DetectedCallSite> calls(const std::string& file) const;
    std::vector<SymbolAccess> accesses(const std::string& file) const;
    AuditFacts auditFacts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    JavaAnalysisLimits limits_;
    bool isolationRequired_ = false;
    std::vector<std::string> inputRoots_;
};
std::unique_ptr<SymbolExtractor> javaFactSymbols(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<CallEdgeExtractor> javaFactEdges(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<CallSiteExtractor> javaFactCalls(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<SymbolAccessExtractor> javaFactAccesses(std::shared_ptr<JavaSourceFacts> facts);
} // namespace topo::check
#endif
