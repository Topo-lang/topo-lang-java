#include "JdtBridge.h"

#include "topo/Platform/Platform.h"
#include "topo/Platform/TempFile.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

namespace topo::lsp {

namespace {

/// Derive a stable per-project workspace data directory for jdtls.
///
/// The jdtls launcher (Homebrew/upstream) defaults its workspace cache to
/// `~/Library/Caches/jdtls/jdtls-<sha1(basename(cwd))>` — a key that only
/// uses the working-directory basename. When multiple unrelated projects
/// share the same basename (notably `test` for CMake-built test binaries
/// run via `ctest`, whose Working Directory is `build/.../test`), they
/// share a poisoned workspace cache and the symbol/semantic-token index
/// produced for one project leaks into another, returning stale or empty
/// results for files outside the cached project.
///
/// Pass `-data <unique-path>` explicitly so each Topo project gets an
/// isolated workspace keyed off its canonical root path.
std::string deriveWorkspaceDataDir(const std::string& rootUri) {
    namespace fs = std::filesystem;

    // Strip the "file://" prefix if present so std::hash sees the same key
    // regardless of how the caller spelled the URI.
    std::string key = rootUri;
    const std::string filePrefix = "file://";
    if (key.size() > filePrefix.size() &&
        key.substr(0, filePrefix.size()) == filePrefix) {
        key = key.substr(filePrefix.size());
    }

    // Canonicalize so different relative paths to the same project collapse
    // to the same workspace directory.
    {
        std::error_code ec;
        auto abs = fs::canonical(key, ec);
        if (!ec) key = abs.generic_string();
    }

    // Pick a cache base that mirrors the jdtls launcher convention so
    // workspaces remain inspectable / cleanable via the documented path.
    // `topo::platform::homeDirectory()` honours USERPROFILE on Windows so
    // this stays cross-platform; if the env is entirely unset we fall back
    // to the platform temp directory.
    fs::path cacheBase;
    fs::path home = topo::platform::homeDirectory();
    if (!home.empty()) {
#ifdef __APPLE__
        cacheBase = home / "Library" / "Caches" / "jdtls";
#elif defined(_WIN32)
        cacheBase = home / "AppData" / "Local" / "jdtls";
#else
        cacheBase = home / ".cache" / "jdtls";
#endif
    } else {
        cacheBase = topo::platform::tempDirectory() / "jdtls";
    }

    std::error_code ec;
    fs::create_directories(cacheBase, ec);

    std::ostringstream oss;
    oss << "topo-" << std::hex << std::hash<std::string>{}(key);
    return (cacheBase / oss.str()).generic_string();
}

} // namespace

JdtBridge::JdtBridge() : LSPBridge("[topo-lsp]") {}

JdtBridge::~JdtBridge() {
    // Best-effort cleanup of the per-project workspace data dir. jdtls
    // populates this with its indexed-project cache; without cleanup the
    // dir grows across runs and a poisoned cache can leak between
    // unrelated runs of `topo-check` with the same `rootUri`. Opt out via
    // TOPO_KEEP_JDTLS_WORKSPACE for debugging.
    namespace fs = std::filesystem;
    if (workspaceDataDir_.empty()) return;
    if (std::getenv("TOPO_KEEP_JDTLS_WORKSPACE")) return;
    std::error_code ec;
    fs::remove_all(workspaceDataDir_, ec);
    // Failure here is non-fatal — the next start() will reuse the dir.
}

bool JdtBridge::start(const std::string& rootUri) {
    return start(std::string{}, rootUri);
}

bool JdtBridge::start(const std::string& jdtPath, const std::string& rootUri) {
    namespace plat = topo::platform;

    std::string exe = jdtPath;
    if (exe.empty()) {
        // Try common names for Eclipse JDT Language Server
        exe = "jdtls" + std::string(plat::ExeSuffix);
    }

    // Pin jdtls to a project-unique workspace data directory. See
    // deriveWorkspaceDataDir() for why this defends against ctest-driven
    // cwd-basename collisions.
    workspaceDataDir_ = deriveWorkspaceDataDir(rootUri);
    std::vector<std::string> args = {"-data", workspaceDataDir_};
    if (!startProcess(exe, args, rootUri))
        return false;

    parseSemanticTokenLegend();
    return true;
}

std::optional<SymbolResult> JdtBridge::findDefinition(const std::string& qualifiedName,
                                                      const std::vector<std::string>& /*javaFiles*/) {
    if (!isAvailable()) return std::nullopt;
    return queryWorkspaceSymbol(qualifiedName);
}

std::vector<SymbolResult> JdtBridge::findReferences(const std::string& qualifiedName,
                                                    const std::vector<std::string>& /*javaFiles*/) {
    if (!isAvailable()) return {};

    auto defn = queryWorkspaceSymbol(qualifiedName);
    if (!defn) return {};

    json params = {{"textDocument", {{"uri", pathToUri(defn->file)}}},
                   {"position", {{"line", defn->line}, {"character", defn->column}}},
                   {"context", {{"includeDeclaration", true}}}};

    auto response = sendRequest("textDocument/references", params);
    if (!response || !response->is_array()) return {};

    std::vector<SymbolResult> results;
    for (const auto& loc : *response) {
        SymbolResult r;
        r.file = uriToPath(loc["uri"].get<std::string>());
        r.line = loc["range"]["start"]["line"].get<int>();
        r.column = loc["range"]["start"]["character"].get<int>();
        results.push_back(std::move(r));
    }
    return results;
}

std::optional<std::string> JdtBridge::getHoverInfo(const std::string& qualifiedName,
                                                   const std::vector<std::string>& /*javaFiles*/) {
    if (!isAvailable()) return std::nullopt;

    auto defn = queryWorkspaceSymbol(qualifiedName);
    if (!defn) return std::nullopt;

    json params = {{"textDocument", {{"uri", pathToUri(defn->file)}}},
                   {"position", {{"line", defn->line}, {"character", defn->column}}}};

    auto response = sendRequest("textDocument/hover", params);
    if (!response || response->is_null()) return std::nullopt;

    if (response->contains("contents")) {
        const auto& contents = (*response)["contents"];
        if (contents.is_string()) {
            return contents.get<std::string>();
        }
        if (contents.is_object() && contents.contains("value")) {
            return contents["value"].get<std::string>();
        }
    }
    return std::nullopt;
}

std::optional<SymbolResult> JdtBridge::findTypeDefinition(const std::string& typeName,
                                                          const std::vector<std::string>& sourceFiles,
                                                          const std::vector<std::string>& /*includeDirs*/) {
    // Prefer the live index when JDT is running.
    if (isAvailable()) {
        auto result = queryWorkspaceSymbol(typeName);
        if (result) return result;
    }

    // Fallback: scan .java source files for a matching type declaration.
    // Matches: class | interface | enum | record (Java 14+) | sealed-class
    // | non-sealed-class with any combination of the standard modifier set.
    // The trailing character class accepts `(` so `record Foo(int x) {}`
    // matches the same way as `class Foo {` / `class Foo<T> {`.
    // Intentional limits: generic *bounds* (`<T extends ...>`), annotations
    // on the declaration line, and the `permits` clause are not modelled —
    // a project using those should rely on jdtls. When jdtls is absent
    // and any of those constructs are needed, install
    // `eclipse-jdt-language-server`; the bridge will prefer the live index.
    const std::regex pattern(
        R"((?:(?:public|protected|private|abstract|final|static|sealed|non-sealed)\s+)*)"
        R"((?:class|interface|enum|record)\s+)" +
        typeName + R"([\s<{(])");

    for (const auto& filePath : sourceFiles) {
        const std::string suffix = ".java";
        if (filePath.size() < suffix.size() || filePath.substr(filePath.size() - suffix.size()) != suffix) continue;

        std::ifstream file(filePath);
        if (!file.is_open()) continue;

        std::string line;
        int lineNo = 0;
        while (std::getline(file, line)) {
            ++lineNo;
            if (std::regex_search(line, pattern)) {
                return SymbolResult{filePath, lineNo, 0};
            }
        }
    }

    return std::nullopt;
}

} // namespace topo::lsp
