// JavaExtractorFidelityTest: golden-fidelity tests for topo-extract-java.
//
// For each fixture directory under <TOPO_TEST_FIXTURES_DIR>/extractor_fidelity:
//   - Input.java     — Java source file to extract from
//   - request.json   — stdin template for the extractor (contains "{{INPUT}}"
//                      placeholder which is substituted with the absolute path
//                      of Input.java at test time)
//   - expected.json  — golden TranspileModule output to compare against
//
// The test writes the substituted request.json to a temporary file, invokes
// `java -jar <TOPO_EXTRACT_JAVA_JAR>` with stdin redirected from that file,
// captures stdout, parses it as JSON, and compares against expected.json.
//
// Unlike a live PipedProcess approach, stdin is redirected from a real file
// (via popen with shell redirection), which guarantees EOF is sent to the
// extractor and avoids the deadlock that would occur if stdin were written
// and left open by the parent.
//
// Regeneration mode: set the environment variable TOPO_REGEN_FIDELITY=1
// to overwrite expected.json with the actual output on a PASS path. This is
// useful after intentional extractor changes but should never be used to
// silently mask regressions.

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define TOPO_POPEN _popen
#define TOPO_PCLOSE _pclose
static int topo_getpid() {
    return _getpid();
}
#else
#include <unistd.h>
#define TOPO_POPEN popen
#define TOPO_PCLOSE pclose
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

// Invoke `java -jar <jar>` reading stdin from requestFile and capture stdout.
// Returns the captured stdout; on failure (java not found, nonzero exit,
// empty output), returns empty string and sets *okOut to false.
std::string runExtractor(const fs::path& jarPath,
                         const fs::path& requestFile,
                         bool* okOut) {
    *okOut = false;

    std::string java = resolveJavaLauncher();

    // Build a shell command with redirected stdin. We use popen so stdin
    // redirection is handled by the shell — this avoids the fork/pipe
    // deadlock that would otherwise arise from leaving stdin open.
    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "\"" << java << "\" -jar \"" << jarPath.string()
        << "\" < \"" << requestFile.string() << "\" 2> NUL";
#else
    cmd << "'" << java << "' -jar '" << jarPath.string()
        << "' < '" << requestFile.string() << "' 2>/dev/null";
#endif

    FILE* fp = TOPO_POPEN(cmd.str().c_str(), "r");
    if (!fp) {
        return {};
    }

    std::string output;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        output.append(buf, n);
    }

    int rc = TOPO_PCLOSE(fp);
    if (rc != 0) {
        // Nonzero exit. The output may still be valid, but more likely
        // the extractor failed (e.g. java not on PATH, jar missing,
        // JDT OOM). Signal failure to the caller.
        return output;
    }

    *okOut = true;
    return output;
}

// Returns true if `java` appears to be usable on this system. We don't
// try to exec it; we only check that PATH is set and that the configured
// jar file exists on disk. The test body will SKIP if either is missing.
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

// Run a single fixture case. Returns void; test body calls ASSERT/EXPECT
// macros directly.
void runFixtureCase(const std::string& fixtureName) {
    std::string skipReason;
    if (!extractorEnvironmentAvailable(&skipReason)) {
        GTEST_SKIP() << skipReason;
        return;
    }

    fs::path fixturesRoot(JAVA_FIDELITY_FIXTURES_DIR);
    fs::path fixtureDir = fixturesRoot / fixtureName;
    fs::path inputJava = fixtureDir / "Input.java";
    fs::path requestTemplate = fixtureDir / "request.json";
    fs::path expectedPath = fixtureDir / "expected.json";

    ASSERT_TRUE(fs::exists(inputJava)) << "missing Input.java: " << inputJava;
    ASSERT_TRUE(fs::exists(requestTemplate)) << "missing request.json: " << requestTemplate;

    // Build a unique temp request file for this case.
    fs::path tmpRequest = fs::temp_directory_path() /
        ("topo_java_fidelity_" + std::to_string(topo_getpid()) + "_" +
         fixtureName + ".json");
    ASSERT_TRUE(buildRequestFile(requestTemplate, inputJava, tmpRequest))
        << "failed to build request file";

    // Run the extractor.
    bool ok = false;
    std::string stdoutOutput = runExtractor(TOPO_EXTRACT_JAVA_JAR, tmpRequest, &ok);

    // Clean up temp request file regardless of outcome.
    std::error_code ec;
    fs::remove(tmpRequest, ec);

    ASSERT_TRUE(ok) << "extractor invocation failed for fixture " << fixtureName
                    << "; stdout was:\n" << stdoutOutput;
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
