#include "JavaAuditRuntime.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Platform/ToolResolution.h"
#include <cstdlib>
#include <fstream>
#include <set>
#include <stdexcept>
namespace topo::check {
namespace fs = std::filesystem;
namespace {
fs::path physical(const fs::path& path) {
    std::error_code error;
    auto result = fs::canonical(path, error);
    if (error) throw std::runtime_error("analysis input is unavailable: " + path.string());
    return result;
}
bool within(const fs::path& path, const fs::path& root) {
    auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void snapshot(JavaAuditRuntime& runtime, const fs::path& path) {
    auto resolved = physical(path);
    if (runtime.inputDigests.count(resolved.string())) return;
    auto digest = dist::sha256File(resolved.string());
    if (digest.empty()) throw std::runtime_error("cannot snapshot Java runtime: " + resolved.string());
    runtime.inputDigests[resolved.string()] = std::move(digest);
}
}
std::map<std::string, std::string> javaAuditRuntimeInventory(const fs::path& root) {
    std::map<std::string, std::string> inventory;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (inventory.size() >= 100000) throw std::runtime_error("Java runtime exceeds 100000 entries");
        if (entry.is_symlink() && entry.is_directory())
            throw std::runtime_error("Java runtime directory symlink requires a self-contained installation: " + entry.path().string());
        const auto kind = entry.is_directory() ? "directory:" : entry.is_regular_file() ? "file:" : "";
        if (!*kind) throw std::runtime_error("unsupported Java runtime entry: " + entry.path().string());
        inventory[entry.path().string()] = kind + physical(entry.path()).string();
    }
    return inventory;
}
JavaAuditRuntime resolveJavaAuditRuntime(const std::string& launcher,
                                        const nlohmann::json& request,
                                        const std::vector<std::string>& inputRoots) {
    JavaAuditRuntime runtime;
    const auto project = physical(request.at("projectRoot").get<std::string>());
    const auto launcherPath = physical(launcher);
    auto outsideProject = [&](const fs::path& path) {
        auto value = physical(path);
        if (within(value, project)) throw std::runtime_error("Java audit runtime must be installed outside the subject project: " + value.string());
        return value;
    };
    outsideProject(launcherPath);
    const auto descriptor = outsideProject(launcherPath.parent_path() / "topo-extract-java.runtime.json");
    if (fs::file_size(descriptor) > 16384) throw std::runtime_error("Java runtime descriptor exceeds 16 KiB");
    snapshot(runtime, descriptor);
    std::ifstream input(descriptor);
    auto metadata = nlohmann::json::parse(input);
    if (!metadata.is_object() || metadata.value("schemaVersion", 0) != 1 ||
        !metadata.contains("jar") || !metadata.at("jar").is_string() || metadata.at("jar").get<std::string>().empty())
        throw std::runtime_error("Java audit requires a schemaVersion 1 runtime descriptor with jar");
    fs::path jar = metadata.at("jar").get<std::string>();
    if (jar.is_relative()) jar = descriptor.parent_path() / jar;
    jar = outsideProject(jar);
    fs::path javaHome = metadata.value("javaHome", std::string());
    if (javaHome.empty()) {
        if (const char* configured = std::getenv("JAVA_HOME"); configured && *configured) javaHome = configured;
        else {
            auto java = platform::findOnPath("java");
            if (!java.empty()) javaHome = physical(java).parent_path().parent_path();
        }
    } else if (javaHome.is_relative()) javaHome = descriptor.parent_path() / javaHome;
    if (javaHome.empty()) throw std::runtime_error("Java audit requires a fixed JDK; set JAVA_HOME");
    javaHome = outsideProject(javaHome);
#ifdef _WIN32
    auto java = outsideProject(javaHome / "bin/java.exe");
#else
    auto java = outsideProject(javaHome / "bin/java");
#endif
    if (!fs::is_regular_file(javaHome / "release") || !fs::is_regular_file(javaHome / "lib/modules"))
        throw std::runtime_error("Java audit requires a modular JDK; set JAVA_HOME to its home directory");
    runtime.executable = java.string();
    runtime.arguments = {"-Xmx768m", "-XX:-UsePerfData", "-Djava.io.tmpdir=" + std::string(platform::AnalysisTemporaryDirectoryToken),
        "-Duser.home=" + std::string(platform::AnalysisTemporaryDirectoryToken), "-jar", jar.string()};
    runtime.policy.allowedExecutables = {java};
    runtime.policy.toolchainDirectories = {javaHome};
    runtime.policy.readOnlyFiles.push_back(jar);
    runtime.policy.environment = {{"LANG", "C"}, {"LC_ALL", "C"}};
    std::vector<fs::path> roots{project};
    for (const auto& root : inputRoots) {
        auto resolved = physical(root);
        if (!fs::is_directory(resolved)) throw std::runtime_error("analysis input root must be a directory: " + resolved.string());
        roots.push_back(resolved);
    }
    // Cover JDT's package-based default source roots even when sourcepath is
    // omitted; these are exactly the reviewer-granted data trees.
    runtime.policy.readOnlyDirectories = roots;
    auto grantInput = [&](const fs::path& path) {
        auto resolved = physical(path);
        bool permitted = false;
        for (const auto& root : roots) if (within(resolved, root)) permitted = true;
        if (!permitted) throw std::runtime_error("Java input is outside authorized roots; pass --analysis-input-root for: " + resolved.string());
        if (fs::is_directory(resolved)) runtime.policy.readOnlyDirectories.push_back(resolved);
        else runtime.policy.readOnlyFiles.push_back(resolved);
    };
    for (const char* key : {"files", "sourcepath", "classpath"})
        if (request.contains(key)) for (const auto& path : request.at(key)) grantInput(path.get<std::string>());
    // The declared compiler describes input identity; it never selects the JVM.
    // It may inspect the running JDK or an explicitly authorized data root.
    if (request.contains("compiler")) {
        auto compilerHome = physical(request.at("compiler").at("javaHome").get<std::string>());
        if (compilerHome != javaHome) grantInput(compilerHome);
    }
    for (const auto& path : {java, jar, javaHome / "release", javaHome / "lib/modules"}) snapshot(runtime, outsideProject(path));
    // The allowed JDK tree is an input closure: CDS archives, properties,
    // config and auxiliary jars are as significant as native libraries.
    runtime.runtimeRoot = javaHome;
    runtime.runtimeInventory = javaAuditRuntimeInventory(javaHome);
    uintmax_t bytes = 0;
    for (const auto& [name, kind] : runtime.runtimeInventory) {
        if (kind.rfind("file:", 0) != 0) continue;
        auto path = outsideProject(name);
        bytes += fs::file_size(path);
        if (bytes > 16ULL * 1024 * 1024 * 1024) throw std::runtime_error("Java runtime snapshot exceeds 16 GiB");
        snapshot(runtime, path);
        // Symlinked runtime files receive only their exact physical authority.
        runtime.policy.readOnlyFiles.push_back(path);
        auto ext = path.extension().string();
        if (ext == ".dylib" || ext == ".so" || ext == ".dll") runtime.policy.toolchainFiles.push_back(path);
    }
    return runtime;
}
} // namespace topo::check
