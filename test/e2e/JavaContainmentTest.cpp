// E2E tests for Java containment checks.
// Each test constructs a CheckConfig pointing at a Java fixture project,
// then verifies the exit code (0 = pass, 1 = errors found).

#include "CheckRunner.h"

#include <gtest/gtest.h>
#include <cstdlib>

using namespace topo;

static std::string fixtureDir(const char* name) {
    return std::string(TOPO_TEST_FIXTURES_DIR) + "/" + name;
}

/// Fixture for L2 deep-mode tests.  These force cfg.deepMode = true so
/// the JdtBridge + JavaSafetyAnalyzer path actually runs in CI —
/// otherwise the existing L1 tests silently bypass every L2 bug (the
/// L2 synthetic-caller attribution regression).  Tests skip themselves
/// when jdtls is unavailable on PATH so unconfigured dev machines don't
/// spuriously fail.
class JavaContainmentL2 : public ::testing::Test {
protected:
    void SetUp() override {
        if (std::system("command -v jdtls >/dev/null 2>&1") != 0) {
            GTEST_SKIP() << "jdtls unavailable on PATH — L2 deep containment "
                            "tests require jdtls (brew install jdtls).";
        }
    }
};

TEST(JavaContainment, ContainmentJavaFail) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_fail");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaExternalOk) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_external_ok");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaContainment, ContainmentReflection) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_reflection");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentRuntimeExec) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_runtime_exec");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJni) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_jni");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentClassLoader) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_classloader");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentThread) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_thread");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentNetwork) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_network");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentFileIo) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_file_io");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentSafeCode) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_safe_code");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

// ---- Adversarial fixtures (issue #11 + #12) ----

TEST(JavaContainment, ContainmentJavaThreadStop) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_thread_stop");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaRuntimeHalt) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_runtime_halt");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaMethodHandleDefine) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_method_handle_define");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaByteBuddy) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_bytebuddy");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaAccessController) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_access_controller");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

// Critical validation of issue #12 — Class::forName method reference bound
// to a Function variable. Without binding resolution this case would be
// missed by simple-name matching alone.
TEST(JavaContainment, ContainmentJavaMethodRefClassForName) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_method_ref_class_for_name");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

TEST(JavaContainment, ContainmentJavaSafeLambda) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_safe_lambda");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

// Critical validation of issue #12 — var-inferred receiver type. JDT binding
// resolution is required to know that rt resolves to java.lang.Runtime.
TEST(JavaContainment, ContainmentJavaVarRuntime) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_java_var_runtime");
    cfg.checkName = "containment";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}

// --- L2 deep-mode coverage ---
// These tests force cfg.deepMode = true so the JdtBridge +
// JavaSafetyAnalyzer path actually runs through checkContainment.  The
// existing tests above all exercise L1 only — without these, the
// `<l2:file:line>` synthetic caller attribution bug can sneak back in
// undetected.

TEST_F(JavaContainmentL2, ContainmentL2ExternalOk) {
    // external read_file() calls FileReader.close().  With correct
    // attribution via documentSymbol, the call site's caller resolves
    // to App::read_file which matches the .topo external declaration
    // via simple-name fallback, so the L2 run reports no violation.
    // Without attribution, the caller would be `<l2:./src/App.java:9>`
    // and the call would be (incorrectly) flagged as a violation.
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_external_ok");
    cfg.checkName = "containment";
    cfg.deepMode = true;
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST_F(JavaContainmentL2, ContainmentL2RuntimeExec) {
    // Non-external function calls Runtime.exec() — should FAIL under L2.
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("containment_runtime_exec");
    cfg.checkName = "containment";
    cfg.deepMode = true;
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}
