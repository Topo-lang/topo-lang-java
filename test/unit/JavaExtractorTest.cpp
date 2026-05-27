// Unit tests for JavaImportExtractor and JavaCallSiteExtractor.
// Creates real Java files on disk and runs the extractors,
// verifying correctness of regex-based extraction.

#include "analysis/extract/JavaImportExtractor.h"
#include "analysis/extract/JavaCallSiteExtractor.h"
#include "topo/Check/ContainmentTypes.h"
#include "topo/Check/CapabilityCatalog.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

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

// ---------------------------------------------------------------------------
// Fixture: creates a temp directory, provides a helper to write files.
// ---------------------------------------------------------------------------
class JavaExtractorTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = fs::temp_directory_path() /
                   ("topo_java_extractor_" + std::to_string(topo_getpid()));
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

    fs::path tempDir_;
};

// ===========================================================================
// JavaImportExtractor tests
// ===========================================================================

TEST_F(JavaExtractorTest, Import_BasicImport) {
    auto path = writeTempFile("Basic.java",
        "import java.io.File;\n"
        "import java.util.List;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    // java.io.File is classified as File capability; java.util.List is safe
    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.io.File");
    EXPECT_EQ(imports[0].line, 1);
    EXPECT_EQ(imports[0].file, path);
}

TEST_F(JavaExtractorTest, Import_WildcardImport) {
    auto path = writeTempFile("Wildcard.java",
        "import java.net.*;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    // java.net.* normalized to java.net, classified as Network
    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.net");
    EXPECT_EQ(imports[0].line, 1);
}

TEST_F(JavaExtractorTest, Import_StaticImport) {
    auto path = writeTempFile("Static.java",
        "import static java.nio.file.Files.readAllLines;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    // java.nio.file.Files.readAllLines starts with java.nio -> File
    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.nio.file.Files.readAllLines");
    EXPECT_EQ(imports[0].line, 1);
}

TEST_F(JavaExtractorTest, Import_BlockCommentIgnored) {
    auto path = writeTempFile("Comment.java",
        "/* \n"
        "import java.io.File;\n"
        "*/\n"
        "import java.net.Socket;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    // Only the import outside the block comment should be found
    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.net.Socket");
    EXPECT_EQ(imports[0].line, 4);
}

TEST_F(JavaExtractorTest, Import_LineCommentIgnored) {
    auto path = writeTempFile("LineComment.java",
        "// import java.io.File;\n"
        "import java.net.Socket;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.net.Socket");
    EXPECT_EQ(imports[0].line, 2);
}

TEST_F(JavaExtractorTest, Import_ProcessBuilderClassified) {
    auto path = writeTempFile("Proc.java",
        "import java.lang.ProcessBuilder;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "java.lang.ProcessBuilder");
}

TEST_F(JavaExtractorTest, Import_SafeImportNotExtracted) {
    auto path = writeTempFile("Safe.java",
        "import java.util.List;\n"
        "import java.util.ArrayList;\n"
        "import java.lang.String;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    // None of these are capability-related
    EXPECT_TRUE(imports.empty());
}

TEST_F(JavaExtractorTest, Import_MultipleCapabilities) {
    auto path = writeTempFile("Multi.java",
        "import java.io.File;\n"
        "import java.net.Socket;\n"
        "import java.lang.ProcessBuilder;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    ASSERT_EQ(imports.size(), 3u);
    EXPECT_EQ(imports[0].normalizedPath, "java.io.File");
    EXPECT_EQ(imports[0].line, 1);
    EXPECT_EQ(imports[1].normalizedPath, "java.net.Socket");
    EXPECT_EQ(imports[1].line, 2);
    EXPECT_EQ(imports[2].normalizedPath, "java.lang.ProcessBuilder");
    EXPECT_EQ(imports[2].line, 3);
}

TEST_F(JavaExtractorTest, Import_ExtractAll_MultipleFiles) {
    auto path1 = writeTempFile("A.java", "import java.io.File;\n");
    auto path2 = writeTempFile("B.java", "import java.net.Socket;\n");

    JavaImportExtractor extractor;
    auto imports = extractor.extractAll({path1, path2});

    ASSERT_EQ(imports.size(), 2u);
    EXPECT_EQ(imports[0].file, path1);
    EXPECT_EQ(imports[0].normalizedPath, "java.io.File");
    EXPECT_EQ(imports[1].file, path2);
    EXPECT_EQ(imports[1].normalizedPath, "java.net.Socket");
}

TEST_F(JavaExtractorTest, Import_EmptyFile) {
    auto path = writeTempFile("Empty.java", "");

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    EXPECT_TRUE(imports.empty());
}

TEST_F(JavaExtractorTest, Import_NonexistentFile) {
    auto path = (tempDir_ / "Nonexistent.java").string();

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    EXPECT_TRUE(imports.empty());
}

TEST_F(JavaExtractorTest, Import_JavaxNetClassified) {
    auto path = writeTempFile("Ssl.java",
        "import javax.net.ssl.SSLSocket;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0].normalizedPath, "javax.net.ssl.SSLSocket");
}

TEST_F(JavaExtractorTest, Import_NioClassified) {
    auto path = writeTempFile("Nio.java",
        "import java.nio.file.Path;\n"
        "import java.nio.channels.FileChannel;\n"
    );

    JavaImportExtractor extractor;
    auto imports = extractor.extractImports(path);

    ASSERT_EQ(imports.size(), 2u);
    EXPECT_EQ(imports[0].normalizedPath, "java.nio.file.Path");
    EXPECT_EQ(imports[1].normalizedPath, "java.nio.channels.FileChannel");
}

// ===========================================================================
// JavaCallSiteExtractor tests
// ===========================================================================

TEST_F(JavaExtractorTest, CallSite_BasicNewSocket) {
    auto path = writeTempFile("NetClient.java",
        "public class NetClient {\n"
        "    public void connect() {\n"
        "        Socket s = new Socket(\"localhost\", 8080);\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].callerQualifiedName, "NetClient::connect");
    EXPECT_EQ(sites[0].calleePattern, "Socket");
    EXPECT_EQ(sites[0].capability, CapabilityKind::Network);
    EXPECT_EQ(sites[0].file, path);
    EXPECT_EQ(sites[0].line, 3);
}

TEST_F(JavaExtractorTest, CallSite_ConstructorCall) {
    auto path = writeTempFile("FileOps.java",
        "public class FileOps {\n"
        "    public void readData() {\n"
        "        FileReader fr = new FileReader(\"data.txt\");\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].callerQualifiedName, "FileOps::readData");
    EXPECT_EQ(sites[0].calleePattern, "FileReader");
    EXPECT_EQ(sites[0].capability, CapabilityKind::File);
}

TEST_F(JavaExtractorTest, CallSite_QualifiedRuntimeExec) {
    auto path = writeTempFile("Exec.java",
        "public class Exec {\n"
        "    public void run() {\n"
        "        Runtime.getRuntime().exec(\"ls\");\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    // Should detect both Runtime.getRuntime and Runtime.exec
    ASSERT_GE(sites.size(), 1u);
    bool foundExec = false;
    for (const auto& s : sites) {
        if (s.calleePattern == "Runtime.exec" || s.calleePattern == "Runtime.getRuntime") {
            EXPECT_EQ(s.capability, CapabilityKind::Process);
            foundExec = true;
        }
    }
    EXPECT_TRUE(foundExec);
}

TEST_F(JavaExtractorTest, CallSite_ProcessBuilder) {
    auto path = writeTempFile("Builder.java",
        "public class Builder {\n"
        "    public void build() {\n"
        "        ProcessBuilder pb = new ProcessBuilder(\"cmd\");\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].calleePattern, "ProcessBuilder");
    EXPECT_EQ(sites[0].capability, CapabilityKind::Process);
}

TEST_F(JavaExtractorTest, CallSite_NestedClassScope) {
    auto path = writeTempFile("Outer.java",
        "package com.app;\n"
        "public class Outer {\n"
        "    class Inner {\n"
        "        public void doIO() {\n"
        "            FileWriter fw = new FileWriter(\"out.txt\");\n"
        "        }\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].callerQualifiedName, "com::app::Outer::Inner::doIO");
    EXPECT_EQ(sites[0].calleePattern, "FileWriter");
    EXPECT_EQ(sites[0].capability, CapabilityKind::File);
}

TEST_F(JavaExtractorTest, CallSite_PackageQualifiedName) {
    auto path = writeTempFile("Server.java",
        "package net.server;\n"
        "public class Server {\n"
        "    public void start() {\n"
        "        ServerSocket ss = new ServerSocket(8080);\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].callerQualifiedName, "net::server::Server::start");
    EXPECT_EQ(sites[0].calleePattern, "ServerSocket");
    EXPECT_EQ(sites[0].capability, CapabilityKind::Network);
}

TEST_F(JavaExtractorTest, CallSite_OutsideMethodNotDetected) {
    // Field-level initializers are not inside a method body
    auto path = writeTempFile("FieldInit.java",
        "public class FieldInit {\n"
        "    Socket s = new Socket();\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    // Field-level Socket() is not inside a method, so not detected
    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_CommentIgnored) {
    auto path = writeTempFile("Commented.java",
        "public class Commented {\n"
        "    public void fn() {\n"
        "        // Socket s = new Socket();\n"
        "        int x = 1;\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_BlockCommentIgnored) {
    auto path = writeTempFile("BlockComment.java",
        "public class BlockComment {\n"
        "    public void fn() {\n"
        "        /* Socket s = new Socket(); */\n"
        "        int x = 1;\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_SafeApiNotDetected) {
    auto path = writeTempFile("SafeApi.java",
        "public class SafeApi {\n"
        "    public void process() {\n"
        "        List<String> items = new ArrayList<>();\n"
        "        Collections.sort(items);\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_MultipleCallsInOneMethod) {
    auto path = writeTempFile("Multi.java",
        "public class Multi {\n"
        "    public void setup() {\n"
        "        Socket s = new Socket(\"host\", 80);\n"
        "        FileWriter fw = new FileWriter(\"log.txt\");\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 2u);
    EXPECT_EQ(sites[0].calleePattern, "Socket");
    EXPECT_EQ(sites[0].capability, CapabilityKind::Network);
    EXPECT_EQ(sites[1].calleePattern, "FileWriter");
    EXPECT_EQ(sites[1].capability, CapabilityKind::File);
}

TEST_F(JavaExtractorTest, CallSite_EmptyFile) {
    auto path = writeTempFile("Empty.java", "");

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_NonexistentFile) {
    auto path = (tempDir_ / "Nonexistent.java").string();

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    EXPECT_TRUE(sites.empty());
}

TEST_F(JavaExtractorTest, CallSite_ClassForNameDynamicLoad) {
    auto path = writeTempFile("Loader.java",
        "public class Loader {\n"
        "    public void load() {\n"
        "        Class.forName(\"com.example.Plugin\");\n"
        "    }\n"
        "}\n"
    );

    JavaCallSiteExtractor extractor;
    auto sites = extractor.extractCallSites(path);

    ASSERT_EQ(sites.size(), 1u);
    EXPECT_EQ(sites[0].calleePattern, "Class.forName");
    EXPECT_EQ(sites[0].capability, CapabilityKind::DynamicLoad);
}
