// E2E tests for Java stage-isolation checks.
//
// Exercises the full topo-check pipeline for stage ordering:
//   .topo parsing -> symbol table -> file scan -> JavaCallEdgeExtractor
//   -> checkStageIsolation -> diagnostics.

#include "CheckRunner.h"

#include <gtest/gtest.h>

using namespace topo;

static std::string fixtureDir(const char* name) {
    return std::string(TOPO_TEST_FIXTURES_DIR) + "/" + name;
}

TEST(JavaStageIsolation, Pass01_NoCrossStageCalls) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_pass_01");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaStageIsolation, Pass02_SameStageCall) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_pass_02");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaStageIsolation, Pass03_BackwardStageCall) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_pass_03");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaStageIsolation, Pass04_ModeOffSuppressesViolations) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_pass_04");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaStageIsolation, Fail01_ForwardStageCall) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_fail_01");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GE(results[0].second.errorCount, 1);
    bool foundStageMsg = false;
    for (const auto& d : results[0].second.diagnostics) {
        if (d.message.find("later stage 2") != std::string::npos) foundStageMsg = true;
    }
    EXPECT_TRUE(foundStageMsg) << "Expected stage-ordering message mentioning `later stage 2`";
}

TEST(JavaStageIsolation, Fail02_StageSkip) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_fail_02");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_GE(results[0].second.errorCount, 1);
}

TEST(JavaStageIsolation, Fail03_MultipleForwardViolations) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_fail_03");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    // Both loadA and loadB (stage<1>) call merge (stage<2>).
    EXPECT_GE(results[0].second.errorCount, 2);
}

TEST(JavaStageIsolation, Fail04_ThreeStageChain) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("stage_isolation_java_fail_04");
    cfg.checkName = "stage-isolation";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
    auto& results = runner.lastResults();
    ASSERT_EQ(results.size(), 1u);
    // stage<1> -> stage<3> and stage<2> -> stage<3> are two forward violations.
    EXPECT_GE(results[0].second.errorCount, 2);
}
