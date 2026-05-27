// JavaDriver — compile .java sources and package .class files into a JAR.
//
// Uses javac/jar from JAVA_HOME or PATH, driven through
// topo::platform::runProcess() for cross-platform subprocess execution.

#include "JavaDriver.h"

#include "topo/Platform/Platform.h"
#include "topo/Platform/Process.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace topo::jvm {

// ---------------------------------------------------------------------------
// Input-trust boundary helpers
// ---------------------------------------------------------------------------

namespace {

constexpr char kClasspathSeparator =
#ifdef _WIN32
    ';';
#else
    ':';
#endif

bool containsControl(const std::string& s) {
    for (unsigned char c : s) {
        if (c == 0) return true; // NUL
        if (c < 0x20 && c != '\t') return true; // CR, LF, BEL, …
    }
    return false;
}

bool isJavaIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isJavaIdentifierPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool isJavaIdentifier(const std::string& s) {
    if (s.empty()) return false;
    if (!isJavaIdentifierStart(s.front())) return false;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (!isJavaIdentifierPart(s[i])) return false;
    }
    return true;
}

} // namespace

JavaInputError validateClasspath(const std::string& classpath,
                                  std::size_t* offendingElement,
                                  std::string* offendingValue) {
    if (classpath.empty()) return JavaInputError::None; // optional flag

    std::size_t idx = 0;
    std::size_t start = 0;
    while (start <= classpath.size()) {
        std::size_t end = classpath.find(kClasspathSeparator, start);
        std::string elem = (end == std::string::npos)
                               ? classpath.substr(start)
                               : classpath.substr(start, end - start);

        if (elem.empty()) {
            if (offendingElement) *offendingElement = idx;
            if (offendingValue) *offendingValue = elem;
            return JavaInputError::EmptyClassPathElement;
        }
        for (unsigned char c : elem) {
            if (c == 0) {
                if (offendingElement) *offendingElement = idx;
                if (offendingValue) *offendingValue = elem;
                return JavaInputError::ClassPathElementContainsNul;
            }
            if (c < 0x20 && c != '\t') {
                if (offendingElement) *offendingElement = idx;
                if (offendingValue) *offendingValue = elem;
                return JavaInputError::ClassPathElementContainsControlChar;
            }
        }

        if (end == std::string::npos) break;
        start = end + 1;
        ++idx;
    }
    return JavaInputError::None;
}

JavaInputError validateMainClass(const std::string& mainClass) {
    if (mainClass.empty()) return JavaInputError::None; // optional flag
    if (containsControl(mainClass)) {
        return JavaInputError::MainClassContainsControlChar;
    }
    // Split on '.', validate each segment as a Java identifier.
    std::size_t start = 0;
    while (true) {
        std::size_t dot = mainClass.find('.', start);
        std::string seg = (dot == std::string::npos)
                              ? mainClass.substr(start)
                              : mainClass.substr(start, dot - start);
        if (!isJavaIdentifier(seg)) {
            return JavaInputError::MainClassInvalidIdentifier;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return JavaInputError::None;
}

JavaInputError validateJavaHome(const std::string& javaHome) {
    if (javaHome.empty()) return JavaInputError::None; // env fallback path
    std::error_code ec;
    if (!fs::is_directory(javaHome, ec) || ec) {
        return JavaInputError::JavaHomeNotADirectory;
    }
    return JavaInputError::None;
}

namespace {

const char* javaInputErrorLabel(JavaInputError e) {
    switch (e) {
        case JavaInputError::None: return "ok";
        case JavaInputError::EmptyClassPathElement: return "empty classpath element";
        case JavaInputError::ClassPathElementContainsNul: return "classpath element contains NUL byte";
        case JavaInputError::ClassPathElementContainsControlChar: return "classpath element contains ASCII control char";
        case JavaInputError::MainClassInvalidIdentifier: return "mainClass is not a valid JLS-grammar identifier";
        case JavaInputError::MainClassContainsControlChar: return "mainClass contains ASCII control char (manifest-injection risk)";
        case JavaInputError::JavaHomeNotADirectory: return "javaHome is not an existing directory";
    }
    return "unknown";
}

} // namespace

// ---------------------------------------------------------------------------
// resolveJavaTool
// ---------------------------------------------------------------------------

std::string resolveJavaTool(const std::string& toolName, const std::string& javaHome) {
    std::string suffix(topo::platform::ExeSuffix);

    // 1. Explicit javaHome parameter
    if (!javaHome.empty()) {
        fs::path candidate = fs::path(javaHome) / "bin" / (toolName + suffix);
        if (fs::exists(candidate)) return candidate.string();
    }

    // 2. JAVA_HOME environment variable
    if (const char* env = std::getenv("JAVA_HOME")) {
        fs::path candidate = fs::path(env) / "bin" / (toolName + suffix);
        if (fs::exists(candidate)) return candidate.string();
    }

    // 3. Fall back to bare tool name (PATH lookup)
    return toolName + suffix;
}

// ---------------------------------------------------------------------------
// collectJavaFiles — recursively discover .java files under source dirs
// ---------------------------------------------------------------------------

static std::vector<std::string> collectJavaFiles(const std::vector<std::string>& sourceDirs) {
    std::vector<std::string> files;
    for (const auto& dir : sourceDirs) {
        std::error_code ec;
        for (auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".java") {
                files.push_back(entry.path().string());
            }
        }
        if (ec) {
            std::cerr << "warning: failed to traverse source directory '" << dir << "': " << ec.message() << "\n";
        }
    }
    return files;
}

// ---------------------------------------------------------------------------
// compileJava
// ---------------------------------------------------------------------------

JavaCompileResult compileJava(const std::vector<std::string>& sources,
                              const std::string& classpath,
                              const std::string& outputDir,
                              const std::string& targetVersion,
                              ::topo::BuildMode buildMode,
                              const std::string& javaHome,
                              bool verbose) {
    JavaCompileResult result;

    // Boundary checks — the driver is the single entry point that
    // translates BackendRequest JSON into argv for an external compiler.
    // Refuse hostile inputs cleanly instead of forwarding them.
    if (auto e = validateJavaHome(javaHome); e != JavaInputError::None) {
        std::cerr << "error: " << javaInputErrorLabel(e) << ": '" << javaHome << "'\n";
        result.exitCode = 1;
        return result;
    }
    {
        std::size_t badIdx = 0;
        std::string badVal;
        if (auto e = validateClasspath(classpath, &badIdx, &badVal); e != JavaInputError::None) {
            std::cerr << "error: " << javaInputErrorLabel(e)
                      << " (element index " << badIdx << ": '" << badVal << "')\n";
            result.exitCode = 1;
            return result;
        }
    }

    std::string javac = resolveJavaTool("javac", javaHome);

    // Discover .java files recursively from source directories
    std::vector<std::string> javaFiles = collectJavaFiles(sources);
    if (javaFiles.empty()) {
        std::cerr << "error: no .java source files found\n";
        result.exitCode = 1;
        return result;
    }

    // Ensure output directory exists
    std::error_code ec;
    fs::create_directories(outputDir, ec);

    // Build argument list
    std::vector<std::string> args;
    args.push_back("-d");
    args.push_back(outputDir);

    // Dev mode emits javac `-g` (LineNumberTable + LocalVariableTable +
    // SourceFile + source debug attributes) so JDWP-driven debuggers can
    // resolve `--break Main.java:N` to real PCs and the JDI frame can name
    // visible locals. Mirrors CppDriver.cpp:46 (`-g`) and the
    // RustDriver `-Cdebuginfo=2` insertion — without it `topo debug query`
    // against a Java fixture would fall back to method-entry breakpoints
    // and `frame.visibleVariables()` returns empty. Aggressive strips the
    // debug payload for release artifacts.
    if (buildMode != ::topo::BuildMode::Aggressive) {
        args.push_back("-g");
    }

    if (!classpath.empty()) {
        args.push_back("--class-path");
        args.push_back(classpath);
    }

    if (!targetVersion.empty()) {
        args.push_back("--release");
        args.push_back(targetVersion);
    }

    for (const auto& f : javaFiles) {
        args.push_back(f);
    }

    if (verbose) {
        std::cerr << "  " << javac;
        for (const auto& a : args)
            std::cerr << " " << a;
        std::cerr << "\n";
    }

    auto pr = topo::platform::runProcess(javac, args, verbose);
    result.exitCode = pr.exitCode;
    if (result.exitCode == 0) {
        result.classDir = outputDir;
    }
    return result;
}

// ---------------------------------------------------------------------------
// packageJar
// ---------------------------------------------------------------------------

JavaPackageResult packageJar(const std::string& classDir,
                             const std::string& outputJar,
                             const std::string& mainClass,
                             const std::string& javaHome,
                             bool verbose) {
    JavaPackageResult result;

    // Boundary checks. `mainClass` is the manifest-injection vector —
    // a value ending in `\nClass-Path: file:///etc/shadow` would write
    // a second header into the resulting JAR's MANIFEST.MF. The JLS
    // identifier grammar reject closes that vector and also rejects
    // strings starting with `-` (which `jar` would interpret as a flag).
    if (auto e = validateMainClass(mainClass); e != JavaInputError::None) {
        std::cerr << "error: " << javaInputErrorLabel(e) << ": '" << mainClass << "'\n";
        result.exitCode = 1;
        return result;
    }
    if (auto e = validateJavaHome(javaHome); e != JavaInputError::None) {
        std::cerr << "error: " << javaInputErrorLabel(e) << ": '" << javaHome << "'\n";
        result.exitCode = 1;
        return result;
    }

    std::string jar = resolveJavaTool("jar", javaHome);

    std::vector<std::string> args;
    args.push_back("--create");
    args.push_back("--file");
    args.push_back(outputJar);

    if (!mainClass.empty()) {
        args.push_back("--main-class");
        args.push_back(mainClass);
    }

    args.push_back("-C");
    args.push_back(classDir);
    args.push_back(".");

    if (verbose) {
        std::cerr << "  " << jar;
        for (const auto& a : args)
            std::cerr << " " << a;
        std::cerr << "\n";
    }

    auto pr = topo::platform::runProcess(jar, args, verbose);
    result.exitCode = pr.exitCode;
    if (result.exitCode == 0) {
        result.jarPath = outputJar;
    }
    return result;
}

} // namespace topo::jvm
