// Unit tests for JavaAnalysisProvider::collectSourceFiles — source discovery
// over the Maven (src/main/java) and flat (src/) layouts.
//
// Regression focus: the scan used to feed each source root to
// fs::recursive_directory_iterator unguarded, so a root that is unexpectedly
// a plain file (or unreadable) raised an uncaught filesystem_error and
// aborted the whole checker. Iteration must degrade, never throw.

#include "analysis/JavaAnalysisProvider.h"

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

class JavaAnalysisProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        projectDir_ = fs::temp_directory_path() /
            ("topo_java_provider_test_" + std::to_string(topo_getpid()));
        fs::create_directories(projectDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(projectDir_, ec);
    }

    fs::path projectDir_;
};

TEST_F(JavaAnalysisProviderTest, FlatSrcLayoutIsDiscovered) {
    fs::create_directories(projectDir_ / "src");
    {
        std::ofstream ofs(projectDir_ / "src" / "Main.java");
        ofs << "public class Main {}\n";
    }

    auto provider = createJavaAnalysisProvider();
    auto files = provider->collectSourceFiles(projectDir_.string(), {});

    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(fs::path(files[0]).filename(), "Main.java");
}

TEST_F(JavaAnalysisProviderTest, SrcAsPlainFileDegradesToSkip) {
    // A project where `src` is a regular file, not a directory. The scan must
    // degrade to finding nothing — not abort on a filesystem_error.
    {
        std::ofstream ofs(projectDir_ / "src");
        ofs << "not a directory\n";
    }

    auto provider = createJavaAnalysisProvider();
    auto files = provider->collectSourceFiles(projectDir_.string(), {});

    EXPECT_TRUE(files.empty());
}
