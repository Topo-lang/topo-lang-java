// E2E guard for examples/showcase: the shipped example must stay
// parse-clean and check-clean. The showcase once declared parameters
// named `record` — a stdlib type keyword, never a legal identifier —
// so every topo-check run on the example failed with a parse-error
// cascade. Running the full default check set ("all") over a copy of
// the example pins that regression class.
//
// Deliberately no GTEST_SKIP path: only the in-process L1 extractors
// run here (no jdtls), so the test is dependency-free and CI's
// skip != pass assert step can rely on it always running.

#include "CheckRunner.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace topo;

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

TEST(JavaShowcase, FullCheckPasses) {
    const fs::path src = fs::path(TOPO_EXAMPLES_DIR) / "showcase";
    ASSERT_TRUE(fs::exists(src)) << "examples/showcase missing at " << src;

    // Copy to a fresh temp dir so check caches never land in the checkout.
    const fs::path work = fs::temp_directory_path() /
        ("topo_java_showcase_test_" + std::to_string(topo_getpid()));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::copy(src, work, fs::copy_options::recursive);

    CheckConfig cfg;
    cfg.projectDir = work.string();
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);

    fs::remove_all(work, ec);
}
