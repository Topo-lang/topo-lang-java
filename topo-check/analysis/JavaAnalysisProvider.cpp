#include "JavaAnalysisProvider.h"
#include "JavaSourceFacts.h"
#include "JavaCallEdgeExtractor.h"
#include "JavaCallSiteExtractor.h"
#include "JavaImportExtractor.h"
#include "JavaLSPCallSiteExtractor.h"
#include "JavaLSPSymbolExtractor.h"
#include "JavaSafePatterns.h"
#include "JavaSafetyAnalyzer.h"
#include "JavaSymbolAccessExtractor.h"
#include "JavaSymbolExtractor.h"
#include "JdtBridge.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace topo::check {

JavaAnalysisProvider::JavaAnalysisProvider() : facts_(std::make_shared<JavaSourceFacts>()) {}
void JavaAnalysisProvider::configureAnalysis(const std::string& projectDir,const std::string& modelPath) { facts_->configure(projectDir,modelPath); }
std::vector<CheckDiagnostic> JavaAnalysisProvider::analysisDiagnostics() const { return facts_->diagnostics(); }
JavaAnalysisProvider::~JavaAnalysisProvider() {
    shutdownLSP();
}

std::unique_ptr<SymbolExtractor> JavaAnalysisProvider::createSymbolExtractor() {
    return javaFactSymbols(facts_);
}

std::unique_ptr<ImportExtractor> JavaAnalysisProvider::createImportExtractor() {
    // Import parsing is deterministic syntax — always use regex extractor
    return std::make_unique<JavaImportExtractor>();
}

std::unique_ptr<CallSiteExtractor> JavaAnalysisProvider::createCallSiteExtractor() {
    return javaFactCalls(facts_);
}

std::unique_ptr<CallEdgeExtractor> JavaAnalysisProvider::createCallEdgeExtractor() {
    return javaFactEdges(facts_);
}

std::unique_ptr<SymbolAccessExtractor> JavaAnalysisProvider::createSymbolAccessExtractor() {
    return javaFactAccesses(facts_);
}

std::vector<std::string> JavaAnalysisProvider::collectSourceFiles(
    const std::string& projectDir,
    const std::vector<std::string>& /*includeDirs*/) const {
    std::vector<std::string> files = facts_->sourceFiles();
    if (!files.empty()) { facts_->load(files); return files; }
    // Non-throwing scan (error_code construction + increment, same pattern
    // as the C++ provider and CheckRunner::discoverRelevantFiles): a source
    // root that is unexpectedly a plain file, unreadable, or vanishing must
    // degrade to skipping it, never abort the checker.
    auto scanDir = [&files](const fs::path& root) {
        std::error_code ec;
        if (fs::is_regular_file(root, ec)) {
            if (root.extension() == ".java") files.push_back(root.string());
            return;
        }
        if (!fs::exists(root, ec)) return;
        fs::recursive_directory_iterator it(root, ec);
        if (ec) return;
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->path().extension() == ".java")
                files.push_back(it->path().string());
        }
    };
    // Scan src/main/java (standard Maven/Gradle layout)
    scanDir(fs::path(projectDir) / "src" / "main" / "java");
    // Also check flat src/ directory
    scanDir(fs::path(projectDir) / "src");
    // Deduplicate (maven path is inside flat src/)
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    facts_->load(files);
    return files;
}

bool JavaAnalysisProvider::initLSP(const std::string& projectDir, bool verbose) {
    if (bridge_ && bridge_->isAvailable()) return true;

    auto bridge = std::make_unique<lsp::JdtBridge>();
    std::string rootUri = "file://" + fs::canonical(projectDir).string();

    if (!bridge->start("", rootUri)) {
        if (verbose) {
            std::cerr << "topo-check: JDT Language Server failed to start\n";
        }
        return false;
    }

    if (!bridge->isAvailable()) {
        if (verbose) {
            std::cerr << "topo-check: JDT Language Server not available\n";
        }
        bridge->stop();
        return false;
    }

    if (!bridge->waitForIndex(std::chrono::milliseconds{30000})) {
        // Index never settled — every subsequent sendRequest will block
        // on a pipe with no deadline. Tear the bridge down and let
        // runDeepContainment() fall back to L1 rather than hanging the
        // suite on the first deep query.
        std::cerr << "[topo-lsp] JDT LS index not ready after 30s, "
                     "marking bridge unavailable (L1 fallback)\n";
        bridge->stop();
        return false;
    }

    bridge_ = std::move(bridge);
    return true;
}

void JavaAnalysisProvider::shutdownLSP() {
    if (bridge_) {
        bridge_->stop();
        bridge_.reset();
    }
}

bool JavaAnalysisProvider::isLSPReady() const {
    return bridge_ && bridge_->isAvailable();
}

std::optional<CheckResult> JavaAnalysisProvider::runDeepContainment(
    const SymbolTable& symbols,
    const std::vector<std::string>& sourceFiles,
    const ContainmentConfig& config,
    const std::string& projectDir,
    bool verbose) {
    CheckResult result;

    JavaSafePatterns patterns;
    if (!patterns.loadDefault()) {
        CheckDiagnostic d;
        d.severity = Severity::Warning;
        d.check = "containment-l2";
        d.message = "JavaSafePatterns.toml not found — cannot run L2 analysis";
        result.addDiagnostic(std::move(d));
        return result;
    }

    if (!bridge_ || !bridge_->isAvailable()) {
        initLSP(projectDir, verbose);
    }
    if (!bridge_ || !bridge_->isAvailable()) {
        CheckDiagnostic d;
        d.severity = Severity::Warning;
        d.check = "containment-l2";
        d.message = "JDT Language Server unavailable — falling back to L1";
        result.addDiagnostic(std::move(d));
        return result;
    }

    JavaSafetyAnalyzer analyzer(*bridge_, patterns);
    result = analyzer.analyze(symbols, sourceFiles, config);
    return result;
}

std::unique_ptr<LanguageAnalysisProvider> createJavaAnalysisProvider() {
    return std::unique_ptr<LanguageAnalysisProvider>(new JavaAnalysisProvider());
}

} // namespace topo::check
