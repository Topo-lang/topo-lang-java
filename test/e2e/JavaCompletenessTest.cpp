// E2E tests for Java completeness checks.
// Each test constructs a CheckConfig pointing at a Java fixture project,
// then verifies the exit code (0 = pass, 1 = errors found).

#include "CheckRunner.h"

#include <gtest/gtest.h>

using namespace topo;

static std::string fixtureDir(const char* name) {
    return std::string(TOPO_TEST_FIXTURES_DIR) + "/" + name;
}

TEST(JavaCompleteness, CompletenessPassFixture) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("completeness_pass");
    cfg.checkName = "completeness";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

TEST(JavaCompleteness, CompletenessFailFixture) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("completeness_fail");
    cfg.checkName = "completeness";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 1);
}
