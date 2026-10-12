#ifndef TOPO_JAVA_SOURCE_FACTS_H
#define TOPO_JAVA_SOURCE_FACTS_H
#include "topo/Check/LanguageAnalysisProvider.h"
#include <memory>
#include <string>
#include <vector>
namespace topo::check {
// One immutable JDT batch per check run. All factories share the same bindings.
class JavaSourceFacts {
public:
    JavaSourceFacts();
    ~JavaSourceFacts();
    void configure(const std::string& projectDir, const std::string& modelPath);
    std::vector<std::string> sourceFiles() const;
    void load(const std::vector<std::string>& files);
    void prepareFile(const std::string& file);
    std::vector<CheckDiagnostic> diagnostics() const;
    std::vector<HostSymbol> symbols(const std::string& file) const;
    std::vector<CallEdge> edges(const std::string& file) const;
    std::vector<DetectedCallSite> calls(const std::string& file) const;
    std::vector<SymbolAccess> accesses(const std::string& file) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::unique_ptr<SymbolExtractor> javaFactSymbols(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<CallEdgeExtractor> javaFactEdges(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<CallSiteExtractor> javaFactCalls(std::shared_ptr<JavaSourceFacts> facts);
std::unique_ptr<SymbolAccessExtractor> javaFactAccesses(std::shared_ptr<JavaSourceFacts> facts);
} // namespace topo::check
#endif
