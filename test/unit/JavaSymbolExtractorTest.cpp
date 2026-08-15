// Unit tests for JavaSymbolExtractor — the regex-based L1 fallback.
//
// Regression focus: statement-shaped lines (`throw new X(...)`,
// `return new X(...)`, statement-start `new X(...)`, `super(...)` /
// `this(...)` delegation) used to satisfy methodRegex and emit phantom
// method symbols (e.g. `Main::AssertionError`), which broke completeness
// checks on otherwise-conforming projects. The whole-word prefix filter
// must reject those shapes while keeping real declarations — including
// package-private constructors, generic return types, and
// keyword-substring names — intact. The extractor also captures return
// and parameter types so arity-verifying consumers see real counts.

#include "analysis/extract/JavaSymbolExtractor.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace topo::check;

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

class JavaSymbolExtractorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
            ("topo_java_symbol_extractor_test_" + std::to_string(topo_getpid()));
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    std::vector<HostSymbol> extract(const std::string& source) {
        const fs::path file = dir_ / "Main.java";
        {
            std::ofstream ofs(file);
            ofs << source;
        }
        JavaSymbolExtractor extractor;
        return extractor.extractSymbols(file.string());
    }

    static bool hasSimpleName(const std::vector<HostSymbol>& syms,
                              const std::string& name) {
        return std::any_of(syms.begin(), syms.end(),
            [&](const HostSymbol& s) { return s.simpleName == name; });
    }

    static const HostSymbol* findSimpleName(const std::vector<HostSymbol>& syms,
                                            const std::string& name) {
        for (const auto& s : syms) {
            if (s.simpleName == name) return &s;
        }
        return nullptr;
    }

    fs::path dir_;
};

TEST_F(JavaSymbolExtractorTest, ThrowNewProducesNoSymbol) {
    // Mirrors the debug fixture Main.java whose `throw new AssertionError`
    // produced the phantom `Main::AssertionError` method symbol.
    auto syms = extract(
        "public class Main {\n"
        "    public static void main(String[] args) {\n"
        "        int total = 0;\n"
        "        if (total < 0) {\n"
        "            throw new AssertionError(\"unreachable\");\n"
        "        }\n"
        "    }\n"
        "}\n");

    ASSERT_EQ(syms.size(), 2u);
    const HostSymbol* cls = findSimpleName(syms, "Main");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->kind, HostSymbolKind::Class);
    const HostSymbol* mainFn = findSimpleName(syms, "main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_EQ(mainFn->kind, HostSymbolKind::StaticMethod);
    EXPECT_FALSE(hasSimpleName(syms, "AssertionError"));
}

TEST_F(JavaSymbolExtractorTest, ReturnNewProducesNoSymbol) {
    auto syms = extract(
        "public class Main {\n"
        "    public String render() {\n"
        "        return new StringBuilder(\"x\").toString();\n"
        "    }\n"
        "}\n");

    EXPECT_FALSE(hasSimpleName(syms, "StringBuilder"));
    EXPECT_TRUE(hasSimpleName(syms, "render"));
}

TEST_F(JavaSymbolExtractorTest, StatementStartNewProducesNoSymbol) {
    auto syms = extract(
        "public class Main {\n"
        "    public void spawn(Runnable r) {\n"
        "        new Thread(r).start();\n"
        "    }\n"
        "}\n");

    EXPECT_FALSE(hasSimpleName(syms, "Thread"));
    EXPECT_TRUE(hasSimpleName(syms, "spawn"));
}

TEST_F(JavaSymbolExtractorTest, ReturnCallProducesNoSymbol) {
    auto syms = extract(
        "public class Main {\n"
        "    public int twice(int x) {\n"
        "        return compute(x);\n"
        "    }\n"
        "}\n");

    EXPECT_FALSE(hasSimpleName(syms, "compute"));
    EXPECT_TRUE(hasSimpleName(syms, "twice"));
}

TEST_F(JavaSymbolExtractorTest, SuperThisDelegationProducesNoSymbol) {
    auto syms = extract(
        "public class Main {\n"
        "    public Main() {\n"
        "        this(0);\n"
        "    }\n"
        "    public Main(int v) {\n"
        "        super();\n"
        "    }\n"
        "}\n");

    EXPECT_FALSE(hasSimpleName(syms, "this"));
    EXPECT_FALSE(hasSimpleName(syms, "super"));
}

TEST_F(JavaSymbolExtractorTest, PackagePrivateConstructorStillExtracted) {
    // The whitespace-only type-group match that statement shapes abuse is
    // also what lets modifier-less constructors match — the prefix filter
    // must not break them.
    auto syms = extract(
        "public class Main {\n"
        "    Main(int v) {\n"
        "    }\n"
        "}\n");

    const HostSymbol* ctor = nullptr;
    for (const auto& s : syms) {
        if (s.kind == HostSymbolKind::Constructor) ctor = &s;
    }
    ASSERT_NE(ctor, nullptr);
    EXPECT_EQ(ctor->simpleName, "Main");
}

TEST_F(JavaSymbolExtractorTest, GenericReturnTypeStillExtracted) {
    auto syms = extract(
        "public class Main {\n"
        "    public Map<String, List<Integer>> lookup(String key) {\n"
        "        return null;\n"
        "    }\n"
        "}\n");

    const HostSymbol* m = findSimpleName(syms, "lookup");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->kind, HostSymbolKind::Method);
    // Unmapped (generic) types pass through verbatim.
    EXPECT_EQ(m->returnType, "Map<String, List<Integer>>");
}

TEST_F(JavaSymbolExtractorTest, KeywordSubstringNamesUnaffected) {
    auto syms = extract(
        "public class Main {\n"
        "    public void renewLease(long id) {\n"
        "    }\n"
        "    public int newCount() {\n"
        "        return 0;\n"
        "    }\n"
        "    public String caseLabel(int idx) {\n"
        "        return \"\";\n"
        "    }\n"
        "}\n");

    EXPECT_TRUE(hasSimpleName(syms, "renewLease"));
    EXPECT_TRUE(hasSimpleName(syms, "newCount"));
    EXPECT_TRUE(hasSimpleName(syms, "caseLabel"));
}

TEST_F(JavaSymbolExtractorTest, ParamAndReturnTypesCaptured) {
    auto syms = extract(
        "public class Main {\n"
        "    public static int compute(int a, String b) {\n"
        "        return a;\n"
        "    }\n"
        "}\n");

    const HostSymbol* m = findSimpleName(syms, "compute");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->returnType, "Int");
    ASSERT_EQ(m->paramTypes.size(), 2u);
    EXPECT_EQ(m->paramTypes[0], "Int");
    EXPECT_EQ(m->paramTypes[1], "String");
}

TEST_F(JavaSymbolExtractorTest, PrimitiveAliasMappingAndVoid) {
    auto syms = extract(
        "public class Main {\n"
        "    public void apply(boolean flag, long ticks, double rate) {\n"
        "    }\n"
        "}\n");

    const HostSymbol* m = findSimpleName(syms, "apply");
    ASSERT_NE(m, nullptr);
    // void return stays empty — consumers render it as `void`.
    EXPECT_EQ(m->returnType, "");
    ASSERT_EQ(m->paramTypes.size(), 3u);
    EXPECT_EQ(m->paramTypes[0], "Boolean");
    EXPECT_EQ(m->paramTypes[1], "Long");
    EXPECT_EQ(m->paramTypes[2], "Double");
}

TEST_F(JavaSymbolExtractorTest, UnmappedTypesPassThroughVerbatim) {
    // Types without a generated alias degrade explicitly: the raw Java
    // type text is preserved so renderers can detect and skip it instead
    // of emitting an unresolvable declaration.
    auto syms = extract(
        "public class Main {\n"
        "    public float scale(float factor) {\n"
        "        return factor;\n"
        "    }\n"
        "}\n");

    const HostSymbol* m = findSimpleName(syms, "scale");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->returnType, "float");
    ASSERT_EQ(m->paramTypes.size(), 1u);
    EXPECT_EQ(m->paramTypes[0], "float");
}
