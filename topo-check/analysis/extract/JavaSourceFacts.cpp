#include "JavaSourceFacts.h"
#include "JavaUnsafeCatalog.h"
#include "topo/Platform/Process.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
namespace topo::check {
using Json = nlohmann::json;
namespace {
std::string canonical(const std::string& path) {
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(path, ec);
    return ec ? std::filesystem::path(path).lexically_normal().string() : p.string();
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
    std::vector<std::string> analyzedFiles;
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
JavaSourceFacts::JavaSourceFacts() : impl_(std::make_unique<Impl>()) {}
JavaSourceFacts::~JavaSourceFacts() = default;
void JavaSourceFacts::configure(const std::string& projectDir, const std::string& modelPath) {
    impl_ = std::make_unique<Impl>();
    if (modelPath.empty()) return;
    try {
        std::filesystem::path path = modelPath;
        if (path.is_relative()) path = std::filesystem::path(projectDir) / path;
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot read " + path.string());
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
                    paths.push_back(canonical(p.string()));
                }
                impl_->request[key] = paths;
            }
        if (model.contains("sourceLevel")) impl_->request["sourceLevel"] = model.at("sourceLevel");
        if (impl_->request.contains("files")) impl_->files = impl_->request.at("files").get<std::vector<std::string>>();
    } catch (const std::exception& e) {
        impl_->error(std::string("Invalid Java project model: ") + e.what());
    }
}
std::vector<std::string> JavaSourceFacts::sourceFiles() const {
    return impl_->files;
}
void JavaSourceFacts::load(const std::vector<std::string>& files) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->loaded) return;
    impl_->loaded = true;
    impl_->analyzedFiles = files;
    impl_->request["files"] = files;
    if (files.empty()) return;
    platform::PipedProcess process;
    if (!process.start("topo-extract-java", {})) {
        impl_->error("topo-extract-java unavailable; Java AST analysis requires the Java 25 extractor on PATH");
        return;
    }
    std::string request = impl_->request.dump();
    if (!process.write(request.data(), request.size())) {
        process.stop();
        impl_->error("Failed to write Java analysis request");
        return;
    }
    process.closeStdin();
    std::string response;
    char buf[8192];
    for (;;) {
        size_t n = process.read(buf, sizeof(buf));
        if (!n) break;
        response.append(buf, n);
    }
    process.stop();
    try {
        Json output = Json::parse(response);
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
    bool present = false;
    for (const auto& p : impl_->analyzedFiles)
        if (canonical(p) == canonical(file)) present = true;
    if (!present) impl_->error("Requested file is outside the configured Java analysis batch", file);
}
std::vector<CheckDiagnostic> JavaSourceFacts::diagnostics() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->diagnostics;
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
