#include "JavaAnalysisProvider.h"
#include "extract/JavaSourceFacts.h"
#include "topo/Platform/Platform.h"
#include "topo/Distribution/Sha256.h"
#include "topo/Check/Audit.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs = std::filesystem;
using namespace topo::check;
using Json = nlohmann::json;
namespace {
void createRuntimeSymlink(const fs::path& target, const fs::path& link, std::error_code& error) {
#ifdef _WIN32
    auto nativeTarget = target; nativeTarget.make_preferred();
    auto nativeLink = link; nativeLink.make_preferred();
    if (CreateSymbolicLinkW(nativeLink.c_str(), nativeTarget.c_str(), 0x2)) { error.clear(); return; }
    auto code = GetLastError();
    if (code == ERROR_INVALID_PARAMETER || code == ERROR_PRIVILEGE_NOT_HELD) {
        if (CreateSymbolicLinkW(nativeLink.c_str(), nativeTarget.c_str(), 0)) { error.clear(); return; }
        code = GetLastError();
    }
    error = std::error_code(static_cast<int>(code), std::system_category());
#else
    fs::create_symlink(target, link, error);
#endif
}
}
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
        std::ofstream(p, std::ios::binary) << text;
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

    Json validAuditResponse() const {
        auto response = validResponse();
        response["facts"]["audit"] = {
            {"schemaVersion", 1}, {"provider", "java-jdt-test"}, {"supported", true}, {"complete", true},
            {"identities", Json::array()}, {"dependencies", Json::array()}, {"references", Json::array()},
            {"nativeBindings", Json::array()}, {"inputs", Json::array({source.string()})},
            {"inputDigests", {{source.string(), topo::dist::sha256File(source.string())}}},
            {"unresolved", Json::array()}};
        return response;
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

TEST_F(JavaSourceFactsProtocolTest, PreservesAuditIdentityAndDependencyBinding) {
    auto response = validResponse();
    response["facts"]["audit"] = {
        {"schemaVersion", 1}, {"provider", "java-jdt-test"}, {"supported", true}, {"complete", true},
        {"identities", Json::array({{{"id", "source#method"}, {"name", "sample.Log.write"},
            {"kind", "method"}, {"signature", "sample.Log.write(int):void"}, {"owner", "source#class"},
            {"file", source.string()}, {"line", 1}, {"origin", source.string()}, {"complete", true}}})},
        {"dependencies", Json::array({{{"id", "jar"}, {"path", "/dep/log.jar"}, {"version", "1"}, {"digest", ""}, {"project", false}, {"ownership", "external"}, {"coordinate", "dep:log"}}})},
        {"references", Json::array({{{"caller", "source#method"}, {"target", "jar#method"},
            {"file", source.string()}, {"line", 2}, {"resolved", true}, {"dispatch", "virtual"},
            {"runtimeTargetsComplete", false}, {"dependency", "jar"}}})},
        {"nativeBindings", Json::array()}, {"inputs", Json::array({source.string()})},
        {"inputDigests", {{source.string(), topo::dist::sha256File(source.string())}}},
        {"analysisIsolation", "forged-child-isolation"}, {"unresolved", Json::array()}};
    auto facts = loadResponse(response);
    auto audit = facts->auditFacts();
    ASSERT_TRUE(audit.supported); ASSERT_TRUE(audit.complete);
    EXPECT_EQ(audit.analysisIsolation, "bounded-process");
    ASSERT_EQ(audit.identities.size(), 1u);
    EXPECT_EQ(audit.identities[0].name, "sample::Log::write");
    EXPECT_EQ(audit.identities[0].owner, "source#class");
    ASSERT_EQ(audit.references.size(), 1u);
    EXPECT_EQ(audit.references[0].dependency, "jar");
    EXPECT_EQ(audit.references[0].dispatch, "virtual");
    EXPECT_FALSE(audit.references[0].runtimeTargetsComplete);
    EXPECT_EQ(audit.inputs.size(), 2u); // source and the physical launcher actually spawned
    response["facts"]["audit"]["schemaVersion"] = 99;
    EXPECT_FALSE(loadResponse(response)->auditFacts().complete);
}

TEST_F(JavaSourceFactsProtocolTest, AuditPublicationRejectsUnapprovedChildPathsAsWholeBatch) {
    auto project = root / "project";
    fs::create_directories(project);
    auto local = write("project/Local.java", "class Local {}");
    auto outside = write("outside.txt", "SYNTHETIC_CHILD_PATH_SECRET");
    auto model = write("project/model.json", Json{{"files", {local.string()}}}.dump());
    for (const std::string field : {"inputs", "inputDigests", "identityFile", "identityOrigin", "dependency",
                                    "reference", "nativeBinding", "relativeInput", "emptyInput", "relativeDigest"}) {
        SCOPED_TRACE(field);
        auto response = validAuditResponse();
        auto& audit = response["facts"]["audit"];
        audit["inputs"] = {local.string()};
        audit["inputDigests"] = {{local.string(), topo::dist::sha256File(local.string())}};
        if (field == "relativeInput") audit["inputs"].push_back("Local.java");
        else if (field == "emptyInput") audit["inputs"].push_back("");
        else if (field == "relativeDigest") audit["inputDigests"]["Local.java"] = "untrusted-digest";
        else if (field == "inputs") audit["inputs"].push_back(outside.string());
        else if (field == "inputDigests") audit["inputDigests"][outside.string()] = "untrusted-digest";
        else if (field == "identityFile" || field == "identityOrigin")
            audit["identities"].push_back({{"id", "source#class"}, {"name", "Local"}, {"kind", "class"},
                {"signature", ""}, {"owner", ""}, {"file", field == "identityFile" ? outside.string() : local.string()},
                {"line", 1}, {"origin", field == "identityOrigin" ? outside.string() : local.string()}, {"complete", true}});
        else if (field == "dependency")
            audit["dependencies"].push_back({{"id", "outside"}, {"path", outside.string()}, {"version", "1"},
                {"digest", ""}, {"project", false}, {"ownership", "external"}, {"coordinate", "test:outside"}});
        else if (field == "reference")
            audit["references"].push_back({{"caller", "local"}, {"target", "external"}, {"file", outside.string()},
                {"line", 1}, {"resolved", true}, {"dispatch", "direct"}, {"runtimeTargetsComplete", true}, {"dependency", ""}});
        else
            audit["nativeBindings"].push_back({{"caller", "local"}, {"file", outside.string()}, {"line", 1},
                {"operation", "load"}, {"library", "test"}, {"symbol", ""}, {"descriptor", ""}, {"resolved", false},
                {"callback", false}, {"lookup", ""}, {"unresolvedReason", "synthetic"}});
        write("response.json", response.dump());
        JavaSourceFacts facts;
        facts.configure(project.string(), model.string());
        facts.load({local.string()});
        ASSERT_TRUE(facts.diagnostics().empty());
        // Stage a forged ordinary-protocol envelope, then exercise only the
        // audit publication boundary; this does not claim sandbox execution.
        std::string capability;
        facts.requireAuditIsolation({}, capability);
        auto published = facts.auditFacts();
        EXPECT_FALSE(published.supported);
        EXPECT_FALSE(published.complete);
        EXPECT_TRUE(published.inputs.empty());
        EXPECT_TRUE(published.inputDigests.empty());
        EXPECT_TRUE(published.identities.empty());
        EXPECT_TRUE(published.dependencies.empty());
        EXPECT_TRUE(published.references.empty());
        EXPECT_TRUE(published.nativeBindings.empty());
        ASSERT_FALSE(published.unresolved.empty());
        EXPECT_NE(published.unresolved.front().find("outside authorized analysis inputs"), std::string::npos);
        const auto report = makeAuditReport(project.string(), published, {}, {}, {}, {});
        EXPECT_EQ(report.dump().find(topo::dist::sha256File(outside.string())), std::string::npos);
    }
}

TEST_F(JavaSourceFactsProtocolTest, AuditPublicationAcceptsExplicitRootsAndParentKnownExtractor) {
    auto project = root / "project";
    auto external = root / "external";
    fs::create_directories(project);
    fs::create_directories(external);
    auto local = write("project/Local.java", "class Local {}");
    auto dependency = write("external/dependency.jar", "synthetic dependency");
    auto model = write("project/model.json", Json{{"files", {local.string()}}}.dump());
    auto response = validAuditResponse();
    const auto extractor = root / (topo::platform::IsWindows ? "topo-extract-java.cmd" : "topo-extract-java");
    response["facts"]["audit"]["inputs"] = {local.string(), dependency.string(), extractor.string()};
    response["facts"]["audit"]["inputDigests"] = {
        {local.string(), topo::dist::sha256File(local.string())},
        {dependency.string(), topo::dist::sha256File(dependency.string())},
        {extractor.string(), topo::dist::sha256File(extractor.string())}};
    write("response.json", response.dump());
    JavaSourceFacts facts;
    facts.configure(project.string(), model.string());
    facts.load({local.string()});
    ASSERT_TRUE(facts.diagnostics().empty());
    std::string capability;
    facts.requireAuditIsolation({external.string()}, capability);
    const auto audit = facts.auditFacts();
    EXPECT_TRUE(audit.supported);
    EXPECT_TRUE(audit.complete);
    EXPECT_TRUE(audit.unresolved.empty());
}

TEST_F(JavaSourceFactsProtocolTest, LimitsRequestsAndResponsesBeforePublishingFacts) {
    write("response.json", validResponse().dump());
    JavaSourceFacts smallRequest({60000, 1, 1024});
    smallRequest.load({source.string()});
    ASSERT_EQ(smallRequest.diagnostics().size(), 1u);
    EXPECT_NE(smallRequest.diagnostics()[0].message.find("request exceeds"), std::string::npos);
    EXPECT_FALSE(fs::exists(root / "request.json"));
    JavaSourceFacts smallResponse({60000, 1024, 1});
    smallResponse.load({source.string()});
    ASSERT_EQ(smallResponse.diagnostics().size(), 1u);
    EXPECT_NE(smallResponse.diagnostics()[0].message.find("response exceeds"), std::string::npos);
    EXPECT_TRUE(smallResponse.symbols(source.string()).empty());
}

TEST_F(JavaSourceFactsProtocolTest, BoundsStalledExtractorAndRejectsNonzeroExit) {
    auto script = root / (topo::platform::IsWindows ? "topo-extract-java.cmd" : "topo-extract-java");
    std::ofstream(script, std::ios::binary) << (topo::platform::IsWindows
        ? "@echo off\r\nmore > nul\r\n:again\r\ngoto again\r\n"
        : "#!/bin/sh\ncat > /dev/null\nwhile :; do :; done\n");
    JavaSourceFacts stalled({100, 1024, 1024});
    auto start = std::chrono::steady_clock::now();
    stalled.load({source.string()});
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    ASSERT_EQ(stalled.diagnostics().size(), 1u);
    EXPECT_NE(stalled.diagnostics()[0].message.find("time limit"), std::string::npos)
        << stalled.diagnostics()[0].message;
    std::ofstream(script, std::ios::binary) << (topo::platform::IsWindows
        ? "@echo off\r\nmore > nul\r\ntype \"%~dp0response.json\"\r\nexit /b 17\r\n"
        : "#!/bin/sh\ncat > /dev/null\ncat \"${0%/*}/response.json\"\nexit 17\n");
    write("response.json", validResponse().dump());
    JavaSourceFacts failed;
    failed.load({source.string()});
    ASSERT_EQ(failed.diagnostics().size(), 1u);
    EXPECT_NE(failed.diagnostics()[0].message.find("code 17"), std::string::npos);
    EXPECT_TRUE(failed.symbols(source.string()).empty());
}

TEST_F(JavaSourceFactsProtocolTest, BoundsDiagnosticStreamBeforePublishingFacts) {
    auto script = root / (topo::platform::IsWindows ? "topo-extract-java.cmd" : "topo-extract-java");
    std::ofstream(script, std::ios::binary) << (topo::platform::IsWindows
        ? "@echo off\r\nmore > nul\r\necho diagnostic-output-that-exceeds-the-limit 1>&2\r\ntype \"%~dp0response.json\"\r\n"
        : "#!/bin/sh\ncat > /dev/null\nprintf 'diagnostic-output-that-exceeds-the-limit' >&2\ncat \"${0%/*}/response.json\"\n");
    write("response.json", validResponse().dump());
    JavaSourceFacts bounded({60000, 1024, 1024, 16});
    bounded.load({source.string()});
    ASSERT_EQ(bounded.diagnostics().size(), 1u);
    EXPECT_NE(bounded.diagnostics()[0].message.find("byte limit"), std::string::npos);
    EXPECT_TRUE(bounded.symbols(source.string()).empty());
    EXPECT_FALSE(bounded.auditFacts().complete);
    ASSERT_FALSE(bounded.auditFacts().unresolved.empty());
    EXPECT_NE(bounded.auditFacts().unresolved.front().find("byte limit"), std::string::npos);

    JavaSourceFacts withinLimit({60000, 1024, 1024, 1024});
    withinLimit.load({source.string()});
    EXPECT_TRUE(withinLimit.diagnostics().empty());
    ASSERT_EQ(withinLimit.symbols(source.string()).size(), 1u);
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
TEST_F(JavaSourceFactsTest, RejectsUnapprovedAuditModelBeforeParsingAndReporting) {
    auto project = root / "project";
    auto outside = root / "project-peer";
    fs::create_directories(project);
    fs::create_directories(outside);
    auto model = outside / "model.json";
    std::ofstream(model) << "{\"SYNTHETIC_SECRET\":invalid}";
    JavaSourceFacts facts;
    std::string capability;
    facts.requireAuditIsolation({}, capability);
    facts.configure(project.string(), model.string());
    facts.load({(project / "Probe.java").string()});
    auto diagnostics = facts.diagnostics();
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].message.find("outside authorized analysis input roots"), std::string::npos);
    EXPECT_EQ(diagnostics[0].message.find("SYNTHETIC_SECRET"), std::string::npos);
    auto audit = facts.auditFacts();
    EXPECT_TRUE(audit.modelPath.empty());
    auto report = makeAuditReport(project.string(), audit, {}, {}, {}, {});
    EXPECT_TRUE(report.at("inputs").empty());
}
#ifndef _WIN32
TEST_F(JavaSourceFactsTest, RejectsAuditModelSymlinkOutsideAuthorizedRoots) {
    auto project = root / "project";
    fs::create_directories(project);
    auto external = write("outside.json", "{\"SYNTHETIC_SECRET\":invalid}");
    auto link = project / "model.json";
    fs::create_symlink(external, link);
    JavaSourceFacts facts;
    std::string capability;
    facts.requireAuditIsolation({}, capability);
    facts.configure(project.string(), link.string());
    auto diagnostics = facts.diagnostics();
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_NE(diagnostics[0].message.find("outside authorized analysis input roots"), std::string::npos);
    EXPECT_EQ(diagnostics[0].message.find("SYNTHETIC_SECRET"), std::string::npos);
    EXPECT_TRUE(facts.auditFacts().modelPath.empty());
}
#endif
TEST_F(JavaSourceFactsTest, AcceptsAuditModelInProjectOrExplicitInputRoot) {
    auto project = root / "project";
    auto external = root / "external";
    fs::create_directories(project);
    fs::create_directories(external);
    auto source = project / "Probe.java";
    std::ofstream(source) << "class Probe {}";
    for (const auto& directory : {project, external}) {
        SCOPED_TRACE(directory.string());
        auto model = directory / "model.json";
        std::ofstream(model) << Json{{"files", {source.string()}}};
        JavaSourceFacts facts;
        std::string capability;
        facts.requireAuditIsolation({external.string()}, capability);
        facts.configure(project.string(), model.string());
        EXPECT_TRUE(facts.diagnostics().empty());
        EXPECT_EQ(facts.sourceFiles(), std::vector<std::string>{fs::canonical(source).string()});
        auto audit = facts.auditFacts();
        EXPECT_EQ(audit.modelPath, fs::canonical(model).string());
        auto report = makeAuditReport(project.string(), audit, {}, {}, {}, {});
        ASSERT_EQ(report.at("inputs").size(), 1u);
        EXPECT_EQ(report.at("inputs")[0].at("sha256"), topo::dist::sha256File(model.string()));
    }
}
TEST_F(JavaSourceFactsTest, OrdinaryChecksRetainExternalModelSupport) {
    auto project = root / "project";
    fs::create_directories(project);
    auto source = write("Probe.java", "class Probe {}");
    auto model = write("outside.json", Json{{"files", {source.string()}}}.dump());
    JavaSourceFacts facts;
    facts.configure(project.string(), model.string());
    EXPECT_TRUE(facts.diagnostics().empty());
    EXPECT_EQ(facts.sourceFiles(), std::vector<std::string>{fs::canonical(source).string()});
    EXPECT_EQ(facts.auditFacts().modelPath, fs::canonical(model).string());
}
TEST_F(JavaSourceFactsTest, AuditModelPublishesExternalSourcesOnlyWithInputRootAuthority) {
    auto project = root / "project";
    auto external = root / "external";
    fs::create_directories(project);
    fs::create_directories(external);
    auto localSource = project / "Local.java";
    auto outsideSource = external / "External.java";
    std::ofstream(localSource) << "class Local {}";
    std::ofstream(outsideSource) << "SYNTHETIC_SOURCE_SECRET";
    auto model = project / "model.json";
    std::ofstream(model) << Json{{"files", {localSource.string(), outsideSource.string()}}};
    for (const bool authorized : {false, true}) {
        SCOPED_TRACE(authorized);
        JavaSourceFacts facts;
        std::string capability;
        facts.requireAuditIsolation(authorized ? std::vector<std::string>{external.string()} : std::vector<std::string>{},
                                    capability);
        facts.configure(project.string(), model.string());
        auto audit = facts.auditFacts();
        auto report = makeAuditReport(project.string(), audit, {}, {}, facts.sourceFiles(), {});
        if (authorized) {
            EXPECT_TRUE(facts.diagnostics().empty());
            EXPECT_EQ(facts.sourceFiles(), (std::vector<std::string>{fs::canonical(localSource).string(),
                                                                   fs::canonical(outsideSource).string()}));
            EXPECT_EQ(report.at("inputs").size(), 3u);
        } else {
            facts.load({localSource.string(), outsideSource.string()});
            ASSERT_EQ(facts.diagnostics().size(), 1u);
            EXPECT_NE(facts.diagnostics()[0].message.find("outside authorized analysis input roots"), std::string::npos);
            EXPECT_TRUE(facts.sourceFiles().empty());
            EXPECT_TRUE(audit.modelPath.empty());
            EXPECT_TRUE(report.at("inputs").empty());
            EXPECT_EQ(report.dump().find("SYNTHETIC_SOURCE_SECRET"), std::string::npos);
        }
    }
}
TEST_F(JavaSourceFactsProtocolTest, InvalidConfiguredModelNeverStartsFallbackExtractor) {
    write("response.json", validResponse().dump());
    for (const std::string text : {"{", "{\"files\":false}"}) {
        SCOPED_TRACE(text);
        auto model = write("invalid.json", text);
        JavaSourceFacts facts;
        facts.configure(root.string(), model.string());
        facts.load({source.string()});
        facts.prepareFile(source.string());
        EXPECT_FALSE(fs::exists(root / "request.json"));
        EXPECT_TRUE(facts.symbols(source.string()).empty());
        ASSERT_EQ(facts.diagnostics().size(), 1u);
        EXPECT_TRUE(facts.auditFacts().modelPath.empty());
    }
}
#ifndef _WIN32
TEST_F(JavaSourceFactsTest, RejectedAuditModelDoesNotPublishFallbackSourceSymlinks) {
    auto project = root / "project";
    fs::create_directories(project / "src");
    auto external = write("External.java", "SYNTHETIC_SOURCE_SECRET");
    fs::create_symlink(external, project / "src" / "External.java");
    auto model = project / "invalid.json";
    std::ofstream(model) << "{";
    auto provider = createJavaAnalysisProvider();
    std::string capability;
    provider->configureAuditExecution({}, capability);
    provider->configureAnalysis(project.string(), model.string());
    const auto files = provider->collectSourceFiles(project.string(), {});
    EXPECT_TRUE(files.empty());
    const auto audit = provider->auditFacts(files);
    EXPECT_TRUE(audit.modelPath.empty());
    EXPECT_TRUE(makeAuditReport(project.string(), audit, {}, {}, files, {}).at("inputs").empty());
    ASSERT_EQ(provider->analysisDiagnostics().size(), 1u);
}
#endif
TEST_F(JavaSourceFactsProtocolTest, AuditKeepsEmptyModelSelectionWhileOrdinaryChecksScanSources) {
    fs::create_directories(root / "src");
    auto scanned = write("src/Scanned.java", "class Scanned {}");
    auto model = write("empty.json", "{\"files\":[]}");
    auto provider = createJavaAnalysisProvider();
    std::string capability;
    provider->configureAuditExecution({}, capability);
    provider->configureAnalysis(root.string(), model.string());
    EXPECT_TRUE(provider->collectSourceFiles(root.string(), {}).empty());
    EXPECT_FALSE(fs::exists(root / "request.json"));

    write("response.json", validResponse().dump());
    auto ordinary = createJavaAnalysisProvider();
    ordinary->configureAnalysis(root.string(), "");
    EXPECT_EQ(ordinary->collectSourceFiles(root.string(), {}), std::vector<std::string>{scanned.make_preferred().string()});
    EXPECT_TRUE(fs::exists(root / "request.json"));
}
TEST_F(JavaSourceFactsTest, AuditRequiresAnExplicitCompileModel) {
    auto file = write("Standalone.java", "class Standalone { static int run(){return 1;} }");
    auto provider = createJavaAnalysisProvider();
    auto audit = provider->auditFacts({file.string()});
    EXPECT_TRUE(audit.supported);
    EXPECT_FALSE(audit.complete);
    EXPECT_TRUE(std::any_of(audit.unresolved.begin(), audit.unresolved.end(), [](const auto& reason) {
        return reason.find("compile model not supplied") != std::string::npos;
    }));
    EXPECT_TRUE(provider->analysisDiagnostics().empty());
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

TEST_F(JavaSourceFactsTest, RealJdtFfmFactsKeepStaticBindingAndEscapeCapability) {
    auto file = write("NativeApi.java", R"JAVA(
import java.lang.foreign.*; import java.lang.invoke.*; import java.nio.file.Path;
class NativeApi {
  static final SymbolLookup LIB=SymbolLookup.libraryLookup(Path.of("/missing/audit-test.dylib"),Arena.global());
  static final MethodHandle H=Linker.nativeLinker().downcallHandle(LIB.find("topo_add").orElseThrow(),FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT,ValueLayout.JAVA_INT));
  static int run(int x)throws Throwable{return (int)H.invokeExact(x,x);}
  static int callback(int x){return x;}
  static MemorySegment callbackAddress()throws ReflectiveOperationException {
    var handle=MethodHandles.lookup().findStatic(NativeApi.class,"callback",MethodType.methodType(int.class,int.class));
    return Linker.nativeLinker().upcallStub(handle,FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_INT),Arena.global());
  }
}
)JAVA");
    auto provider = createJavaAnalysisProvider();
    auto model = write("java-project.json", Json{{"files", {file.string()}}, {"sourcepath", {root.string()}}, {"sourceLevel", "25"}}.dump());
    provider->configureAnalysis(root.string(), model.string());
    auto audit = provider->auditFacts({file.string()});
    EXPECT_TRUE(audit.supported);
    EXPECT_TRUE(audit.complete);
    EXPECT_TRUE(provider->analysisDiagnostics().empty());
    bool fixed = false;
    for (const auto& binding : audit.nativeBindings) if (binding.operation.find("invokeExact") != std::string::npos) {
        fixed = true;
        EXPECT_TRUE(binding.resolved);
        EXPECT_EQ(binding.symbol, "topo_add");
        EXPECT_EQ(binding.descriptor, "JAVA_INT(JAVA_INT,JAVA_INT)");
        EXPECT_EQ(binding.lookup, "library-path");
    }
    EXPECT_TRUE(fixed);
    auto calls = provider->createCallSiteExtractor()->extractCallSites(file.string());
    for (const std::string name : {"Linker::downcallHandle", "Linker::upcallStub", "SymbolLookup::libraryLookup"}) {
        bool escape = false;
        for (const auto& call : calls) if (call.calleePattern.find("java::lang::foreign::" + name) != std::string::npos) {
            escape = true; EXPECT_EQ(call.unsafeLevel, UnsafeLevel::Escape);
        }
        EXPECT_TRUE(escape) << name;
    }
    EXPECT_FALSE(fs::exists("/missing/audit-test.dylib"));
}

#include "extract/JavaAuditRuntime.h"
class JavaAuditRuntimeTest : public JavaSourceFactsTest {
protected:
    fs::path project, launcher, home, jar, java;
    Json request;
    void SetUp() override {
        JavaSourceFactsTest::SetUp();
        project = root / "subject";
        home = root / "runtime/jdk";
        launcher = root / "runtime/bin/topo-extract-java";
        jar = root / "runtime/lib/extractor.jar";
        for (const auto& dir : {project, home / "bin", home / "lib", launcher.parent_path(), jar.parent_path()}) fs::create_directories(dir);
        std::ofstream(project / "Input.java") << "class Input {}";
        std::ofstream(launcher) << "a wrapper that audit never runs";
        std::ofstream(jar) << "fixed extractor bytes";
        std::ofstream(home / "release") << "JAVA_VERSION=25";
        std::ofstream(home / "lib/modules") << "module bytes";
        std::ofstream(home / "lib/libjvm.dylib") << "native runtime bytes";
        std::ofstream(home / "lib/classes.jsa") << "shared classes";
        std::ofstream(home / "lib/jvm.cfg") << "JVM startup configuration";
        java = home / "bin" / (topo::platform::IsWindows ? "java.exe" : "java");
        std::ofstream(java) << "native executable bytes";
        descriptor();
        request = {{"projectRoot", project.string()}, {"files", { (project / "Input.java").string() }}};
    }
    void descriptor(const std::string& jarPath = "../lib/extractor.jar") {
        std::ofstream(launcher.parent_path() / "topo-extract-java.runtime.json")
            << Json{{"schemaVersion", 1}, {"jar", jarPath}, {"javaHome", "../jdk"}};
    }
};
TEST_F(JavaAuditRuntimeTest, ResolvesRelativeDescriptorAndBindsNativeRuntime) {
    auto runtime = resolveJavaAuditRuntime(launcher.string(), request, {});
    EXPECT_EQ(runtime.executable, fs::canonical(java).string());
    EXPECT_EQ(runtime.arguments.back(), fs::canonical(jar).string());
    EXPECT_EQ(runtime.policy.environment.size(), 2u);
    EXPECT_EQ(runtime.policy.allowedExecutables.size(), 1u);
    EXPECT_TRUE(runtime.inputDigests.count(fs::canonical(home / "lib/libjvm.dylib").string()));
    EXPECT_TRUE(runtime.inputDigests.count(fs::canonical(home / "lib/classes.jsa").string()));
    EXPECT_TRUE(runtime.inputDigests.count(fs::canonical(home / "lib/jvm.cfg").string()));
    auto inventory = runtime.runtimeInventory;
    std::ofstream(home / "lib/new-runtime-file") << "added";
    EXPECT_NE(inventory, javaAuditRuntimeInventory(fs::canonical(home)));
    auto before = resolveJavaAuditRuntime(launcher.string(), request, {}).inputDigests;
    std::ofstream(home / "lib/libjvm.dylib") << "changed bytes";
    std::ofstream(home / "lib/classes.jsa") << "changed CDS archive";
    auto after = resolveJavaAuditRuntime(launcher.string(), request, {}).inputDigests;
    for (const auto& path : {home / "lib/libjvm.dylib", home / "lib/classes.jsa"})
        EXPECT_NE(before.at(fs::canonical(path).string()), after.at(fs::canonical(path).string()));
}
TEST_F(JavaAuditRuntimeTest, ExternalInputsRequireAnExplicitReviewerRoot) {
    auto dependency = root / "dependency"; fs::create_directory(dependency);
    std::ofstream(dependency / "input.jar") << "bytes";
    request["classpath"] = {(dependency / "input.jar").string()};
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
    EXPECT_NO_THROW(resolveJavaAuditRuntime(launcher.string(), request, {dependency.string()}));
}
TEST_F(JavaAuditRuntimeTest, ProjectSymlinkDoesNotGrantExternalReadAuthority) {
    auto outside = root / "secret.jar"; std::ofstream(outside) << "secret";
    std::error_code error; createRuntimeSymlink(outside, project / "linked.jar", error);
    ASSERT_FALSE(error) << error.message();
    request["classpath"] = {(project / "linked.jar").string()};
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
}
TEST_F(JavaAuditRuntimeTest, RejectsProjectControlledRuntimeEvenWithInputGrant) {
    std::ofstream(project / "extractor.jar") << "project runtime";
    descriptor((project / "extractor.jar").string());
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {root.string()}), std::runtime_error);
}
TEST_F(JavaAuditRuntimeTest, DeclaredCompilerCannotSelectTheExecutionJVM) {
    auto compiler = root / "declared-compiler"; fs::create_directory(compiler);
    request["compiler"] = {{"javaHome", compiler.string()}, {"targetRelease", "25"}};
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
    EXPECT_EQ(resolveJavaAuditRuntime(launcher.string(), request, {compiler.string()}).executable,
              fs::canonical(java).string());
}
TEST_F(JavaAuditRuntimeTest, RequiresVersionedBoundedRuntimeMetadata) {
    auto metadata = launcher.parent_path() / "topo-extract-java.runtime.json";
    fs::remove(metadata);
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
    std::ofstream(metadata) << Json{{"schemaVersion", 99}, {"jar", "../lib/extractor.jar"}};
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
    std::ofstream(metadata) << std::string(16385, ' ');
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
}

TEST_F(JavaAuditRuntimeTest, RejectsRuntimeModuleSymlinkIntoSubject) {
    auto modules = home / "lib/modules";
    fs::remove(modules);
    std::ofstream(project / "modules") << "project bytes";
    std::error_code error; createRuntimeSymlink(project / "modules", modules, error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_THROW(resolveJavaAuditRuntime(launcher.string(), request, {}), std::runtime_error);
}
