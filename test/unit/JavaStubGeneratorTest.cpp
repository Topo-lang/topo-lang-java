// Unit tests for JavaStubGenerator.

#include "analysis/stub/JavaStubGenerator.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

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
using namespace topo::check;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class JavaStubGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = fs::temp_directory_path() / ("topo_java_stub_test_" + std::to_string(topo_getpid()));
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    std::string writeTempFile(const std::string& name, const std::string& content) {
        auto path = tempDir_ / name;
        std::ofstream ofs(path);
        ofs << content;
        return path.string();
    }

    std::string readFileContent(const std::string& path) {
        std::ifstream ifs(path);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    fs::path tempDir_;
};

// ===========================================================================
// findMethodBodyStart tests
// ===========================================================================

// 1. FindSimpleMethod
TEST_F(JavaStubGeneratorTest, FindSimpleMethod) {
    std::string src = "public void run() { System.out.println(); }";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '{');
}

// 2. FindMethodWithThrows
TEST_F(JavaStubGeneratorTest, FindMethodWithThrows) {
    std::string src = "public void run() throws Exception { doStuff(); }";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '{');
}

// 3. FindMethodAmongMultiple
TEST_F(JavaStubGeneratorTest, FindMethodAmongMultiple) {
    std::string src =
        "public void alpha() { }\n"
        "public int beta() { return 1; }\n"
        "public void gamma() { }\n";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "beta");
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '{');
    // Verify it is the beta body, not alpha or gamma
    std::string before = src.substr(0, pos);
    EXPECT_NE(before.find("beta"), std::string::npos);
}

// 4. DoesNotMatchSubstring
TEST_F(JavaStubGeneratorTest, DoesNotMatchSubstring) {
    std::string src = "public void runTask() { }";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    EXPECT_EQ(pos, std::string::npos);
}

// 5. DoesNotMatchAbstract
TEST_F(JavaStubGeneratorTest, DoesNotMatchAbstract) {
    std::string src = "abstract void run();";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    EXPECT_EQ(pos, std::string::npos);
}

// 6. FindMethodNotFound
TEST_F(JavaStubGeneratorTest, FindMethodNotFound) {
    std::string src = "public void execute() { }";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    EXPECT_EQ(pos, std::string::npos);
}

// 7. SkipsStringContent
TEST_F(JavaStubGeneratorTest, SkipsStringContent) {
    std::string src =
        "public void other() {\n"
        "    String s = \"run(\";\n"
        "}\n"
        "public void run() { actual(); }";
    size_t pos = JavaStubGenerator::findMethodBodyStart(src, "run");
    ASSERT_NE(pos, std::string::npos);
    // The found position should be after the string literal occurrence
    std::string after = src.substr(pos);
    EXPECT_TRUE(after.find("actual()") != std::string::npos);
}

// ===========================================================================
// findMatchingBrace tests
// ===========================================================================

// 8. MatchingBraceSimple
TEST_F(JavaStubGeneratorTest, MatchingBraceSimple) {
    std::string src = "{ return 42; }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 9. MatchingBraceNested
TEST_F(JavaStubGeneratorTest, MatchingBraceNested) {
    std::string src = "{ if (x) { y(); } }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 10. MatchingBraceWithStrings
TEST_F(JavaStubGeneratorTest, MatchingBraceWithStrings) {
    std::string src = "{ String s = \"}\"; }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 11. MatchingBraceWithComments
TEST_F(JavaStubGeneratorTest, MatchingBraceWithComments) {
    std::string src = "{ // }\n return; }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 12. MatchingBraceBlockComment
TEST_F(JavaStubGeneratorTest, MatchingBraceBlockComment) {
    std::string src = "{ /* } */ return; }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 13. MatchingBraceTextBlock
TEST_F(JavaStubGeneratorTest, MatchingBraceTextBlock) {
    std::string src = "{ String s = \"\"\"\n}\n\"\"\"; return; }";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(src[pos], '}');
    EXPECT_EQ(pos, src.size() - 1);
}

// 14. MatchingBraceUnmatched
TEST_F(JavaStubGeneratorTest, MatchingBraceUnmatched) {
    std::string src = "{ return;";
    size_t pos = JavaStubGenerator::findMatchingBrace(src, 0);
    EXPECT_EQ(pos, std::string::npos);
}

// ===========================================================================
// Return type detection tests
// ===========================================================================

// 15. IsVoidReturnTrue
TEST_F(JavaStubGeneratorTest, IsVoidReturnTrue) {
    std::string src = "public void run() {";
    size_t brace = src.find('{');
    EXPECT_TRUE(JavaStubGenerator::isVoidReturn(src, brace));
}

// 16. IsVoidReturnFalse
TEST_F(JavaStubGeneratorTest, IsVoidReturnFalse) {
    std::string src = "public int compute() {";
    size_t brace = src.find('{');
    EXPECT_FALSE(JavaStubGenerator::isVoidReturn(src, brace));
}

// 17. IsBooleanReturnTrue
TEST_F(JavaStubGeneratorTest, IsBooleanReturnTrue) {
    std::string src = "public boolean isValid() {";
    size_t brace = src.find('{');
    EXPECT_TRUE(JavaStubGenerator::isBooleanReturn(src, brace));
}

// 18. IsPrimitiveReturnInt
TEST_F(JavaStubGeneratorTest, IsPrimitiveReturnInt) {
    std::string src = "public int count() {";
    size_t brace = src.find('{');
    EXPECT_TRUE(JavaStubGenerator::isPrimitiveReturn(src, brace));
}

// 19. IsPrimitiveReturnLong
TEST_F(JavaStubGeneratorTest, IsPrimitiveReturnLong) {
    std::string src = "public long timestamp() {";
    size_t brace = src.find('{');
    EXPECT_TRUE(JavaStubGenerator::isPrimitiveReturn(src, brace));
}

// 20. ObjectReturnNotPrimitive
TEST_F(JavaStubGeneratorTest, ObjectReturnNotPrimitive) {
    std::string src = "public String getName() {";
    size_t brace = src.find('{');
    EXPECT_FALSE(JavaStubGenerator::isVoidReturn(src, brace));
    EXPECT_FALSE(JavaStubGenerator::isBooleanReturn(src, brace));
    EXPECT_FALSE(JavaStubGenerator::isPrimitiveReturn(src, brace));
}

// ===========================================================================
// Integration tests (file I/O)
// ===========================================================================

// 21. StubAndRestore
TEST_F(JavaStubGeneratorTest, StubAndRestore) {
    std::string original =
        "public class S {\n"
        "    public int compute() {\n"
        "        int x = 1;\n"
        "        return x + 2;\n"
        "    }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "compute");
    ASSERT_TRUE(result.success) << result.error;

    std::string modified = readFileContent(path);
    EXPECT_NE(modified.find("{ return 0; }"), std::string::npos) << "Expected int stub '{ return 0; }' in:\n"
                                                                 << modified;

    // Restore
    EXPECT_TRUE(gen.restoreFile(path, result));
    std::string restored = readFileContent(path);
    EXPECT_EQ(restored, original);
}

// 22. StubVoidMethod
TEST_F(JavaStubGeneratorTest, StubVoidMethod) {
    std::string original =
        "public class S {\n"
        "    public void run() {\n"
        "        System.out.println(\"hello\");\n"
        "    }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "run");
    ASSERT_TRUE(result.success) << result.error;

    std::string modified = readFileContent(path);
    EXPECT_NE(modified.find("{ }"), std::string::npos) << "Expected void stub '{ }' in:\n" << modified;
}

// 23. StubBooleanMethod
TEST_F(JavaStubGeneratorTest, StubBooleanMethod) {
    std::string original =
        "public class S {\n"
        "    public boolean isValid() {\n"
        "        return checkSomething();\n"
        "    }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "isValid");
    ASSERT_TRUE(result.success) << result.error;

    std::string modified = readFileContent(path);
    EXPECT_NE(modified.find("{ return false; }"), std::string::npos)
        << "Expected boolean stub '{ return false; }' in:\n"
        << modified;
}

// 24. StubObjectMethod
TEST_F(JavaStubGeneratorTest, StubObjectMethod) {
    std::string original =
        "public class S {\n"
        "    public String getName() {\n"
        "        return \"hello\";\n"
        "    }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "getName");
    ASSERT_TRUE(result.success) << result.error;

    std::string modified = readFileContent(path);
    EXPECT_NE(modified.find("{ return null; }"), std::string::npos) << "Expected object stub '{ return null; }' in:\n"
                                                                    << modified;
}

// 25. StubMethodNotFound
TEST_F(JavaStubGeneratorTest, StubMethodNotFound) {
    std::string original =
        "public class S {\n"
        "    public void run() { }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "nonexistent");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

// 26. StubNestedBraces
TEST_F(JavaStubGeneratorTest, StubNestedBraces) {
    std::string original =
        "public class S {\n"
        "    public int compute() {\n"
        "        if (true) {\n"
        "            for (int i = 0; i < 10; i++) {\n"
        "                if (i > 5) {\n"
        "                    return i;\n"
        "                }\n"
        "            }\n"
        "        }\n"
        "        return 0;\n"
        "    }\n"
        "}\n";
    auto path = writeTempFile("S.java", original);

    JavaStubGenerator gen;
    auto result = gen.stubFunction(path, "compute");
    ASSERT_TRUE(result.success) << result.error;

    std::string modified = readFileContent(path);
    EXPECT_NE(modified.find("{ return 0; }"), std::string::npos) << "Expected int stub after complex body in:\n"
                                                                 << modified;
    // The deeply nested braces should all be gone
    // Count braces — the stub body has exactly one { and one }
    // plus the class braces
}
