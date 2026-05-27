# topo-lang-java -- Java Language Support

Java language analysis, extraction, compilation driver, LSP bridge,
JVM-side runtime jar, and JavaPlugin for the Topo toolchain.

## Structure

Second-level directories are named after the `topo-<tool>` they serve, so the mapping from code to top-level component is explicit.

| Directory | Serves | Purpose |
|-----------|--------|---------|
| runtime/ | user code | Java runtime library (Parallel, Pipeline, Adaptive, Observe, Arena, annotations) — Gradle project |
| topo-check/analysis/ | topo-check | JavaAnalysisProvider + extractors + safety catalog + stub generator |
| topo-check/runner/ | topo-check | JavaCheckRunner -- language-specific check orchestration |
| topo-check/extractor/ | topo-check | Gradle-built standalone symbol/body extractor (`topo-extract-java`) |
| topo-build/ | topo-build | JavaDriver -- javac/jar subprocess invocation |
| topo-init/ | topo-init | Java project template provider |
| topo-lsp/ | topo-lsp | JdtBridge -- proxies Eclipse JDT Language Server for IDE integration |
| topo-transpile/ | topo-transpile | JavaEmitter -- AST → Java source |
| topo-debug/ | topo-debug | JDWP/JDI adapter for Java binaries |
| topo-profile/ | topo-profile | Java span-emitter fixture + JFR bridge |
| topo-lang/ | topo-lang | JavaPlugin -- registers all components with the language-plugin framework |
| test/ | — | Unit tests (JavaStubGenerator, JavaEmitterStdlib, JavaDriverInputTrust, JavaEmitterRealJavac) |
| examples/ | — | quickstart and showcase projects |

## Build

Standalone build expects two upstream Topo packages installed and
discoverable via `CMAKE_PREFIX_PATH`:

- `topo-core` (built with `TOPO_CORE_WITH_LANG=ON`)
- `topo-lang`

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_PREFIX_PATH=<topo-install-prefix> \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The C++ libraries (analysis, runner, transpile, lsp, init, driver,
plugin) build by default. To opt into the JVM-backed toolchain layer
(Gradle-built jars `topo-runtime.jar` / `topo-extract-java` /
`topo-debug-java`, the JFR bridge, and span/JFR fixtures plus the
extractor-fidelity + check E2E tests that consume them), configure with
`-DTOPO_LANG_JAVA_ENABLE_JVM_BACKEND=ON` and set
`-DTOPO_GRADLE_JAVA_HOME=<JDK 17 home>`.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The `topo-lang-java-emitter-realjavac-tests` suite auto-skips when `javac`
is not on `PATH` (or when `TOPO_GRADLE_JAVA_HOME` does not point at a
JDK); status messages spell out the SKIP reason at configure time.

## Downstream usage

```cmake
find_package(topo-lang-java CONFIG REQUIRED)
target_link_libraries(<target> PRIVATE topo::lang-java::TopoJavaAnalysis)
```

The exported library targets (always available) are:

- `topo::lang-java::TopoJavaAnalysis`
- `topo::lang-java::TopoJavaCheck`
- `topo::lang-java::TopoJavaInit`
- `topo::lang-java::TopoJavaLSP`
- `topo::lang-java::TopoJavaTranspile`
- `topo::lang-java::TopoJavaDriver`
- `topo::lang-java::TopoJavaPlugin`

When configured with `-DTOPO_LANG_JAVA_ENABLE_JVM_BACKEND=ON`, the
Gradle-built jars (`topo-runtime.jar`, `topo-extract-java`,
`topo-debug-java`, JFR bridge) also ship alongside.
