#ifndef TOPO_JVM_JAVADRIVER_H
#define TOPO_JVM_JAVADRIVER_H

#include "topo/Basic/BuildTypes.h"
#include "topo/Build/ArtifactEvidence.h"

#include <string>
#include <vector>

namespace topo::jvm {

// The caller supplies roots for reusable evidence and verified parent stages.
// With no options the driver captures local command evidence using inferred
// input/output roots; that evidence remains incomplete and unauthenticated.
struct JavaEvidenceOptions {
    build::evidence::RootMap roots;
    std::vector<build::evidence::Manifest> parents;
    std::string manifestPath;
    std::vector<std::string> extraInputs;
};

struct JavaCompileResult {
    int exitCode = 0;
    std::string classDir; // output directory containing .class files
    std::string evidencePath;
    build::evidence::RootMap evidenceRoots;
    std::vector<std::string> evidenceDiagnostics;
};

struct JavaPackageResult {
    int exitCode = 0;
    std::string jarPath;
    std::string evidencePath;
    build::evidence::RootMap evidenceRoots;
    std::vector<std::string> evidenceDiagnostics;
};

/// Compile .java sources to .class files using javac.
///
/// `buildMode` controls debug-info emission: Dev passes `-g` so JDWP-driven
/// tools (topo-debug-java, IDE remote debug, JFR diagnostics) can resolve
/// source lines + local variable names against the produced bytecode.
/// Aggressive omits `-g` so release artifacts ship without debug payloads.
JavaCompileResult compileJava(const std::vector<std::string>& sources,
                              const std::string& classpath,
                              const std::string& outputDir,
                              const std::string& targetVersion,
                              ::topo::BuildMode buildMode,
                              const std::string& javaHome,
                              bool verbose,
                              const JavaEvidenceOptions* evidence = nullptr);

/// Package .class files into a JAR.
JavaPackageResult packageJar(const std::string& classDir,
                             const std::string& outputJar,
                             const std::string& mainClass,
                             const std::string& javaHome,
                             bool verbose,
                             const JavaEvidenceOptions* evidence = nullptr);

/// Resolve the path to a Java tool (javac, jar, java) from javaHome or JAVA_HOME.
std::string resolveJavaTool(const std::string& toolName, const std::string& javaHome);
/// The selected source files used by both the actual compiler and check model.
std::vector<std::string> collectJavaSourceFiles(const std::vector<std::string>& sources);

// ---------------------------------------------------------------------------
// JavaDriver input-trust boundary
// ---------------------------------------------------------------------------
//
// Topo's threat model treats `Topo.toml` and the BackendRequest
// `backendExtras` fields (`javaHome`, `classpath`, `jvmArgs`,
// `mainClass`) as user-editable input. A poisoned Topo.toml downloaded
// alongside a third-party sample project becomes the attack vector the
// moment a developer runs `topo build`. javac handles its own argv so
// the immediate compile call is safe under runProcess, but the same
// `mainClass` flows into the `--main-class` flag and into the resulting
// JAR manifest's `Main-Class:` header — an embedded newline injects a
// second manifest header (`\nClass-Path: file:///etc/shadow`). The
// classpath string flows into JDWP-launcher shell wrappers downstream.
//
// The helpers below validate at the driver's input boundary and let the
// caller refuse cleanly with `JavaInputError::Invalid` instead of
// forwarding hostile bytes.

enum class JavaInputError {
    None = 0,
    EmptyClassPathElement,
    ClassPathElementContainsNul,
    ClassPathElementContainsControlChar,
    MainClassInvalidIdentifier,
    MainClassContainsControlChar,
    JavaHomeNotADirectory,
};

/// Split a `classpath` string on the platform separator (`;` on Windows,
/// `:` elsewhere) and validate every element.
///
/// Rejects:
///   - empty element (`a::b` on POSIX -> middle element is "")
///   - NUL byte anywhere in the element (would terminate the argv item
///     and reach the kernel as a truncated path)
///   - any other ASCII control character (CR / LF could inject a second
///     line into a launcher script the C++ caller might later quote)
///
/// On success returns `JavaInputError::None`. On failure returns the
/// first error class and writes the offending element index + value
/// into `*offendingElement` / `*offendingValue` (either may be null).
JavaInputError validateClasspath(const std::string& classpath,
                                  std::size_t* offendingElement = nullptr,
                                  std::string* offendingValue = nullptr);

/// Validate a Java main-class name against the JLS identifier grammar
/// (`(Letter)(Letter|Digit)*` joined by `.`).
///
/// Rejects empty / leading or trailing `.` / consecutive `.` / any
/// character outside `[A-Za-z0-9._$]` (Java identifiers permit `$` and
/// `_`). Rejects any embedded ASCII control character — `\n` would
/// inject a second `Main-Class:` line into the JAR manifest.
JavaInputError validateMainClass(const std::string& mainClass);

/// True iff `javaHome` is a non-empty existing directory. The full
/// "looks like a JDK install" check (presence of `release`,
/// `bin/javac<suffix>`) lives in `resolveJavaTool`; this boundary
/// helper is the cheap pre-check the driver runs before consulting the
/// directory.
JavaInputError validateJavaHome(const std::string& javaHome);

} // namespace topo::jvm

#endif // TOPO_JVM_JAVADRIVER_H
