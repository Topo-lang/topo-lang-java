// Real-javac roundtrip tests for JavaEmitter.
//
// The sibling `JavaEmitterStdlibTest` builds in-memory TranspileModule
// fixtures, runs `JavaEmitter::emit(mod)`, and asserts on the output via
// `std::string::find`. That suite is fast but cannot catch failure modes
// where the emitter produces text the emitter calls "Java" but `javac`
// rejects — wrong import set, missing class wrapper, malformed generic
// syntax, throws clause referencing an unimported exception, etc.
//
// This file wires the emitter output through real `javac` so the
// roundtrip "TranspileModule → JavaEmitter::emit → javac compile" is
// asserted to *succeed*, not just to contain the right substrings.
// When `javac` is not on PATH the tests SKIP with a stated reason per
// CLAUDE.md skip semantics.
//
// Issue: javaemitterstdlibtest-uses-synth-not-real-javac.

#include "JavaEmitter.h"
#include "topo/Platform/Process.h"
#include "topo/Stdlib/Types.h"
#include "topo/Transpile/TranspileModel.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace topo;
using namespace topo::transpile;
namespace fs = std::filesystem;

namespace {

/// Detect a usable `javac` on PATH. Per CLAUDE.md skip semantics the
/// detection result feeds a GTest GTEST_SKIP() with a printed reason —
/// never a silent pass, never a hard-fail.
bool javacAvailable() {
    auto r = platform::runProcessCapture("javac", {"-version"}, /*verbose=*/false);
    return r.exitCode == 0;
}

/// Write `content` to <dir>/<filename> and return the absolute path.
std::string writeFile(const fs::path& dir, const std::string& filename,
                      const std::string& content) {
    fs::path p = dir / filename;
    std::ofstream out(p);
    out << content;
    out.close();
    return p.string();
}

/// Mkdtemp wrapper returning the directory path. The test fixture
/// removes it at teardown.
fs::path makeTempDir() {
    std::string tpl = (fs::temp_directory_path() / "topo-java-roundtrip-XXXXXX").string();
    std::vector<char> buf(tpl.begin(), tpl.end());
    buf.push_back('\0');
    char* p = mkdtemp(buf.data());
    if (!p) throw std::runtime_error("mkdtemp failed");
    return fs::path(p);
}

class JavaEmitterRealJavacFixture : public ::testing::Test {
protected:
    fs::path tmp;

    void SetUp() override {
        if (!javacAvailable()) {
            GTEST_SKIP() << "SKIPPED: javac not found on PATH. "
                            "Real-path Java emitter roundtrip needs a JDK installed; "
                            "set PATH to point at a JDK's bin/ to run these tests.";
        }
        tmp = makeTempDir();
    }

    void TearDown() override {
        std::error_code ec;
        if (!tmp.empty()) fs::remove_all(tmp, ec);
    }

    /// Save `code` to <tmp>/<filename> and run `javac` on it. Returns
    /// true on successful compile; on failure populates `error` with
    /// the javac stderr so the gtest message reports the actual diag.
    bool tryCompile(const std::string& filename, const std::string& code,
                    std::string& error) {
        std::string srcPath = writeFile(tmp, filename, code);
        auto r = platform::runProcessCapture(
            "javac", {"-d", tmp.string(), srcPath},
            /*verbose=*/false);
        if (r.exitCode != 0) {
            error = "javac exit=" + std::to_string(r.exitCode)
                  + "\nstderr:\n" + r.stderrOutput
                  + "\nstdout:\n" + r.stdoutOutput
                  + "\nsource:\n" + code;
            return false;
        }
        return true;
    }
};

/// Build a bare TypeNode naming a single (reference) type.
TypeNode namedType(const std::string& name) {
    TypeNode t;
    t.nameParts = {name};
    return t;
}

} // namespace

// ── Roundtrip 1: a minimal class compiles ──────────────────────────

TEST_F(JavaEmitterRealJavacFixture, EmptyClassCompilesUnderJavac) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Plain";
    mod.types.push_back(std::move(ty));

    JavaEmitter emitter;
    auto result = emitter.emit(mod);

    // Sanity: the existing string-search assertions should hold first.
    ASSERT_NE(result.code.find("class Plain"), std::string::npos)
        << "emitter output missing class header:\n" << result.code;

    // Roundtrip: emit the file and feed it to javac. A regression that
    // produces a syntactically-Java file javac rejects would fail here
    // — exactly the dim-14 trap the audit flagged.
    std::string err;
    EXPECT_TRUE(tryCompile("Plain.java", result.code, err)) << err;
}

// ── Roundtrip 2: an int free function compiles ─────────────────────

TEST_F(JavaEmitterRealJavacFixture, VoidFunctionInNamespaceCompilesUnderJavac) {
    // Free functions inside a namespace get wrapped in a class named
    // after the namespace's last segment. Functions with empty bodies
    // and void return are valid Java (the synthesised test only checks
    // the generic clause sits between `static` and the return type;
    // this round-trip pins that javac accepts the result).
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "util::doNothing";
    fn.returnType = namedType("void");
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    auto result = emitter.emit(mod);

    std::string err;
    EXPECT_TRUE(tryCompile("Util.java", result.code, err)) << err;
}

// ── Roundtrip 2.5: int free function with empty body compiles ──────
//
// Regression for issue `javaemitter-int-return-with-empty-body-rejected-by-
// javac`. Before the empty-body zero-value-stub fix, `int compute() {}` was
// emitted verbatim and javac rejected the output with "missing return
// statement". The emitter now synthesises `return 0;` for any non-void
// return whose body is empty; this case must stay GREEN.

TEST_F(JavaEmitterRealJavacFixture, IntFreeFunctionWithEmptyBodyCompilesUnderJavac) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "util::compute";
    fn.returnType = namedType("int");
    // fn.body intentionally empty — exercises the stub-synthesis path.
    mod.functions.push_back(std::move(fn));

    JavaEmitter emitter;
    auto result = emitter.emit(mod);

    // The emitter should have synthesised both the FIXME marker (visible
    // to a human reviewer) and a `return 0;` body (so javac stays happy).
    ASSERT_NE(result.code.find("// FIXME: emitter inserted stub"),
              std::string::npos)
        << "expected stub-synthesis marker in:\n" << result.code;
    ASSERT_NE(result.code.find("return 0;"), std::string::npos)
        << "expected `return 0;` zero-value stub in:\n" << result.code;

    std::string err;
    EXPECT_TRUE(tryCompile("Util.java", result.code, err)) << err;
}

// ── Roundtrip 3: generic class with bound compiles ─────────────────

TEST_F(JavaEmitterRealJavacFixture, GenericClassWithBoundCompilesUnderJavac) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    TemplateParamDecl tp;
    tp.kind = TemplateParamDecl::TypeParam;
    tp.name = "T";
    tp.constraintType = namedType("Comparable");
    ty.templateParams.push_back(std::move(tp));
    mod.types.push_back(std::move(ty));

    JavaEmitter emitter;
    auto result = emitter.emit(mod);

    ASSERT_NE(result.code.find("Box<T extends Comparable>"), std::string::npos)
        << "missing bound emission:\n" << result.code;

    std::string err;
    EXPECT_TRUE(tryCompile("Box.java", result.code, err)) << err;
}

// ── Roundtrip 4: array<int, N> field compiles ──────────────────────

TEST_F(JavaEmitterRealJavacFixture, ArrayFieldRoundtripsThroughJavac) {
    // Stdlib bridging: array<int, 4> -> int[] with an N=4 comment. Mirrors
    // the synthetic JavaEmitterStdlib ArrayBoolEmitsLongArrayBracketWithN
    // shape but compiles the result.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Holder";
    TranspileField f;
    TypeNode arr;
    arr.nameParts = {stdlib::keywordOf(stdlib::TypeId::Array)};
    arr.stdlibId = stdlib::TypeId::Array;
    TypeNode innerType;
    innerType.nameParts = {stdlib::keywordOf(stdlib::TypeId::I32)};
    innerType.stdlibId = stdlib::TypeId::I32;
    arr.templateArgs.push_back(std::move(innerType));
    // Non-type N=4 carried on a placeholder TypeNode (TypeNode::nonTypeValue).
    TypeNode nArg;
    nArg.nonTypeValue = 4;
    arr.templateArgs.push_back(std::move(nArg));
    f.type = std::move(arr);
    f.name = "data";
    ty.fields.push_back(std::move(f));
    mod.types.push_back(std::move(ty));

    JavaEmitter emitter;
    auto result = emitter.emit(mod);

    std::string err;
    EXPECT_TRUE(tryCompile("Holder.java", result.code, err)) << err;
}
