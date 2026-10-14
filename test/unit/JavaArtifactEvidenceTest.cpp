#include "JavaDriver.h"
#include "JavaNativeEvidence.h"
#include "JavaAnalysisProvider.h"
#include "topo/Check/Audit.h"
#include "topo/Platform/Process.h"
#include "topo/Platform/ToolResolution.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace ev = topo::build::evidence;
using namespace topo::jvm;
using namespace topo::check;
namespace {
void createNativeEvidenceLink(const fs::path& target, const fs::path& link, bool directory, std::error_code& error) {
#ifdef _WIN32
    // Match the native reparse-point fixtures in ArtifactEvidenceTest.
    const DWORD kind = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), kind | 0x2)) { error.clear(); return; }
    auto code = GetLastError();
    if (code == ERROR_INVALID_PARAMETER || code == ERROR_PRIVILEGE_NOT_HELD) {
        if (CreateSymbolicLinkW(link.c_str(), target.c_str(), kind)) { error.clear(); return; }
        code = GetLastError();
    }
    error = std::error_code(static_cast<int>(code), std::system_category());
#else
    if (directory) fs::create_directory_symlink(target, link, error);
    else fs::create_symlink(target, link, error);
#endif
}
}
class JavaArtifactEvidenceTest : public ::testing::Test {
protected:
    fs::path root;
    void SetUp() override {
        root = fs::temp_directory_path() / ("topo-java-artifact-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root); root = fs::canonical(root);
    }
    void TearDown() override { std::error_code ec; fs::remove_all(root, ec); }
    fs::path write(const std::string& name, const std::string& text) {
        auto path = root / name; fs::create_directories(path.parent_path()); std::ofstream(path) << text; return path;
    }
    void rejectedRedirect(const std::string& slot, bool intermediate = false) {
        auto approved = root / "approved";
        auto outside = root / "outside";
        auto project = root / "subject";
        fs::create_directories(project);
        const auto safe = write("approved/safe.dat", "authorized evidence bytes");
        const auto relative = intermediate ? "nested/artifact.dat" : "artifact.dat";
        const auto artifact = write(std::string("approved/") + relative, "original artifact bytes");
        const auto secret = write("outside/artifact.dat", "synthetic external private bytes");
        ev::FileEntry safeEntry, entry; std::string error;
        ASSERT_TRUE(ev::snapshotFile(approved, "native", "safe.dat", safeEntry, error)) << error;
        ASSERT_TRUE(ev::snapshotFile(approved, "native", relative, entry, error)) << error;
        ev::Record record; record.action.kind = "native-link"; record.action.version = "1";
        record.action.toolIdentity = "synthetic-observation"; record.outputs = {safeEntry};
        if (slot == "input") record.action.inputs.push_back(entry);
        else if (slot == "output") record.outputs.push_back(entry);
        else record.metadata.push_back(entry);
        ev::sealRecord(record);
        ev::Manifest manifest; manifest.nodes = {record}; manifest.roots = {record.id};
        const auto path = root / "native-evidence.json";
        ASSERT_TRUE(ev::writeManifest(path, manifest, error)) << error;
        ASSERT_TRUE(ev::verifyManifest(manifest, {{"native", approved}}).integrity);
        std::error_code linkError;
        if (intermediate) {
            fs::remove_all(artifact.parent_path());
            createNativeEvidenceLink(outside, artifact.parent_path(), true, linkError);
        } else {
            fs::remove(artifact);
            createNativeEvidenceLink(fs::path("../outside/artifact.dat"), artifact, false, linkError);
        }
        ASSERT_FALSE(linkError) << "native link fixture creation failed: " << linkError.message();
        ev::Manifest parsed;
        ASSERT_TRUE(ev::readManifest(path, parsed, error)) << error;
        const auto checked = ev::verifyManifest(parsed, {{"native", approved}});
        ASSERT_FALSE(checked.integrity);
        auto provider = createJavaAnalysisProvider();
        std::string isolation;
        provider->configureAuditExecution({}, isolation);
        provider->configureAnalysis(project.string(), "");
        provider->configureNativeEvidence(path.string(), {{"native", approved.string()}});
        auto facts = provider->auditFacts({});
        EXPECT_TRUE(facts.nativeBindings.empty());
        EXPECT_FALSE(facts.complete);
        EXPECT_EQ(std::find(facts.inputs.begin(), facts.inputs.end(), secret.string()), facts.inputs.end());
        EXPECT_EQ(std::find(facts.inputs.begin(), facts.inputs.end(), safe.string()), facts.inputs.end());
        EXPECT_FALSE(facts.inputDigests.count(secret.string()));
        EXPECT_TRUE(std::any_of(facts.unresolved.begin(), facts.unresolved.end(), [](const auto& text) {
            return text.find("symlink") != std::string::npos || text.find("reparse") != std::string::npos;
        }));
        auto report = makeAuditReport(project.string(), facts, {}, {}, {}, {});
        const bool exposed = std::any_of(report["inputs"].begin(), report["inputs"].end(), [&](const auto& input) {
            return input["resolvedPath"] == secret.string();
        });
        EXPECT_FALSE(exposed) << "Actual parent report: " << report.dump(2);
    }
    ev::Manifest read(const std::string& path) {
        ev::Manifest manifest; std::string error; EXPECT_TRUE(ev::readManifest(path, manifest, error)) << error; return manifest;
    }
};

TEST_F(JavaArtifactEvidenceTest, ActualJavacNestedClassesResourcesAndJarFormContentBoundStages) {
    auto javac = topo::platform::findSpawnableOnPath(resolveJavaTool("javac", ""));
    ASSERT_FALSE(javac.empty()) << "A real JDK is required";
    auto jdk = fs::canonical(javac).parent_path().parent_path();
    write("src/App.java", "class App { static class Nested {} Runnable r=new Runnable(){public void run(){}}; }\n");
    JavaEvidenceOptions options;
    options.roots = {{"project", root}, {"jdk", jdk}};
    auto compilation = compileJava({(root / "src").string()}, "", (root / "classes").string(), "25",
        topo::BuildMode::Dev, jdk.string(), false, &options);
    ASSERT_EQ(compilation.exitCode, 0);
    ASSERT_TRUE(compilation.evidenceDiagnostics.empty());
    auto compileManifest = read(compilation.evidencePath);
    ASSERT_EQ(compileManifest.nodes.size(), 1u);
    EXPECT_GE(compileManifest.nodes[0].outputs.size(), 3u);
    EXPECT_FALSE(compileManifest.nodes[0].complete);
    EXPECT_TRUE(ev::verifyManifest(compileManifest, options.roots).integrity);
    write("classes/native/resource.dylib", "native resource bytes");
    options.parents = {compileManifest};
    auto packaged = packageJar((root / "classes").string(), (root / "app.jar").string(), "", jdk.string(), false, &options);
    ASSERT_EQ(packaged.exitCode, 0);
    ASSERT_TRUE(packaged.evidenceDiagnostics.empty());
    auto jarManifest = read(packaged.evidencePath);
    ASSERT_EQ(jarManifest.nodes.size(), 2u);
    auto jarNode = std::find_if(jarManifest.nodes.begin(), jarManifest.nodes.end(), [&](const auto& node) {
        return node.id == jarManifest.roots.at(0);
    });
    ASSERT_NE(jarNode, jarManifest.nodes.end());
    ASSERT_EQ(jarNode->parents.size(), 1u);
    EXPECT_EQ(jarNode->parents[0], compileManifest.roots[0]);
    EXPECT_TRUE(ev::verifyManifest(jarManifest, options.roots).integrity);
    EXPECT_FALSE(ev::verifyManifest(jarManifest, options.roots).buildEvidence);
    std::ofstream(root / "app.jar", std::ios::binary | std::ios::app) << "post-package change";
    EXPECT_FALSE(ev::verifyManifest(jarManifest, options.roots).integrity);
    write("classes/App.class", "class replacement");
    auto replaced = packageJar((root / "classes").string(), (root / "replaced.jar").string(), "", jdk.string(), false, &options);
    ASSERT_EQ(replaced.exitCode, 0);
    EXPECT_FALSE(replaced.evidenceDiagnostics.empty());
    auto replacementManifest = read(replaced.evidencePath);
    auto replacementNode = std::find_if(replacementManifest.nodes.begin(), replacementManifest.nodes.end(), [&](const auto& node) {
        return node.id == replacementManifest.roots.at(0);
    });
    ASSERT_NE(replacementNode, replacementManifest.nodes.end());
    EXPECT_TRUE(replacementNode->parents.empty());
}

TEST_F(JavaArtifactEvidenceTest, DirectCAbiJoinUsesActualDylibAndNeverExecutesNativeInitializer) {
#if defined(__APPLE__) && defined(__aarch64__)
    auto sentinel = root / "native-initializer-ran";
    auto source = write("native.cpp", "#include <stdio.h>\nextern \"C\" int topo_add(int a,int b){return a+b;}\n"
        "__attribute__((constructor)) static void init(){FILE* f=fopen(\"" + sentinel.string() + "\",\"w\");if(f)fclose(f);}\n");
    auto library = root / "libnative.dylib";
    auto compiler = topo::platform::findSpawnableOnPath("clang++");
    ASSERT_FALSE(compiler.empty());
    ASSERT_EQ(topo::platform::runProcess(compiler, {"-dynamiclib", source.string(), "-o", library.string()}, false).exitCode, 0);
    ev::Record record;
    record.action.kind = "native-link"; record.action.version = "1";
    record.action.toolIdentity = "test-compiler-observation";
    record.action.parameters["native"] = {{"abi", "c"}, {"targetTriple", "arm64-apple-darwin"}, {"binaryKind", "dylib"},
        {"exports", nlohmann::json::array({{{"name", "topo_add"}, {"signature", "int (int, int)"},
            {"returnType", "int"}, {"parameterTypes", {"int", "int"}}}})}};
    std::string error; ev::FileEntry output;
    ASSERT_TRUE(ev::snapshotFile(root, "project", "libnative.dylib", output, error)) << error;
    record.outputs.push_back(output); ev::sealRecord(record);
    ev::Manifest manifest; manifest.nodes = {record}; manifest.roots = {record.id};
    AuditNativeBinding binding;
    binding.library = library.string(); binding.symbol = "topo_add"; binding.descriptor = "JAVA_INT(JAVA_INT,JAVA_INT)";
    binding.resolved = true; binding.lookup = "library-path";
    ev::RootMap roots{{"project", root}};
    auto matched = verifyJavaNativeBinding(binding, manifest, roots);
    EXPECT_TRUE(matched.staticBinding) << nlohmann::json(matched.diagnostics).dump(); EXPECT_FALSE(matched.runtimeMapping);
    EXPECT_EQ(matched.provenanceStatus, "static-binding-only");
    EXPECT_FALSE(fs::exists(sentinel));
    auto libraryOnly = binding; libraryOnly.symbol.clear(); libraryOnly.descriptor.clear();
    EXPECT_EQ(verifyJavaNativeBinding(libraryOnly, manifest, roots).provenanceStatus, "library-bytes-only");
    auto wrongAbi = binding; wrongAbi.descriptor = "JAVA_LONG(JAVA_INT,JAVA_INT)";
    EXPECT_EQ(verifyJavaNativeBinding(wrongAbi, manifest, roots).provenanceStatus, "mismatch");
    auto wrongSymbol = binding; wrongSymbol.symbol = "hidden";
    EXPECT_EQ(verifyJavaNativeBinding(wrongSymbol, manifest, roots).provenanceStatus, "mismatch");
    auto alias = root / "same-name/libnative.dylib"; fs::create_directories(alias.parent_path()); fs::copy_file(library, alias);
    auto impostor = binding; impostor.library = alias.string();
    EXPECT_FALSE(verifyJavaNativeBinding(impostor, manifest, roots).staticBinding);
    std::ofstream(library, std::ios::binary | std::ios::app) << "post-link-signing-or-replacement";
    EXPECT_EQ(verifyJavaNativeBinding(binding, manifest, roots).provenanceStatus, "mismatch");
    EXPECT_EQ(verifyJavaNativeBinding(libraryOnly, manifest, roots).provenanceStatus, "mismatch");
    EXPECT_FALSE(fs::exists(sentinel));
#else
    GTEST_SKIP() << "Initial native evidence inspection targets macOS ARM64 direct C ABI";
#endif
}

TEST_F(JavaArtifactEvidenceTest, ProviderRejectsEscapingNativeActionInputWithoutFfmBindings) {
    rejectedRedirect("input");
}
TEST_F(JavaArtifactEvidenceTest, ProviderRejectsEscapingNativeOutputWithoutFfmBindings) {
    rejectedRedirect("output");
}
TEST_F(JavaArtifactEvidenceTest, ProviderRejectsEscapingNativeMetadataWithoutFfmBindings) {
    rejectedRedirect("metadata");
}
TEST_F(JavaArtifactEvidenceTest, ProviderRejectsIntermediateNativeDirectorySymlink) {
    rejectedRedirect("output", true);
}
TEST_F(JavaArtifactEvidenceTest, ExplicitNativeEvidenceRootPreservesSupportedFileIdentities) {
    auto approved = root / "approved";
    auto project = root / "subject"; fs::create_directories(project);
    const auto original = write("approved/artifact.dat", "native evidence bytes");
    std::error_code linkError;
    createNativeEvidenceLink("artifact.dat", approved / "alias.dat", false, linkError);
    ASSERT_FALSE(linkError) << linkError.message();
    ev::FileEntry direct, alias; std::string error;
    ASSERT_TRUE(ev::snapshotFile(approved, "native", "artifact.dat", direct, error)) << error;
#ifdef _WIN32
    EXPECT_FALSE(ev::snapshotFile(approved, "native", "alias.dat", alias, error));
    EXPECT_NE(error.find("reparse-point evidence identity is unsupported"), std::string::npos);
    alias = direct;
#else
    ASSERT_TRUE(ev::snapshotFile(approved, "native", "alias.dat", alias, error)) << error;
#endif
    ev::Record record; record.action.kind = "native-link"; record.action.version = "1";
    record.action.toolIdentity = "synthetic-observation";
    record.action.inputs = {direct}; record.outputs = {alias}; record.metadata = {direct};
    // Content integrity remains usable even when build coverage is incomplete.
    record.complete = false; ev::sealRecord(record);
    ev::Manifest manifest; manifest.nodes = {record}; manifest.roots = {record.id};
    const auto path = root / "native-evidence.json";
    ASSERT_TRUE(ev::writeManifest(path, manifest, error)) << error;
    auto provider = createJavaAnalysisProvider(); std::string isolation;
    provider->configureAuditExecution({}, isolation);
    provider->configureAnalysis(project.string(), "");
    provider->configureNativeEvidence(path.string(), {{"native", approved.string()}});
    const auto facts = provider->auditFacts({});
    EXPECT_NE(std::find(facts.inputs.begin(), facts.inputs.end(), original.string()), facts.inputs.end());
    EXPECT_EQ(facts.inputDigests.at(original.string()), direct.sha256);
    EXPECT_FALSE(std::any_of(facts.unresolved.begin(), facts.unresolved.end(), [](const auto& text) {
        return text.find("Native evidence") != std::string::npos || text.find("symlink") != std::string::npos;
    }));
}
