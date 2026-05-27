// E2E tests for Java purity checks.
//
// Exercises the full topo-check pipeline for parallel-stage purity:
//   .topo parsing -> symbol table -> file scan -> JavaSymbolAccessExtractor
//   -> checkPurity -> diagnostics.

#include "CheckRunner.h"

#include <gtest/gtest.h>

using namespace topo;

static std::string fixtureDir(const char* name) {
    return std::string(TOPO_TEST_FIXTURES_DIR) + "/" + name;
}

TEST(JavaPurity, Pass01_NoStaticState) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_pass_01");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaPurity, Pass02_LocalsOnly) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_pass_02");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaPurity, Pass03_SequentialStagesAllowStaticWrites) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_pass_03");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaPurity, Pass04_FinalStaticConstants) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_pass_04");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaPurity, Fail01_ParallelStaticFieldWrite) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_fail_01");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GE(results[0].second.errorCount, 1);
    bool foundCounter = false;
    for (const auto& d : results[0].second.diagnostics) {
        if (d.message.find("counter") != std::string::npos) foundCounter = true;
    }
    EXPECT_TRUE(foundCounter) << "Expected `counter` to appear in the violation message";
}

TEST(JavaPurity, Fail02_ParallelQualifiedClassFieldWrite) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_fail_02");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GE(results[0].second.errorCount, 1);
    bool foundGValue = false;
    for (const auto& d : results[0].second.diagnostics) {
        if (d.message.find("gValue") != std::string::npos) foundGValue = true;
    }
    EXPECT_TRUE(foundGValue) << "Expected `gValue` to appear in the violation message";
}

TEST(JavaPurity, Fail03_IncrementCompound) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_fail_03");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    // ++ticks and ticks++ are separate writes; one violation per line is
    // emitted, so two distinct line writes -> at least 2 errors.
    EXPECT_GE(results[0].second.errorCount, 2);
}

TEST(JavaPurity, Fail04_MultipleParallelViolations) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_fail_04");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    // Three parallel methods each writing a distinct global -> 3 violations.
    EXPECT_GE(results[0].second.errorCount, 3);
}

// Regression guard for the JavaSymbolAccessExtractor single-line-body
// scope-leak fixed 2026-05-18. `parse` has a compact one-line body whose
// closing `}` was previously consumed before method detection, leaking
// `inFunction` so `audit`'s static `sideLog` write was dropped -> false
// PASS. The fix must keep flagging `audit`.
TEST(JavaPurity, Fail05_SingleLineBodyParallelStaticWrite) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_fail_05");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GE(results[0].second.errorCount, 1);
    bool foundSideLog = false;
    for (const auto& d : results[0].second.diagnostics) {
        if (d.message.find("sideLog") != std::string::npos) foundSideLog = true;
    }
    EXPECT_TRUE(foundSideLog)
        << "Expected `sideLog` in the violation message — the single-line "
           "`parse` body must not leak scope past `audit`";
}

// Symmetric compliant single-line case: same compact-body shape, no static
// writes -> the scope-leak fix must not over-flag pure single-line bodies.
TEST(JavaPurity, Pass05_SingleLineBodyPure) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("purity_java_pass_05");
    cfg.checkName = "purity";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}
