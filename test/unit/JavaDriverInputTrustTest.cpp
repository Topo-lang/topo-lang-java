// Regression tests for JavaDriver's input-trust boundary helpers — guards
// against unsanitised classpath / argument injection.
//
// Verifies the validation helpers reject hostile values (NUL bytes,
// control characters, JLS-invalid identifiers, manifest-injection
// payloads, non-directory javaHome) and accept the common shapes
// callers actually use.

#include "JavaDriver.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using topo::jvm::JavaInputError;
using topo::jvm::validateClasspath;
using topo::jvm::validateJavaHome;
using topo::jvm::validateMainClass;

namespace fs = std::filesystem;

// --- validateClasspath ---------------------------------------------------

TEST(JavaDriverInputTrust, EmptyClasspathIsOk) {
    EXPECT_EQ(validateClasspath(""), JavaInputError::None);
}

TEST(JavaDriverInputTrust, SingleElementClasspathIsOk) {
    EXPECT_EQ(validateClasspath("build/classes"), JavaInputError::None);
}

TEST(JavaDriverInputTrust, MultiElementClasspathIsOk) {
#ifdef _WIN32
    EXPECT_EQ(validateClasspath("a;b;c"), JavaInputError::None);
#else
    EXPECT_EQ(validateClasspath("a:b:c"), JavaInputError::None);
#endif
}

TEST(JavaDriverInputTrust, RejectsEmptyElement) {
    std::size_t badIdx = 99;
#ifdef _WIN32
    EXPECT_EQ(validateClasspath("a;;b", &badIdx), JavaInputError::EmptyClassPathElement);
#else
    EXPECT_EQ(validateClasspath("a::b", &badIdx), JavaInputError::EmptyClassPathElement);
#endif
    EXPECT_EQ(badIdx, 1u);
}

TEST(JavaDriverInputTrust, RejectsNulByte) {
    std::string cp = "a";
    cp.push_back('\0');
    cp += "bad";
    EXPECT_EQ(validateClasspath(cp), JavaInputError::ClassPathElementContainsNul);
}

TEST(JavaDriverInputTrust, RejectsControlChar) {
#ifdef _WIN32
    EXPECT_EQ(validateClasspath("ok;path\nwith-newline"),
              JavaInputError::ClassPathElementContainsControlChar);
#else
    EXPECT_EQ(validateClasspath("ok:path\nwith-newline"),
              JavaInputError::ClassPathElementContainsControlChar);
#endif
}

// --- validateMainClass ---------------------------------------------------

TEST(JavaDriverInputTrust, EmptyMainClassIsOk) {
    EXPECT_EQ(validateMainClass(""), JavaInputError::None);
}

TEST(JavaDriverInputTrust, AcceptsSimpleAndQualifiedNames) {
    EXPECT_EQ(validateMainClass("Main"), JavaInputError::None);
    EXPECT_EQ(validateMainClass("dev.topo.App"), JavaInputError::None);
    EXPECT_EQ(validateMainClass("a.b_c.D$Inner"), JavaInputError::None);
    EXPECT_EQ(validateMainClass("_Foo"), JavaInputError::None);
}

TEST(JavaDriverInputTrust, RejectsLeadingDigit) {
    EXPECT_EQ(validateMainClass("1Bad"), JavaInputError::MainClassInvalidIdentifier);
    EXPECT_EQ(validateMainClass("ok.1Bad"), JavaInputError::MainClassInvalidIdentifier);
}

TEST(JavaDriverInputTrust, RejectsLeadingOrConsecutiveDots) {
    EXPECT_EQ(validateMainClass(".dev.topo"), JavaInputError::MainClassInvalidIdentifier);
    EXPECT_EQ(validateMainClass("dev..topo"), JavaInputError::MainClassInvalidIdentifier);
    EXPECT_EQ(validateMainClass("dev.topo."), JavaInputError::MainClassInvalidIdentifier);
}

TEST(JavaDriverInputTrust, RejectsLeadingDash) {
    // A `-`-leading value would be interpreted as a flag by `jar`.
    EXPECT_EQ(validateMainClass("-Main"), JavaInputError::MainClassInvalidIdentifier);
}

TEST(JavaDriverInputTrust, RejectsManifestInjectionPayload) {
    // The audit's worked example: a newline embedded in mainClass would
    // inject a second `Class-Path:` header into MANIFEST.MF.
    EXPECT_EQ(validateMainClass("Main\nClass-Path: file:///etc/shadow"),
              JavaInputError::MainClassContainsControlChar);
}

TEST(JavaDriverInputTrust, RejectsShellMetacharacters) {
    EXPECT_EQ(validateMainClass("a;b"), JavaInputError::MainClassInvalidIdentifier);
    EXPECT_EQ(validateMainClass("a b"), JavaInputError::MainClassInvalidIdentifier);
}

// --- validateJavaHome ----------------------------------------------------

TEST(JavaDriverInputTrust, EmptyJavaHomeIsOk) {
    EXPECT_EQ(validateJavaHome(""), JavaInputError::None);
}

TEST(JavaDriverInputTrust, RejectsNonDirectoryJavaHome) {
    fs::path tmp = fs::temp_directory_path() / "javadriver-input-trust-test-not-a-dir";
    {
        std::ofstream f(tmp);
        f << "this is a regular file";
    }
    EXPECT_EQ(validateJavaHome(tmp.string()), JavaInputError::JavaHomeNotADirectory);
    std::error_code ec;
    fs::remove(tmp, ec);
}

TEST(JavaDriverInputTrust, AcceptsExistingDirectoryJavaHome) {
    fs::path tmp = fs::temp_directory_path() / "javadriver-input-trust-test-dir";
    fs::create_directories(tmp);
    EXPECT_EQ(validateJavaHome(tmp.string()), JavaInputError::None);
    std::error_code ec;
    fs::remove_all(tmp, ec);
}
