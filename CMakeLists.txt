cmake_minimum_required(VERSION 3.20)

project(topo-lang-java
    VERSION 0.1.0
    DESCRIPTION "Topo Java language support — check, build/transpile, LSP bridge, plugin, JVM runtime jar"
    HOMEPAGE_URL "https://github.com/topo-lang/topo-lang-java"
    LANGUAGES CXX
)

# topo-lang-java: Java language support (analysis + driver + LSP bridge +
# JVM-side runtime/extract/debug/profile jars built via Gradle).
#
# Layering:
#   - C++ libs (analysis, runner, lsp, transpile, init, driver, plugin) ship
#     by default; they only need topo-core + topo-lang and a host C++ compiler.
#   - JVM-side artifacts (Gradle-built topo-runtime.jar / topo-extract-java.jar
#     / topo-debug-java.jar / topo-profile-jvm-jfr.jar / SpansDemo.class) are
#     gated behind TOPO_LANG_JAVA_ENABLE_JVM_BACKEND. They require a working
#     JDK (TOPO_GRADLE_JAVA_HOME + Gradle wrapper).

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TopoLangJavaCompilerFlags)

# ── Upstream Topo packages ────────────────────────────
# Dual-mode: when this clone is consumed via add_subdirectory() from a
# parent meta-repo that already brought in topo-core / topo-lang, their
# exported targets are already live — skip find_package() in that case.
if(NOT TARGET topo::core::TopoBasic)
    find_package(topo-core CONFIG REQUIRED)
endif()
if(NOT TARGET topo::lang::TopoLang)
    find_package(topo-lang CONFIG REQUIRED)
endif()

# ── External dependencies ─────────────────────────────
find_package(nlohmann_json CONFIG REQUIRED)

# ── Optional JVM-backed toolchain layer ───────────────
# Gradle-built jars (runtime, extract, debug, profile bridge) and the
# javac-driven span/JFR fixtures require a working JDK + Gradle wrapper.
# Default OFF so machines without a JDK still configure cleanly; the C++
# libraries are always built. Mirrors topo-lang-rust's TOPO_LANG_RUST_ENABLE_LLVM
# gate (which defers the LLVM/LLDB-coupled subset).
option(TOPO_LANG_JAVA_ENABLE_JVM_BACKEND
    "Enable JVM-side artifacts (Gradle-built jars, topo-debug-java, topo-profile fixtures)"
    OFF)

if(TOPO_LANG_JAVA_ENABLE_JVM_BACKEND)
    if(NOT TOPO_GRADLE_JAVA_HOME)
        message(WARNING "TOPO_LANG_JAVA_ENABLE_JVM_BACKEND=ON but TOPO_GRADLE_JAVA_HOME "
                        "is unset; Gradle invocations will fall back to ambient JAVA_HOME.")
    endif()
    if(NOT TOPO_GRADLEW_CMD)
        # Default to the Gradle wrapper that ships with each Gradle subproject.
        # Downstream consumers can override with an absolute path to a
        # different gradle binary.
        if(WIN32)
            set(TOPO_GRADLEW_CMD "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gradlew.bat"
                CACHE STRING "Gradle wrapper invocation")
        else()
            set(TOPO_GRADLEW_CMD "${CMAKE_CURRENT_SOURCE_DIR}/runtime/gradlew"
                CACHE STRING "Gradle wrapper invocation")
        endif()
    endif()
endif()

# ── Subdirs (C++ libs always; JVM artifacts gated) ───────────
# Order: lsp/transpile/runner first since analysis links them.
add_subdirectory(topo-lsp)
add_subdirectory(topo-transpile)
add_subdirectory(topo-check/runner)
add_subdirectory(topo-check/analysis)
add_subdirectory(topo-build)
add_subdirectory(topo-init)
add_subdirectory(topo-lang)

if(TOPO_LANG_JAVA_ENABLE_JVM_BACKEND)
    if(TOPO_GRADLE_JAVA_HOME)
        set(_JAVA_GRADLE_ENV ${CMAKE_COMMAND} -E env "JAVA_HOME=${TOPO_GRADLE_JAVA_HOME}")
    else()
        set(_JAVA_GRADLE_ENV ${CMAKE_COMMAND} -E env)
    endif()

    # Extra args appended to every CMake-driven Gradle invocation. Empty by
    # default so a local build keeps the daemon (org.gradle.daemon=true in the
    # subprojects' gradle.properties, defended there as the main local speed-up).
    # CI sets "--no-daemon --console=plain": the daemon gives nothing on an
    # ephemeral runner torn down after one build, and its cold bootstrap hangs
    # the Windows benchmark leg. Mirrors topo-jvm's TOPO_JVM_GRADLE_ARGS.
    set(TOPO_LANG_JAVA_GRADLE_ARGS "" CACHE STRING
        "Extra args appended to every Gradle invocation (e.g. --no-daemon --console=plain)")
    separate_arguments(_JAVA_GRADLE_EXTRA_ARGS NATIVE_COMMAND "${TOPO_LANG_JAVA_GRADLE_ARGS}")

    # Build topo-extract-java.jar via gradle.
    # Use --rerun-tasks=false (default) so Gradle's own up-to-date checks keep
    # configure-build incremental; the file-level DEPENDS captures the Java
    # sources so CMake re-invokes Gradle whenever a source changes.
    set(_EXTRACT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/topo-check/extractor)
    set(_EXTRACT_JAR ${_EXTRACT_DIR}/build/libs/topo-extract-java-0.1.0.jar)
    file(GLOB_RECURSE _EXTRACT_JAVA_SRC CONFIGURE_DEPENDS
        ${_EXTRACT_DIR}/src/main/java/*.java)
    add_custom_command(
        OUTPUT ${_EXTRACT_JAR}
        COMMAND ${_JAVA_GRADLE_ENV} ${TOPO_GRADLEW_CMD} jar ${_JAVA_GRADLE_EXTRA_ARGS}
        WORKING_DIRECTORY ${_EXTRACT_DIR}
        DEPENDS ${_EXTRACT_JAVA_SRC} ${_EXTRACT_DIR}/build.gradle.kts
        COMMENT "Building topo-extract-java"
    )
    add_custom_target(topo-extract-java ALL DEPENDS ${_EXTRACT_JAR})

    # Build topo-runtime.jar from Java SDK runtime sources
    set(_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
    set(_RUNTIME_JAR ${_RUNTIME_DIR}/build/libs/topo-runtime.jar)
    file(GLOB_RECURSE _RUNTIME_JAVA_SRC CONFIGURE_DEPENDS
        ${_RUNTIME_DIR}/src/main/java/*.java)
    add_custom_command(
        OUTPUT ${_RUNTIME_JAR}
        COMMAND ${_JAVA_GRADLE_ENV} ${TOPO_GRADLEW_CMD} jar ${_JAVA_GRADLE_EXTRA_ARGS}
        WORKING_DIRECTORY ${_RUNTIME_DIR}
        DEPENDS ${_RUNTIME_JAVA_SRC} ${_RUNTIME_DIR}/build.gradle.kts
        COMMENT "Building topo-runtime-java"
    )
    add_custom_target(topo-runtime-java ALL DEPENDS ${_RUNTIME_JAR})

    # ── topo-extract-java launcher ──────────────────────────────────
    # The C++ side (TranspileDriver::extractFunctions) resolves the
    # extractor through PipedProcess::start("topo-extract-java", ...) — so the
    # binary must be on PATH at runtime. Generate a posix sh launcher (and a
    # Windows .cmd) that forwards stdin/stdout to `java -jar ... .jar`.
    # The launcher is generated at configure time with the *install* layout
    # baked in so the installed copy resolves the staged jar at
    # share/topo-lang-java/lib/topo-extract-java-0.1.0.jar. A build-tree copy
    # also lives in the build directory for in-tree tests.
    set(_EXTRACT_LAUNCHER_BUILD ${CMAKE_BINARY_DIR}/topo-extract-java)
    set(_EXTRACT_LAUNCHER_BUILD_CMD ${CMAKE_BINARY_DIR}/topo-extract-java.cmd)
    set(_EXTRACT_LAUNCHER_INSTALL_SH
        ${CMAKE_CURRENT_BINARY_DIR}/install/topo-extract-java)
    set(_EXTRACT_LAUNCHER_INSTALL_CMD
        ${CMAKE_CURRENT_BINARY_DIR}/install/topo-extract-java.cmd)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/install)

    # Build-tree launcher — references the Gradle output directly so an
    # in-tree test (extractor-fidelity) can invoke `topo-extract-java` from
    # PATH-prepended-with-build by just adding the build dir.
    file(WRITE ${_EXTRACT_LAUNCHER_BUILD}
"#!/bin/sh
# topo-extract-java launcher (build-tree).
# Forwards stdin/stdout to the Gradle-built extractor jar.
exec \"${TOPO_GRADLE_JAVA_HOME}/bin/java\" -jar \"${_EXTRACT_JAR}\" \"$@\"
")
    file(CHMOD ${_EXTRACT_LAUNCHER_BUILD} PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    file(WRITE ${_EXTRACT_LAUNCHER_BUILD_CMD}
"@echo off
\"${TOPO_GRADLE_JAVA_HOME}/bin/java.exe\" -jar \"${_EXTRACT_JAR}\" %*
")

    # Install-tree launcher — uses a relative path from <prefix>/bin to
    # <prefix>/share/topo-lang-java/lib/topo-extract-java-0.1.0.jar so the
    # prefix can be relocated. JAVA_HOME (or `java` on PATH) is the runtime
    # JRE; we deliberately do NOT bake TOPO_GRADLE_JAVA_HOME into the
    # installed launcher because the install prefix outlives the build
    # machine's JDK location.
    file(WRITE ${_EXTRACT_LAUNCHER_INSTALL_SH}
"#!/bin/sh
# topo-extract-java launcher (installed).
# Resolves the jar relative to this script so the prefix is relocatable.
# Picks `java` from \$JAVA_HOME/bin/java or PATH.
SCRIPT_DIR=$(cd \"$(dirname \"$0\")\" && pwd)
JAR=\"$SCRIPT_DIR/../share/topo-lang-java/lib/topo-extract-java-0.1.0.jar\"
if [ -n \"$JAVA_HOME\" ] && [ -x \"$JAVA_HOME/bin/java\" ]; then
    JAVA=\"$JAVA_HOME/bin/java\"
else
    JAVA=\"java\"
fi
exec \"$JAVA\" -jar \"$JAR\" \"$@\"
")
    file(CHMOD ${_EXTRACT_LAUNCHER_INSTALL_SH} PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    file(WRITE ${_EXTRACT_LAUNCHER_INSTALL_CMD}
"@echo off
setlocal
set SCRIPT_DIR=%~dp0
set JAR=%SCRIPT_DIR%..\\share\\topo-lang-java\\lib\\topo-extract-java-0.1.0.jar
if defined JAVA_HOME (
    \"%JAVA_HOME%\\bin\\java.exe\" -jar \"%JAR%\" %*
) else (
    java -jar \"%JAR%\" %*
)
")

    # ── JVM artifact install rules ──────────────────────────────────
    # Layout under <prefix>:
    #   bin/topo-extract-java               POSIX launcher (PATH-resolvable)
    #   bin/topo-extract-java.cmd           Windows launcher
    #   share/topo-lang-java/lib/*.jar      All four Gradle-built jars
    #
    # Naming mirrors topo-jvm's share/topo-jvm/lib/ for symmetry. The four
    # jars install with their canonical filenames (no version mangling) so
    # downstream find_path() lookups by name work directly.
    install(FILES ${_EXTRACT_JAR}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/topo-lang-java/lib
        COMPONENT topo-lang-java-jvm)
    install(FILES ${_RUNTIME_JAR}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/topo-lang-java/lib
        COMPONENT topo-lang-java-jvm)
    install(PROGRAMS ${_EXTRACT_LAUNCHER_INSTALL_SH}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT topo-lang-java-jvm)
    install(FILES ${_EXTRACT_LAUNCHER_INSTALL_CMD}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT topo-lang-java-jvm)

    add_subdirectory(topo-debug)
    add_subdirectory(topo-profile)
endif()

# ── Tests ─────────────────────────────────────────────
option(TOPO_LANG_JAVA_BUILD_TESTS "Build topo-lang-java tests" ON)
if(TOPO_LANG_JAVA_BUILD_TESTS AND (PROJECT_IS_TOP_LEVEL OR TOPO_META_DRIVING_TESTS))
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
    add_subdirectory(test)
endif()

# ── Install / export ──────────────────────────────────
install(EXPORT topo-lang-javaTargets
    FILE topo-lang-javaTargets.cmake
    NAMESPACE topo::lang-java::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/topo-lang-java
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/topo-lang-javaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/topo-lang-javaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/topo-lang-java
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/topo-lang-javaConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/topo-lang-javaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/topo-lang-javaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/topo-lang-java
)
