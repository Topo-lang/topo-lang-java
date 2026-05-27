#include "JavaAnalysisProvider.h"
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

JavaAnalysisProvider::~JavaAnalysisProvider() {
    shutdownLSP();
}

std::unique_ptr<SymbolExtractor> JavaAnalysisProvider::createSymbolExtractor() {
    if (isLSPReady() && bridge_->hasSemanticTokens()) {
        return std::make_unique<JavaLSPSymbolExtractor>(*bridge_);
    }
    return std::make_unique<JavaSymbolExtractor>(); // L1 regex fallback
}

std::unique_ptr<ImportExtractor> JavaAnalysisProvider::createImportExtractor() {
    // Import parsing is deterministic syntax — always use regex extractor
    return std::make_unique<JavaImportExtractor>();
}

std::unique_ptr<CallSiteExtractor> JavaAnalysisProvider::createCallSiteExtractor() {
    return std::make_unique<JavaCallSiteExtractor>(); // L1 regex fallback
}

std::unique_ptr<CallEdgeExtractor> JavaAnalysisProvider::createCallEdgeExtractor() {
    // Java currently runs L1 call-edge extraction only; the LSP/L2 merging
    // layer that the C++ provider uses for clangd-driven cross-TU edges has
    // no jdtls counterpart wired here.
    return std::make_unique<JavaCallEdgeExtractor>();
}

std::unique_ptr<SymbolAccessExtractor> JavaAnalysisProvider::createSymbolAccessExtractor() {
    return std::make_unique<JavaSymbolAccessExtractor>();
}

std::vector<std::string> JavaAnalysisProvider::collectSourceFiles(
    const std::string& projectDir,
    const std::vector<std::string>& /*includeDirs*/) const {
    std::vector<std::string> files;
    // Scan src/main/java (standard Maven/Gradle layout)
    fs::path mavenSrc = fs::path(projectDir) / "src" / "main" / "java";
    if (fs::exists(mavenSrc)) {
        for (const auto& entry : fs::recursive_directory_iterator(mavenSrc)) {
            if (entry.path().extension() == ".java")
                files.push_back(entry.path().string());
        }
    }
    // Also check flat src/ directory
    fs::path flatSrc = fs::path(projectDir) / "src";
    if (fs::exists(flatSrc)) {
        for (const auto& entry : fs::recursive_directory_iterator(flatSrc)) {
            if (entry.path().extension() == ".java")
                files.push_back(entry.path().string());
        }
    }
    // Deduplicate (maven path is inside flat src/)
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
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
