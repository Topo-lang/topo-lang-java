// JavaExtractorFidelityTest: golden-fidelity tests for topo-extract-java.
//
// For each fixture directory under <TOPO_TEST_FIXTURES_DIR>/extractor_fidelity:
//   - <Type>.java    — Java source with its real public-type filename
//   - request.json   — stdin template for the extractor (contains "{{INPUT}}"
//                      placeholder which is substituted with the absolute path
//                      of the source at test time)
//   - expected.json  — golden TranspileModule output to compare against
//
// The test submits the request through the platform bounded-process API to
// `java -jar <TOPO_EXTRACT_JAVA_JAR>`. Input is staged with EOF before spawn,
// and stdout/stderr are captured with finite limits on every platform.
//
// Regeneration mode: set the environment variable TOPO_REGEN_FIDELITY=1
// to overwrite expected.json with the actual output on a PASS path. This is
// useful after intentional extractor changes but should never be used to
// silently mask regressions.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "topo/Platform/Process.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
static int topo_getpid() {
    return _getpid();
}
#else
#include <unistd.h>
static int topo_getpid() {
    return getpid();
}
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// -----------------------------------------------------------------------
// Paths injected by CMake
// -----------------------------------------------------------------------

#ifndef TOPO_EXTRACT_JAVA_JAR
#error "TOPO_EXTRACT_JAVA_JAR must be defined (path to topo-extract-java-*.jar)"
#endif
#ifndef JAVA_FIDELITY_FIXTURES_DIR
#error "JAVA_FIDELITY_FIXTURES_DIR must be defined (path to fixtures/extractor_fidelity)"
#endif

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

namespace {

std::string readFileToString(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

void writeStringToFile(const fs::path& p, const std::string& content) {
    std::ofstream ofs(p, std::ios::binary);
    ofs << content;
}

// Substitute "{{INPUT}}" in the request template with the absolute path to
// Input.java, writing the result to tmpRequestPath. Returns true on success.
bool buildRequestFile(const fs::path& requestTemplate,
                      const fs::path& inputJava,
                      const fs::path& tmpRequestPath) {
    std::string tmpl = readFileToString(requestTemplate);
    if (tmpl.empty()) return false;

    const std::string placeholder = "{{INPUT}}";
    std::string inputAbs = fs::absolute(inputJava).string();

#ifdef _WIN32
    // Escape backslashes for JSON string embedding on Windows.
    std::string escaped;
    escaped.reserve(inputAbs.size());
    for (char c : inputAbs) {
        if (c == '\\') escaped.append("\\\\");
        else escaped.push_back(c);
    }
    inputAbs = escaped;
#endif

    size_t pos = 0;
    while ((pos = tmpl.find(placeholder, pos)) != std::string::npos) {
        tmpl.replace(pos, placeholder.size(), inputAbs);
        pos += inputAbs.size();
    }

    writeStringToFile(tmpRequestPath, tmpl);
    return true;
}

// Resolve the `java` launcher to use. Prefers JAVA_HOME/bin/java when set
// (mirrors how the rest of the Topo toolchain locates the JDK); otherwise
// falls back to bare `java` from PATH.
std::string resolveJavaLauncher() {
    const char* javaHome = std::getenv("JAVA_HOME");
    if (javaHome && javaHome[0] != '\0') {
#ifdef _WIN32
        fs::path candidate = fs::path(javaHome) / "bin" / "java.exe";
#else
        fs::path candidate = fs::path(javaHome) / "bin" / "java";
#endif
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            return candidate.string();
        }
    }
    return "java";
}

// Use argument-vector process creation so paths containing spaces are passed
// unchanged; stderr stays available if the real extractor rejects the request.
topo::platform::BoundedProcessResult runExtractor(const fs::path& jarPath,
                                                   const fs::path& requestFile) {
    topo::platform::ProcessCaptureLimits limits;
    limits.timeoutMs = 60000;
    limits.stdinInput = readFileToString(requestFile);
    return topo::platform::runProcessCaptureBounded(
        resolveJavaLauncher(), {"-jar", jarPath.string()}, limits);
}

// Returns true if `java` appears to be usable on this system. We don't
// try to exec it; we only check that PATH is set and that the configured
// jar file exists on disk. Registered fidelity tests require both prerequisites.
bool extractorEnvironmentAvailable(std::string* reasonOut) {
    fs::path jar(TOPO_EXTRACT_JAVA_JAR);
    if (!fs::exists(jar)) {
        *reasonOut = "topo-extract-java jar not built yet: " + jar.string() +
                     " (run `cmake --build build --target topo-extract-java`)";
        return false;
    }
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv || pathEnv[0] == '\0') {
        *reasonOut = "PATH environment variable is empty";
        return false;
    }
    return true;
}

// Compare actual vs expected JSON. nlohmann::json::operator== is recursive
// and order-insensitive for objects, which matches our contract: field order
// in the extractor output should not affect golden comparison.
//
// On mismatch, this pretty-prints both sides into the failure message so
// developers can diff visually.
::testing::AssertionResult jsonsEqual(const json& actual, const json& expected) {
    if (actual == expected) {
        return ::testing::AssertionSuccess();
    }
    std::ostringstream oss;
    oss << "JSON mismatch.\n--- Expected ---\n"
        << expected.dump(2)
        << "\n--- Actual ---\n"
        << actual.dump(2);
    return ::testing::AssertionFailure() << oss.str();
}

// Source locations and binding evidence are exercised by real-JDT tests.
// Golden projection omits only those additive facts; semantic nodes, fidelity,
// modifiers, and unsupported reasons remain exact comparisons.
void stripProvenance(json& value) {
    if (value.is_array()) for (auto& item : value) stripProvenance(item);
    else if (value.is_object()) {
        for (const char* key : {"sourceSpan", "rawSource", "resolvedQualifiedName", "resolvedSignature", "bindingResolved"}) value.erase(key);
        for (auto& [key, child] : value.items()) stripProvenance(child);
    }
}

// Run a single fixture case. Returns void; test body calls ASSERT/EXPECT
// macros directly.
void runFixtureCase(const std::string& fixtureName) {
    std::string environmentError;
    ASSERT_TRUE(extractorEnvironmentAvailable(&environmentError)) << environmentError;

    fs::path fixturesRoot(JAVA_FIDELITY_FIXTURES_DIR);
    fs::path fixtureDir = fixturesRoot / fixtureName;
    fs::path inputJava;
    for (const auto& entry : fs::directory_iterator(fixtureDir)) if (entry.path().extension() == ".java") {
        ASSERT_TRUE(inputJava.empty()) << "fixture must contain exactly one Java source";
        inputJava = entry.path();
    }
    fs::path requestTemplate = fixtureDir / "request.json";
    fs::path expectedPath = fixtureDir / "expected.json";

    ASSERT_TRUE(fs::exists(inputJava)) << "missing Java source: " << inputJava;
    ASSERT_TRUE(fs::exists(requestTemplate)) << "missing request.json: " << requestTemplate;

    // Build a unique temp request file for this case.
    fs::path tmpRequest = fs::temp_directory_path() /
        ("topo_java_fidelity_" + std::to_string(topo_getpid()) + "_" +
         fixtureName + ".json");
    ASSERT_TRUE(buildRequestFile(requestTemplate, inputJava, tmpRequest))
        << "failed to build request file";

    // Run the extractor.
    const auto result = runExtractor(TOPO_EXTRACT_JAVA_JAR, tmpRequest);
    const auto& stdoutOutput = result.stdoutOutput;

    // Clean up temp request file regardless of outcome.
    std::error_code ec;
    fs::remove(tmpRequest, ec);

    ASSERT_EQ(result.failure, topo::platform::ProcessCaptureFailure::None)
        << "extractor process failed for fixture " << fixtureName << ": " << result.diagnostic
        << "\nstderr:\n" << result.stderrOutput;
    ASSERT_EQ(result.exitCode, 0) << "extractor invocation failed for fixture " << fixtureName
        << "; stderr was:\n" << result.stderrOutput << "\nstdout was:\n" << stdoutOutput;
    ASSERT_FALSE(stdoutOutput.empty())
        << "extractor produced empty stdout for fixture " << fixtureName;

    // Parse actual output.
    json actual;
    try {
        actual = json::parse(stdoutOutput);
    } catch (const json::exception& e) {
        FAIL() << "failed to parse extractor stdout as JSON: " << e.what()
               << "\nraw output:\n" << stdoutOutput;
    }

    ASSERT_TRUE(actual.value("analysisComplete", false)) << actual.value("diagnostics", json::array()).dump(2);
    ASSERT_EQ(actual.value("sourceLevel", std::string()), "25");
    ASSERT_TRUE(actual.at("diagnostics").empty());
    ASSERT_FALSE(actual.value("runDegraded", true));
    for (const char* key : {"schemaVersion", "sourceLevel", "analysisComplete", "diagnostics", "facts"}) actual.erase(key);
    stripProvenance(actual);

    // Regen mode: overwrite expected.json with the actual output and pass.
    const char* regen = std::getenv("TOPO_REGEN_FIDELITY");
    if (regen && std::string(regen) == "1") {
        writeStringToFile(expectedPath, actual.dump(2) + "\n");
        std::cerr << "  [REGEN] wrote " << expectedPath << "\n";
        SUCCEED();
        return;
    }

    ASSERT_TRUE(fs::exists(expectedPath))
        << "missing expected.json for fixture " << fixtureName
        << "; run with TOPO_REGEN_FIDELITY=1 to generate";

    // Parse expected golden.
    json expected;
    try {
        expected = json::parse(readFileToString(expectedPath));
    } catch (const json::exception& e) {
        FAIL() << "failed to parse expected.json: " << e.what();
    }

    EXPECT_TRUE(jsonsEqual(actual, expected));
}

} // namespace

// -----------------------------------------------------------------------
// Test cases: one per fixture directory
// -----------------------------------------------------------------------

TEST(JavaExtractorFidelity, BasicMethod) {
    runFixtureCase("01_basic_method");
}

TEST(JavaExtractorFidelity, InstanceMethod) {
    runFixtureCase("02_instance_method");
}

TEST(JavaExtractorFidelity, NestedClass) {
    runFixtureCase("03_nested_class");
}

TEST(JavaExtractorFidelity, GenericMethod) {
    runFixtureCase("04_generic_method");
}

TEST(JavaExtractorFidelity, Annotation) {
    runFixtureCase("05_annotation");
}

TEST(JavaExtractorFidelity, IfElse) {
    runFixtureCase("06_if_else");
}

TEST(JavaExtractorFidelity, PackageDeclaration) {
    runFixtureCase("07_package_declaration");
}

TEST(JavaExtractorFidelity, InterfaceDefaultMethod) {
    runFixtureCase("08_interface_default_method");
}

TEST(JavaExtractorFidelity, TryCatch) {
    runFixtureCase("09_try_catch");
}

TEST(JavaExtractorFidelity, WhileLoop) {
    runFixtureCase("10_while_loop");
}

TEST(JavaExtractorFidelity, Varargs) {
    runFixtureCase("11_varargs");
}

TEST(JavaExtractorFidelity, EnumMethod) {
    runFixtureCase("12_enum_method");
}

// Generic class `Box<T>` plus a method with a bounded type parameter
// `<U extends Comparable<U>>`. Scope is type-parameter names only, so the
// bound is dropped: the bare name is still emitted, the method records the
// downgrade in `unsupported` and its fidelity becomes "inferred".
TEST(JavaExtractorFidelity, BoundedGenerics) {
    runFixtureCase("13_bounded_generics");
}

// Intersection bound `class Sortable<T extends A & B>`: the multi-bound
// wire shape graduates from `bound: TypeNode` to `bounds: [TypeNode]`
// when JDT's `TypeParameter.typeBounds()` returns more than one entry.
// All bounds are Type instances → both lift through `AstConverter.convertType`.
TEST(JavaExtractorFidelity, IntersectionBound) {
    runFixtureCase("14_intersection_bound");
}
