#include "JavaAnalysisProvider.h"
#include "extract/JavaSourceFacts.h"
#include "topo/Platform/Platform.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
namespace fs = std::filesystem;
using namespace topo::check;
using Json = nlohmann::json;
class JavaSourceFactsTest : public ::testing::Test {
protected:
    fs::path root;
    void SetUp() override {
        root = fs::temp_directory_path() /
               ("topo-java-facts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path write(const std::string& file, const std::string& text) {
        auto p = root / file;
        std::ofstream(p) << text;
        return p;
    }
};

// Exercise the production subprocess protocol with a staged bare-name launcher,
// matching the POSIX script / Windows .cmd pattern in ProcessSpawnTest.
class JavaSourceFactsProtocolTest : public JavaSourceFactsTest {
protected:
    std::string oldPath;
    bool hadPath = false;
    bool changedPath = false;
    fs::path source;

    void SetUp() override {
        JavaSourceFactsTest::SetUp();
        source = write("Protocol.java", "class Protocol {}\n");
        if (topo::platform::IsWindows) {
            write("topo-extract-java.cmd",
                  "@echo off\r\nmore > \"%~dp0request.json\"\r\ntype \"%~dp0response.json\"\r\n");
        } else {
            auto script = write("topo-extract-java",
                                "#!/bin/sh\ncat > \"${0%/*}/request.json\"\ncat \"${0%/*}/response.json\"\n");
            std::error_code ec;
            fs::permissions(script, fs::perms::owner_all, ec);
            ASSERT_FALSE(ec) << ec.message();
        }
        const char* path = std::getenv("PATH");
        hadPath = path != nullptr;
        oldPath = path ? path : "";
        std::string newPath = root.string() + std::string(topo::platform::PathSeparator) + oldPath;
#ifdef _WIN32
        ASSERT_EQ(_putenv_s("PATH", newPath.c_str()), 0);
#else
        ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);
#endif
        changedPath = true;
    }

    void TearDown() override {
        if (changedPath) {
#ifdef _WIN32
            _putenv_s("PATH", hadPath ? oldPath.c_str() : "");
#else
            if (hadPath)
                setenv("PATH", oldPath.c_str(), 1);
            else
                unsetenv("PATH");
#endif
        }
        JavaSourceFactsTest::TearDown();
    }

    Json validResponse() const {
        return Json{{"schemaVersion", 2},
                    {"facts",
                     {{"symbols",
                       Json::array({{{"file", source.string()},
                                     {"qualifiedName", "Protocol"},
                                     {"simpleName", "Protocol"},
                                     {"kind", "class"}}})}}},
                    {"diagnostics", Json::array()},
                    {"analysisComplete", true}};
    }

    std::unique_ptr<JavaSourceFacts> loadResponse(const Json& response) {
        write("response.json", response.dump());
        auto facts = std::make_unique<JavaSourceFacts>();
        facts->load({source.string()});
        std::ifstream requestFile(root / "request.json");
        Json request;
        requestFile >> request;
        EXPECT_EQ(request.at("files"), Json::array({source.string()}));
        return facts;
    }
};

TEST_F(JavaSourceFactsProtocolTest, AcceptsCompleteVersionTwoEnvelope) {
    auto facts = loadResponse(validResponse());
    EXPECT_TRUE(facts->diagnostics().empty());
    auto symbols = facts->symbols(source.string());
    ASSERT_EQ(symbols.size(), 1u);
    EXPECT_EQ(symbols[0].qualifiedName, "Protocol");
}

TEST_F(JavaSourceFactsProtocolTest, IncompleteAnalysisWithOnlyWarningsAddsError) {
    auto response = validResponse();
    response["analysisComplete"] = false;
    response["diagnostics"] =
        Json::array({{{"severity", "warning"}, {"code", "partial"}, {"message", "Some facts are incomplete"}}});
    auto facts = loadResponse(response);
    auto diagnostics = facts->diagnostics();
    ASSERT_EQ(diagnostics.size(), 2u);
    EXPECT_EQ(diagnostics[0].severity, Severity::Warning);
    EXPECT_EQ(diagnostics[0].message, "partial: Some facts are incomplete");
    EXPECT_EQ(diagnostics[1].severity, Severity::Error);
    EXPECT_NE(diagnostics[1].message.find("incomplete analysis"), std::string::npos);
}

TEST_F(JavaSourceFactsProtocolTest, IncompleteAnalysisWithoutDiagnosticsAddsError) {
    auto response = validResponse();
    response["analysisComplete"] = false;
    auto facts = loadResponse(response);
    auto diagnostics = facts->diagnostics();
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].severity, Severity::Error);
    EXPECT_NE(diagnostics[0].message.find("incomplete analysis"), std::string::npos);
}

TEST_F(JavaSourceFactsProtocolTest, IncompleteAnalysisPreservesExistingError) {
    auto response = validResponse();
    response["analysisComplete"] = false;
    response["diagnostics"] =
        Json::array({{{"severity", "error"}, {"code", "unresolved"}, {"message", "Missing dependency"}}});
    auto facts = loadResponse(response);
    auto diagnostics = facts->diagnostics();
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].severity, Severity::Error);
    EXPECT_EQ(diagnostics[0].message, "unresolved: Missing dependency");
}

TEST_F(JavaSourceFactsProtocolTest, RejectsMissingEnvelopeFieldsBeforePublishingFacts) {
    for (const char* field : {"schemaVersion", "facts", "diagnostics", "analysisComplete"}) {
        SCOPED_TRACE(field);
        auto response = validResponse();
        response.erase(field);
        auto facts = loadResponse(response);
        auto diagnostics = facts->diagnostics();
        ASSERT_EQ(diagnostics.size(), 1u);
        EXPECT_EQ(diagnostics[0].severity, Severity::Error);
        EXPECT_NE(diagnostics[0].message.find("schemaVersion 2"), std::string::npos);
        EXPECT_TRUE(facts->symbols(source.string()).empty());
    }
}

TEST_F(JavaSourceFactsProtocolTest, RejectsUnsupportedVersionsAndMalformedEnvelopeTypes) {
    const std::vector<std::pair<std::string, Json>> invalidFields = {{"schemaVersion", 1},
                                                                     {"schemaVersion", 3},
                                                                     {"schemaVersion", 2.0},
                                                                     {"schemaVersion", "2"},
                                                                     {"schemaVersion", nullptr},
                                                                     {"facts", Json::array()},
                                                                     {"facts", nullptr},
                                                                     {"diagnostics", Json::object()},
                                                                     {"diagnostics", nullptr},
                                                                     {"analysisComplete", "false"},
                                                                     {"analysisComplete", 0},
                                                                     {"analysisComplete", nullptr}};
    for (const auto& [field, value] : invalidFields) {
        SCOPED_TRACE(field + " = " + value.dump());
        auto response = validResponse();
        response[field] = value;
        auto facts = loadResponse(response);
        auto diagnostics = facts->diagnostics();
        ASSERT_EQ(diagnostics.size(), 1u);
        EXPECT_EQ(diagnostics[0].severity, Severity::Error);
        EXPECT_NE(diagnostics[0].message.find("schemaVersion 2"), std::string::npos);
        EXPECT_TRUE(facts->symbols(source.string()).empty());
    }
    for (const Json& response : {Json::array(), Json(nullptr), Json("invalid")}) {
        auto facts = loadResponse(response);
        ASSERT_EQ(facts->diagnostics().size(), 1u);
        EXPECT_EQ(facts->diagnostics()[0].severity, Severity::Error);
        EXPECT_TRUE(facts->symbols(source.string()).empty());
    }
}

TEST_F(JavaSourceFactsProtocolTest, RejectsMalformedDiagnosticsBeforePublishingFacts) {
    for (const Json& diagnostic : {Json::array(), Json(nullptr), Json{{"severity", false}}, Json{{"line", "1"}}}) {
        SCOPED_TRACE(diagnostic.dump());
        auto response = validResponse();
        response["diagnostics"] = Json::array({diagnostic});
        auto facts = loadResponse(response);
        auto diagnostics = facts->diagnostics();
        ASSERT_EQ(diagnostics.size(), 1u);
        EXPECT_EQ(diagnostics[0].severity, Severity::Error);
        EXPECT_NE(diagnostics[0].message.find("Java AST extraction failed"), std::string::npos);
        EXPECT_TRUE(facts->symbols(source.string()).empty());
    }
}

TEST_F(JavaSourceFactsTest, RejectsMalformedProjectModelBeforeParsing) {
    for (const std::string text : {"[]",
                                   "{\"sourceLevel\":25}",
                                   "{\"classpath\":\"classes\"}",
                                   "{\"sourcepath\":[\"\"]}",
                                   "{\"files\":[false]}"}) {
        auto model = write("project.json", text);
        JavaSourceFacts facts;
        facts.configure(root.string(), model.string());
        auto d = facts.diagnostics();
        ASSERT_FALSE(d.empty()) << text;
        EXPECT_EQ(d[0].severity, Severity::Error);
    }
}
TEST_F(JavaSourceFactsTest, DirectFactoryLoadsRealJdtInstanceEdges) {
    auto file =
        write("Calls.java", "class Calls { int apply(int x){ return x; } int run(Calls obj){return obj.apply(4);} }");
    auto provider = createJavaAnalysisProvider();
    auto extractor = provider->createCallEdgeExtractor();
    auto edges = extractor->extractCallEdges(file.string());
    bool found = false;
    for (const auto& e : edges)
        if (e.caller == "Calls::run" && e.callee == "Calls::apply") found = true;
    EXPECT_TRUE(found);
    EXPECT_TRUE(provider->analysisDiagnostics().empty());
}
TEST_F(JavaSourceFactsTest, MissingDependencyStaysExplicitInProvider) {
    auto file = write("Broken.java", "class Broken { void run(Missing obj){obj.call();} }");
    auto provider = createJavaAnalysisProvider();
    provider->createCallEdgeExtractor()->extractCallEdges(file.string());
    auto diagnostics = provider->analysisDiagnostics();
    ASSERT_FALSE(diagnostics.empty());
    bool unresolved = false;
    for (const auto& d : diagnostics)
        if (d.message.find("unresolved-call") != std::string::npos) unresolved = true;
    EXPECT_TRUE(unresolved);
}
TEST_F(JavaSourceFactsTest, AllFactoriesUseSameBatchForSymbolsCallsAndWrites) {
    auto file =
        write("Facts.java", "class Facts { static int state; void run(){ state++; System.currentTimeMillis(); } }");
    auto provider = createJavaAnalysisProvider();
    auto symbols = provider->createSymbolExtractor()->extractSymbols(file.string());
    EXPECT_FALSE(symbols.empty());
    auto accesses = provider->createSymbolAccessExtractor()->extractSymbolAccesses(file.string());
    ASSERT_FALSE(accesses.empty());
    EXPECT_EQ(accesses[0].symbol, "Facts::state");
    EXPECT_TRUE(accesses[0].isWrite);
    auto calls = provider->createCallSiteExtractor()->extractCallSites(file.string());
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls[0].calleePattern, "java::lang::System::currentTimeMillis");
    EXPECT_TRUE(provider->analysisDiagnostics().empty());
}

TEST_F(JavaSourceFactsTest, ResolvedExternalNamesStillMatchUnsafeCatalog) {
    auto file = write("Unsafe.java", "class Unsafe { void run(){ System.exit(0); } }");
    auto provider = createJavaAnalysisProvider();
    auto calls = provider->createCallSiteExtractor()->extractCallSites(file.string());
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].unsafeLevel, UnsafeLevel::Escape);
}
