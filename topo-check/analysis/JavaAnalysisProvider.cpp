#include "JavaAnalysisProvider.h"
#include "JavaSourceFacts.h"
#include "JavaNativeEvidence.h"
#include "topo/Distribution/Sha256.h"
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
bool JavaAnalysisProvider::configureAuditExecution(const AuditExecutionOptions& options, std::string& diagnostic) {
    return facts_->requireAuditIsolation(options.inputRoots, diagnostic);
}
void JavaAnalysisProvider::configureAnalysis(const std::string& projectDir,const std::string& modelPath) { facts_->configure(projectDir,modelPath); }
std::vector<CheckDiagnostic> JavaAnalysisProvider::analysisDiagnostics() const { return facts_->diagnostics(); }
void JavaAnalysisProvider::configureNativeEvidence(const std::string& manifest, const std::map<std::string, std::string>& roots) {
    nativeManifest_ = manifest; nativeRoots_ = roots;
}
AuditFacts JavaAnalysisProvider::auditFacts(const std::vector<std::string>& sourceFiles) const {
    facts_->load(sourceFiles);
    auto audit = facts_->auditFacts();
    if (audit.modelPath.empty()) {
        audit.complete = false;
        audit.unresolved.push_back("Java compile model not supplied; audit cannot establish the selected source set or classpath");
    }
    if (nativeManifest_.empty()) return audit;
    build::evidence::Manifest manifest; std::string error;
    std::error_code pathError;
    auto manifestPath = fs::weakly_canonical(nativeManifest_, pathError).string();
    auto manifestDigest = dist::sha256File(nativeManifest_);
    if (!build::evidence::readManifest(nativeManifest_, manifest, error)) {
        audit.complete = false; audit.unresolved.push_back(error); return audit;
    }
    build::evidence::RootMap roots;
    for (const auto& [id, path] : nativeRoots_) roots.emplace(id, path);
    // Validate every entry against the explicitly authorized evidence roots
    // before any entry can reach parent-side report snapshots, even without FFM.
    // Incomplete build coverage does not invalidate content-only evidence.
    const auto verification = build::evidence::verifyManifest(manifest, roots);
    if (!verification.integrity) {
        audit.complete = false;
        for (const auto& diagnostic : verification.diagnostics)
            audit.unresolved.push_back("Native evidence integrity verification failed: " + diagnostic);
        return audit;
    }
    audit.inputs.push_back(manifestPath);
    audit.inputDigests[manifestPath] = manifestDigest;
    for (const auto& record : manifest.nodes) {
        auto addFiles = [&](const auto& files) {
            for (const auto& entry : files) {
                auto root = roots.find(entry.root); if (root == roots.end()) continue;
                auto path = fs::weakly_canonical(root->second / entry.path, pathError).string();
                if (pathError) continue;
                audit.inputs.push_back(path); audit.inputDigests[path] = entry.sha256;
            }
        };
        addFiles(record.action.inputs); addFiles(record.outputs); addFiles(record.metadata);
    }
    for (auto& binding : audit.nativeBindings) {
        if (binding.library.empty()) continue;
        binding = verifyJavaNativeBinding(binding, manifest, roots);
        if (!binding.staticBinding) {
            audit.complete = false;
            audit.unresolved.insert(audit.unresolved.end(), binding.diagnostics.begin(), binding.diagnostics.end());
        }
    }
    if (dist::sha256File(nativeManifest_) != manifestDigest) {
        audit.complete = false; audit.unresolved.push_back("Native evidence changed during Java audit");
    }
    return audit;
}
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
    // Audit source selection comes only from the authorized explicit model;
    // an empty or rejected selection must not turn into a filesystem scan.
    if (!files.empty() || facts_->auditIsolationRequired()) { facts_->load(files); return files; }
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
