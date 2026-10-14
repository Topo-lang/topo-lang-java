#include "JavaSourceFacts.h"
#include "JavaAuditRuntime.h"
#include "JavaUnsafeCatalog.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/ToolResolution.h"
#include "topo/Distribution/Sha256.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
namespace topo::check {
using Json = nlohmann::json;
namespace {
std::string canonical(const std::string& path) {
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(path, ec);
    return ec ? std::filesystem::path(path).lexically_normal().string() : p.string();
}
std::filesystem::path authorizedInput(const std::filesystem::path& path, const std::string& project,
                                      const std::vector<std::string>& inputRoots, const std::string& kind) {
    namespace fs = std::filesystem;
    const auto resolved = fs::canonical(path);
    auto roots = inputRoots;
    roots.push_back(project);
    bool authorized = false;
    for (const auto& root : roots) {
        const fs::path directory = root;
        if (!fs::is_directory(directory))
            throw std::runtime_error("analysis input root is not an existing directory: " + root);
        const auto relative = resolved.lexically_relative(directory);
        if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") authorized = true;
    }
    if (!authorized) throw std::runtime_error(kind + " is outside authorized analysis input roots");
    return resolved;
}
void validateAuditPaths(const Json& audit, const std::string& project,
                        const std::vector<std::string>& inputRoots, const std::set<std::string>& exactInputs) {
    namespace fs = std::filesystem;
    auto roots = inputRoots;
    roots.push_back(project);
    auto validate = [&](const std::string& text, bool mayBeEmpty) {
        if (text.empty() && mayBeEmpty) return;
        const fs::path path = text;
        bool authorized = false;
        if (!text.empty() && path.is_absolute()) {
            std::error_code ec;
            const auto resolved = fs::canonical(path, ec);
            if (!ec) {
                authorized = exactInputs.count(resolved.string()) != 0;
                for (const auto& root : roots) {
                    const auto relative = resolved.lexically_relative(fs::path(root));
                    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") authorized = true;
                }
            }
        }
        if (!authorized) throw std::runtime_error("Java audit response contains a path outside authorized analysis inputs");
    };
    for (const auto& path : audit.at("inputs")) validate(path.get<std::string>(), false);
    if (!audit.at("inputDigests").is_object()) throw std::runtime_error("Java audit inputDigests must be an object");
    for (const auto& entry : audit.at("inputDigests").items()) validate(entry.key(), false);
    for (const auto& identity : audit.at("identities")) {
        validate(identity.at("file").get<std::string>(), true);
        validate(identity.at("origin").get<std::string>(), true);
    }
    for (const auto& dependency : audit.at("dependencies")) validate(dependency.at("path").get<std::string>(), true);
    for (const char* field : {"references", "nativeBindings"})
        for (const auto& entry : audit.at(field)) validate(entry.at("file").get<std::string>(), true);
}
std::string qualified(std::string s) {
    std::string out;
    for (char c : s) {
        if (c == '.')
            out += "::";
        else
            out += c;
    }
    return out;
}
} // namespace
struct JavaSourceFacts::Impl {
    Json request = Json{{"mode", "facts"}, {"sourceLevel", "25"}}, facts = Json::object();
    std::vector<CheckDiagnostic> diagnostics;
    std::vector<std::string> files;
    mutable std::mutex mutex;
    bool loaded = false;
    bool invalidModel = false;
    std::vector<std::string> analyzedFiles;
    std::string modelPath;
    std::string extractorPath;
    std::string extractorIdentityPath;
    std::string modelDigest, extractorDigest;
    std::string isolation = "bounded-process";
    std::map<std::string, std::string> runtimeDigests, runtimeInventory;
    std::filesystem::path runtimeRoot;
    Json executionIdentity = Json::object();
    void error(const std::string& message, const std::string& file = "", int line = 0) {
        CheckDiagnostic d;
        d.severity = Severity::Error;
        d.check = "java-analysis";
        d.message = message;
        d.file = file;
        d.line = line;
        diagnostics.push_back(std::move(d));
    }
    Json entries(const char* key, const std::string& file) const {
        Json out = Json::array();
        if (!facts.contains(key)) return out;
        for (const auto& e : facts.at(key))
            if (canonical(e.value("file", std::string())) == canonical(file)) out.push_back(e);
        return out;
    }
};
JavaSourceFacts::JavaSourceFacts(JavaAnalysisLimits limits) : impl_(std::make_unique<Impl>()), limits_(limits) {
    limits_.timeoutMs = std::clamp(limits_.timeoutMs, 1, 60000);
}
JavaSourceFacts::~JavaSourceFacts() = default;
bool JavaSourceFacts::requireAuditIsolation(const std::vector<std::string>& inputRoots, std::string& diagnostic) {
    isolationRequired_ = true;
    inputRoots_.clear();
    try {
        for (const auto& root : inputRoots) {
            auto resolved = std::filesystem::canonical(root);
            if (!std::filesystem::is_directory(resolved))
                throw std::runtime_error("analysis input root is not an existing directory: " + root);
            inputRoots_.push_back(resolved.string());
        }
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
    auto capability = platform::analysisProcessCapability();
    diagnostic = capability.diagnostic;
    return capability.supported;
}
bool JavaSourceFacts::auditIsolationRequired() const {
    return isolationRequired_;
}
void JavaSourceFacts::configure(const std::string& projectDir, const std::string& modelPath) {
    impl_ = std::make_unique<Impl>();
    impl_->request["projectRoot"] = canonical(projectDir);
    if (modelPath.empty()) return;
    impl_->invalidModel = true;
    try {
        std::filesystem::path path = modelPath;
        if (path.is_relative()) path = std::filesystem::path(projectDir) / path;
        // Parent-side parsing and hashing need the same explicit input authority
        // as the extractor. Resolve symlinks before any model content is read.
        const auto project = impl_->request.at("projectRoot").get<std::string>();
        const auto readPath = isolationRequired_ ? authorizedInput(path, project, inputRoots_, "project model") : path;
        std::error_code sizeError;
        if (std::filesystem::file_size(readPath, sizeError) > limits_.maxRequestBytes || sizeError)
            throw std::runtime_error("project model is unavailable or exceeds the configured byte limit");
        std::ifstream input(readPath);
        if (!input) throw std::runtime_error("cannot read " + readPath.string());
        const auto modelDigest = dist::sha256File(readPath.string());
        Json model;
        input >> model;
        if (!model.is_object()) throw std::runtime_error("project model must be an object");
        if (model.contains("sourceLevel") && !model.at("sourceLevel").is_string())
            throw std::runtime_error("sourceLevel must be a string");
        for (const char* key : {"classpath", "sourcepath", "files"})
            if (model.contains(key)) {
                if (!model.at(key).is_array()) throw std::runtime_error(std::string(key) + " must be an array");
                Json paths = Json::array();
                for (const auto& e : model.at(key)) {
                    if (!e.is_string() || e.get<std::string>().empty())
                        throw std::runtime_error(std::string(key) + " entries must be non-empty strings");
                    std::filesystem::path p = e.get<std::string>();
                    if (p.is_relative()) p = path.parent_path() / p;
                    // Source selections are also consumed by the parent report
                    // generator, before the runtime validates extractor inputs.
                    if (isolationRequired_ && std::string(key) == "files")
                        p = authorizedInput(p, project, inputRoots_, "Java source file");
                    paths.push_back(canonical(p.string()));
                }
                impl_->request[key] = paths;
            }
        if (model.contains("sourceLevel")) impl_->request["sourceLevel"] = model.at("sourceLevel");
        if (model.contains("compiler")) {
            auto compiler = model.at("compiler");
            if (!compiler.is_object() || !compiler.contains("javaHome") || !compiler.at("javaHome").is_string() ||
                !compiler.contains("targetRelease") || !compiler.at("targetRelease").is_string())
                throw std::runtime_error("compiler requires javaHome and targetRelease strings");
            std::filesystem::path home = compiler.at("javaHome").get<std::string>();
            if (home.empty()) throw std::runtime_error("compiler.javaHome must be nonempty");
            if (home.is_relative()) home = path.parent_path() / home;
            compiler["javaHome"] = canonical(home.string());
            impl_->request["compiler"] = compiler;
        }
        if (model.contains("dependencies")) {
            if (!model.at("dependencies").is_array()) throw std::runtime_error("dependencies must be an array");
            Json dependencies = Json::array();
            for (auto dependency : model.at("dependencies")) {
                if (!dependency.is_object() || !dependency.at("path").is_string() || !dependency.at("kind").is_string())
                    throw std::runtime_error("dependency path/kind must be strings");
                std::filesystem::path p = dependency.at("path").get<std::string>();
                if (p.empty()) throw std::runtime_error("dependency path must be nonempty");
                if (p.is_relative()) p = path.parent_path() / p;
                dependency["path"] = canonical(p.string());
                auto kind = dependency.at("kind").get<std::string>();
                if (kind != "project" && kind != "external" && kind != "unknown")
                    throw std::runtime_error("dependency kind must be project, external or unknown");
                for (const char* field : {"coordinate", "version"})
                    if (dependency.contains(field) && !dependency.at(field).is_string())
                        throw std::runtime_error(std::string("dependency ") + field + " must be a string");
                dependencies.push_back(dependency);
            }
            impl_->request["dependencies"] = dependencies;
        }
        if (impl_->request.contains("files")) impl_->files = impl_->request.at("files").get<std::vector<std::string>>();
        // The report generator snapshots modelPath; publish it only after the
        // configured model has passed authorization and validation.
        impl_->modelPath = canonical(readPath.string());
        impl_->modelDigest = modelDigest;
        impl_->invalidModel = false;
    } catch (const std::exception& e) {
        impl_->error(std::string("Invalid Java project model: ") + e.what());
    }
}
std::vector<std::string> JavaSourceFacts::sourceFiles() const {
    return impl_->files;
}
void JavaSourceFacts::load(const std::vector<std::string>& files) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->loaded || impl_->invalidModel) return;
    impl_->loaded = true;
    impl_->analyzedFiles = files;
    impl_->request["files"] = files;
    if (files.empty()) return;
    std::string request = impl_->request.dump();
    if (request.size() > limits_.maxRequestBytes) {
        impl_->error("Java analysis request exceeds the configured byte limit");
        return;
    }
    impl_->extractorPath = platform::findSpawnableOnPath("topo-extract-java");
    if (!impl_->extractorPath.empty()) {
        impl_->extractorIdentityPath = canonical(impl_->extractorPath);
        impl_->extractorDigest = dist::sha256File(impl_->extractorIdentityPath);
    }
    if (impl_->extractorPath.empty()) {
        impl_->error("topo-extract-java unavailable; Java AST analysis requires the Java 25 extractor on PATH");
        return;
    }
    platform::ProcessCaptureLimits capture;
    capture.timeoutMs = limits_.timeoutMs;
    capture.maxStdoutBytes = limits_.maxResponseBytes;
    capture.maxStderrBytes = limits_.maxDiagnosticBytes;
    capture.maxStdinBytes = limits_.maxRequestBytes;
    capture.stdinInput = std::move(request);
    platform::BoundedProcessResult process;
    if (isolationRequired_) {
        try {
            const auto capability = platform::analysisProcessCapability();
            if (!capability.supported) throw std::runtime_error(capability.diagnostic);
            auto runtime = resolveJavaAuditRuntime(impl_->extractorPath, impl_->request, inputRoots_);
            impl_->executionIdentity = {{"policy", "java-audit-isolation-v1"}, {"platform", capability.platformIdentity},
                {"tools", Json::array()}, {"inputRoots", Json::array()}};
            const std::filesystem::path project = impl_->request.at("projectRoot").get<std::string>();
            for (const auto& tool : capability.executionTools) {
                auto resolved = std::filesystem::canonical(tool);
                auto relative = resolved.lexically_relative(project);
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    throw std::runtime_error("analysis execution tool is inside the subject project");
                auto digest = dist::sha256File(resolved.string());
                if (digest.empty()) throw std::runtime_error("cannot snapshot analysis execution tool: " + resolved.string());
                runtime.inputDigests[resolved.string()] = digest;
                impl_->executionIdentity["tools"].push_back({{"path", resolved.string()}, {"sha256", digest}});
            }
            for (const auto& root : runtime.policy.readOnlyDirectories)
                impl_->executionIdentity["inputRoots"].push_back(root.string());
            impl_->runtimeDigests = runtime.inputDigests;
            impl_->runtimeInventory = runtime.runtimeInventory;
            impl_->runtimeRoot = runtime.runtimeRoot;
            auto isolated = platform::runAnalysisProcess(runtime.executable, runtime.arguments, runtime.policy, capture);
            if (!isolated.isolationEstablished) {
                impl_->error("Java audit isolation could not be established: " + isolated.diagnostic + isolated.stderrOutput);
                return;
            }
            if (isolated.executionTools != capability.executionTools || isolated.platformIdentity != capability.platformIdentity)
                throw std::runtime_error("analysis execution chain changed before launch");
            impl_->isolation = isolated.isolation;
            process = std::move(isolated);
        } catch (const std::exception& error) {
            impl_->error(std::string("Java audit runtime: ") + error.what());
            return;
        }
    } else process = platform::runProcessCaptureBounded(impl_->extractorPath, {}, capture);
    if (process.failure == platform::ProcessCaptureFailure::TimedOut) {
        impl_->error("Java analysis exceeded its time limit");
        return;
    }
    if (process.failure == platform::ProcessCaptureFailure::OutputLimitExceeded) {
        impl_->error("Java analysis response exceeds the configured byte limit: " + process.diagnostic);
        return;
    }
    if (process.failure != platform::ProcessCaptureFailure::None) {
        impl_->error("Java analysis process failed: " + process.diagnostic);
        return;
    }
    if (process.exitCode != 0) {
        impl_->error("Java extractor exited with code " + std::to_string(process.exitCode) + ": " + process.stderrOutput);
        return;
    }
    try {
        Json output = Json::parse(process.stdoutOutput);
        if (!output.is_object() || !output.contains("schemaVersion") ||
            !output.at("schemaVersion").is_number_integer() || output.at("schemaVersion") != 2 ||
            !output.contains("facts") || !output.at("facts").is_object() || !output.contains("diagnostics") ||
            !output.at("diagnostics").is_array() || !output.contains("analysisComplete") ||
            !output.at("analysisComplete").is_boolean())
            throw std::runtime_error(
                "Java extractor requires schemaVersion 2, object facts, array diagnostics, and boolean "
                "analysisComplete; reinstall the matching extractor");
        std::vector<CheckDiagnostic> diagnostics;
        for (const auto& d : output.at("diagnostics")) {
            if (!d.is_object()) throw std::runtime_error("Java extractor diagnostic must be an object");
            CheckDiagnostic diag;
            diag.check = "java-analysis";
            diag.severity =
                d.value("severity", std::string("error")) == "warning" ? Severity::Warning : Severity::Error;
            diag.message = d.value("code", std::string("java")) + ": " + d.value("message", std::string());
            diag.file = d.value("file", std::string());
            diag.line = d.value("line", 0);
            diagnostics.push_back(std::move(diag));
        }
        impl_->facts = output.at("facts");
        impl_->diagnostics.insert(impl_->diagnostics.end(), diagnostics.begin(), diagnostics.end());
        if (!output.at("analysisComplete").get<bool>() &&
            std::none_of(impl_->diagnostics.begin(), impl_->diagnostics.end(), [](const CheckDiagnostic& d) {
                return d.severity == Severity::Error;
            }))
            impl_->error("Java extractor reported incomplete analysis without an error diagnostic");
    } catch (const std::exception& e) {
        impl_->error(std::string("Java AST extraction failed: ") + e.what());
    }
}
void JavaSourceFacts::prepareFile(const std::string& file) {
    auto files = sourceFiles();
    if (files.empty()) files.push_back(file);
    load(files);
    std::lock_guard lock(impl_->mutex);
    if (impl_->invalidModel) return;
    bool present = false;
    for (const auto& p : impl_->analyzedFiles)
        if (canonical(p) == canonical(file)) present = true;
    if (!present) impl_->error("Requested file is outside the configured Java analysis batch", file);
}
std::vector<CheckDiagnostic> JavaSourceFacts::diagnostics() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->diagnostics;
}
AuditFacts JavaSourceFacts::auditFacts() const {
    std::lock_guard lock(impl_->mutex);
    AuditFacts out;
    out.modelPath = impl_->modelPath;
    out.provider = "java-jdt";
    out.buildVariant = "java-" + impl_->request.value("sourceLevel", std::string("25"));
    if (!impl_->facts.contains("audit")) {
        for (const auto& diagnostic : impl_->diagnostics)
            if (diagnostic.severity == Severity::Error) out.unresolved.push_back(diagnostic.message);
        if (out.unresolved.empty())
            out.unresolved.push_back("Java extractor did not provide versioned audit facts; use a matching extractor");
        return out;
    }
    try {
        const auto& a = impl_->facts.at("audit");
        if (!a.is_object() || !a.at("schemaVersion").is_number_integer() || a.at("schemaVersion") != 1)
            throw std::runtime_error("Java audit facts require schemaVersion 1");
        for (const char* field : {"identities", "dependencies", "references", "nativeBindings", "inputs", "unresolved"})
            if (!a.at(field).is_array()) throw std::runtime_error(std::string("Java audit field must be an array: ") + field);
        if (isolationRequired_) {
            std::set<std::string> exactInputs;
            for (const auto& [path, digest] : impl_->runtimeDigests) {
                (void)digest;
                exactInputs.insert(path);
            }
            if (!impl_->extractorIdentityPath.empty()) exactInputs.insert(impl_->extractorIdentityPath);
            if (!impl_->modelPath.empty()) exactInputs.insert(impl_->modelPath);
            // Validate the whole response before copying or hashing any child
            // path. Report generation may snapshot even an incomplete batch.
            validateAuditPaths(a, impl_->request.at("projectRoot").get<std::string>(), inputRoots_, exactInputs);
        }
        out.supported = a.at("supported").get<bool>();
        out.complete = a.at("complete").get<bool>();
        out.provider = a.at("provider").get<std::string>();
        out.inputs = a.at("inputs").get<std::vector<std::string>>();
        out.inputDigests = a.at("inputDigests").get<std::map<std::string, std::string>>();
        // Isolation is the parent's execution result, never a child assertion.
        out.analysisIsolation = impl_->isolation;
        for (const auto& [path, digest] : impl_->runtimeDigests) {
            out.inputs.push_back(path);
            out.inputDigests[path] = digest;
        }
        out.buildVariant += "-" + impl_->isolation;
        if (!impl_->runtimeRoot.empty()) out.buildVariant += "-" + dist::sha256Hex(
            Json{{"runtimeInventory", impl_->runtimeInventory}, {"execution", impl_->executionIdentity}}.dump());
        if (!impl_->extractorIdentityPath.empty()) out.inputs.push_back(impl_->extractorIdentityPath);
        if (!out.modelPath.empty()) out.inputs.push_back(out.modelPath);
        if (!impl_->extractorIdentityPath.empty()) out.inputDigests[impl_->extractorIdentityPath] = impl_->extractorDigest;
        if (!out.modelPath.empty()) out.inputDigests[out.modelPath] = impl_->modelDigest;
        out.unresolved = a.at("unresolved").get<std::vector<std::string>>();
        if (!impl_->runtimeRoot.empty() && javaAuditRuntimeInventory(impl_->runtimeRoot) != impl_->runtimeInventory) {
            out.complete = false;
            out.unresolved.push_back("Java runtime file inventory changed during analysis");
        }
        for (const auto& input : out.inputs) {
            auto digest = out.inputDigests.find(input);
            if (digest == out.inputDigests.end() || digest->second.empty() ||
                dist::sha256File(input) != digest->second) {
                out.complete = false;
                out.unresolved.push_back("Java analysis input changed or lacks a content snapshot: " + input);
            }
        }
        for (const auto& e : a.at("identities")) {
            AuditIdentity v;
            v.id = e.at("id").get<std::string>();
            v.name = qualified(e.at("name").get<std::string>());
            v.kind = e.at("kind").get<std::string>();
            v.signature = e.at("signature").get<std::string>();
            v.owner = e.at("owner").get<std::string>();
            v.file = e.at("file").get<std::string>();
            v.line = e.at("line").get<int>();
            v.origin = e.at("origin").get<std::string>();
            v.complete = e.at("complete").get<bool>();
            out.identities.push_back(std::move(v));
        }
        for (const auto& e : a.at("dependencies")) {
            AuditDependency v;
            v.id = e.at("id").get<std::string>();
            v.path = e.at("path").get<std::string>();
            v.version = e.at("version").get<std::string>();
            v.digest = e.at("digest").get<std::string>();
            v.project = e.at("project").get<bool>();
            v.ownership = e.at("ownership").get<std::string>();
            v.coordinate = e.at("coordinate").get<std::string>();
            out.dependencies.push_back(std::move(v));
        }
        for (const auto& e : a.at("references")) {
            AuditReference v;
            v.caller = e.at("caller").get<std::string>();
            v.target = e.at("target").get<std::string>();
            v.file = e.at("file").get<std::string>();
            v.line = e.at("line").get<int>();
            v.resolved = e.at("resolved").get<bool>();
            v.dispatch = e.at("dispatch").get<std::string>();
            v.runtimeTargetsComplete = e.at("runtimeTargetsComplete").get<bool>();
            v.dependency = e.at("dependency").get<std::string>();
            out.references.push_back(std::move(v));
        }
        for (const auto& e : a.at("nativeBindings")) {
            AuditNativeBinding v;
            v.caller = e.at("caller").get<std::string>();
            v.file = e.at("file").get<std::string>();
            v.line = e.at("line").get<int>();
            v.operation = e.at("operation").get<std::string>();
            v.library = e.at("library").get<std::string>();
            v.symbol = e.at("symbol").get<std::string>();
            v.descriptor = e.at("descriptor").get<std::string>();
            v.resolved = e.at("resolved").get<bool>();
            v.callback = e.at("callback").get<std::string>();
            v.lookup = e.at("lookup").get<std::string>();
            v.unresolvedReason = e.at("unresolvedReason").get<std::string>();
            out.nativeBindings.push_back(std::move(v));
        }
        for (const auto& diagnostic : impl_->diagnostics) if (diagnostic.severity == Severity::Error) {
            out.complete = false;
            out.unresolved.push_back(diagnostic.message);
        }
    } catch (const std::exception& e) {
        if (isolationRequired_) {
            AuditFacts rejected;
            rejected.provider = "java-jdt";
            rejected.modelPath = impl_->modelPath;
            rejected.buildVariant = "java-" + impl_->request.value("sourceLevel", std::string("25"));
            rejected.analysisIsolation = impl_->isolation;
            rejected.unresolved.push_back(std::string("Invalid Java audit facts: ") + e.what());
            return rejected;
        }
        out.complete = false;
        out.unresolved.push_back(std::string("Invalid Java audit facts: ") + e.what());
    }
    return out;
}
std::vector<HostSymbol> JavaSourceFacts::symbols(const std::string& file) const {
    std::vector<HostSymbol> out;
    for (const auto& e : impl_->entries("symbols", file)) {
        HostSymbol s;
        s.qualifiedName = qualified(e.value("qualifiedName", std::string()));
        s.simpleName = e.value("simpleName", std::string());
        std::string kind = e.value("kind", std::string());
        s.kind = kind == "class"         ? HostSymbolKind::Class
                 : kind == "interface"   ? HostSymbolKind::Interface
                 : kind == "enum"        ? HostSymbolKind::Enum
                 : kind == "constructor" ? HostSymbolKind::Constructor
                                         : HostSymbolKind::Method;
        s.file = e.value("file", file);
        s.line = e.value("line", 0);
        s.isStatic = e.value("isStatic", false);
        s.enclosingClass = qualified(e.value("enclosingClass", std::string()));
        s.returnType = e.value("returnType", std::string());
        if (e.contains("paramTypes")) s.paramTypes = e.at("paramTypes").get<std::vector<std::string>>();
        std::string vis = e.value("visibility", std::string());
        if (vis == "public")
            s.hostVisibility = Visibility::Public;
        else if (vis == "protected")
            s.hostVisibility = Visibility::Protected;
        else if (vis == "private")
            s.hostVisibility = Visibility::Private;
        else
            s.hostVisibility = Visibility::Internal;
        out.push_back(std::move(s));
    }
    return out;
}
std::vector<CallEdge> JavaSourceFacts::edges(const std::string& file) const {
    std::vector<CallEdge> out;
    for (const auto& e : impl_->entries("calls", file))
        if (e.value("bindingResolved", false))
            out.push_back({qualified(e.at("caller").get<std::string>()),
                           qualified(e.at("callee").get<std::string>()),
                           e.value("file", file),
                           e.value("line", 0)});
    return out;
}
std::vector<DetectedCallSite> JavaSourceFacts::calls(const std::string& file) const {
    std::vector<DetectedCallSite> out;
    for (const auto& e : impl_->entries("calls", file))
        if (e.value("bindingResolved", false) && e.value("external", false)) {
            DetectedCallSite c;
            c.callerQualifiedName = qualified(e.at("caller").get<std::string>());
            c.calleePattern = qualified(e.at("callee").get<std::string>());
            // The catalog's known-call entries use Java surface dots and often
            // a class/method suffix; checker identities use ::. Classify the
            // resolved Java name before normalizing its presentation.
            std::string dotted = e.at("callee").get<std::string>();
            c.unsafeLevel = JavaUnsafeCatalog::classifyImport(dotted);
            for (std::string candidate = dotted;;) {
                auto level = JavaUnsafeCatalog::classifyCall(candidate);
                if (static_cast<int>(level) > static_cast<int>(c.unsafeLevel)) c.unsafeLevel = level;
                auto dot = candidate.find('.');
                if (dot == std::string::npos) break;
                candidate.erase(0, dot + 1);
            }
            c.file = e.value("file", file);
            c.line = e.value("line", 0);
            out.push_back(std::move(c));
        }
    return out;
}
std::vector<SymbolAccess> JavaSourceFacts::accesses(const std::string& file) const {
    std::vector<SymbolAccess> out;
    for (const auto& e : impl_->entries("accesses", file))
        out.push_back({qualified(e.at("function").get<std::string>()),
                       qualified(e.at("symbol").get<std::string>()),
                       e.value("isWrite", false),
                       e.value("file", file),
                       e.value("line", 0)});
    return out;
}
namespace {
class Symbols : public SymbolExtractor {
    std::shared_ptr<JavaSourceFacts> f;

public:
    explicit Symbols(std::shared_ptr<JavaSourceFacts> f) : f(std::move(f)) {}
    std::vector<HostSymbol> extractSymbols(const std::string& p) override {
        f->prepareFile(p);
        return f->symbols(p);
    }
};
class Edges : public CallEdgeExtractor {
    std::shared_ptr<JavaSourceFacts> f;

public:
    explicit Edges(std::shared_ptr<JavaSourceFacts> f) : f(std::move(f)) {}
    std::vector<CallEdge> extractCallEdges(const std::string& p) override {
        f->prepareFile(p);
        return f->edges(p);
    }
};
class Calls : public CallSiteExtractor {
    std::shared_ptr<JavaSourceFacts> f;

public:
    explicit Calls(std::shared_ptr<JavaSourceFacts> f) : f(std::move(f)) {}
    std::vector<DetectedCallSite> extractCallSites(const std::string& p) override {
        f->prepareFile(p);
        return f->calls(p);
    }
};
class Accesses : public SymbolAccessExtractor {
    std::shared_ptr<JavaSourceFacts> f;

public:
    explicit Accesses(std::shared_ptr<JavaSourceFacts> f) : f(std::move(f)) {}
    std::vector<SymbolAccess> extractSymbolAccesses(const std::string& p) override {
        f->prepareFile(p);
        return f->accesses(p);
    }
};
} // namespace
std::unique_ptr<SymbolExtractor> javaFactSymbols(std::shared_ptr<JavaSourceFacts> f) {
    return std::make_unique<Symbols>(std::move(f));
}
std::unique_ptr<CallEdgeExtractor> javaFactEdges(std::shared_ptr<JavaSourceFacts> f) {
    return std::make_unique<Edges>(std::move(f));
}
std::unique_ptr<CallSiteExtractor> javaFactCalls(std::shared_ptr<JavaSourceFacts> f) {
    return std::make_unique<Calls>(std::move(f));
}
std::unique_ptr<SymbolAccessExtractor> javaFactAccesses(std::shared_ptr<JavaSourceFacts> f) {
    return std::make_unique<Accesses>(std::move(f));
}
} // namespace topo::check
