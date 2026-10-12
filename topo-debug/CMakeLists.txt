# topo-debug-java — JDWP/JDI Extract adapter for Java binaries.
#
# Speaks the debug wire protocol on stdout/stdin, driving a target
# Java main class through com.sun.jdi.* (the JDK's built-in jdk.jdi module).
# Mirrors topo-lang-cpp/topo-debug's role but uses JDI in place of liblldb,
# so there's no LLVM dependency — only a working JDK to compile + run.
#
# Gates the entire subtree on TOPO_GRADLE_JAVA_HOME so machines without a JDK
# still configure cleanly; downstream e2e tests check `if(TARGET topo-debug-java)`.

if(NOT TOPO_GRADLE_JAVA_HOME)
    message(STATUS "topo-debug-java: TOPO_GRADLE_JAVA_HOME not set — skipping Java debug adapter.")
    return()
endif()

set(_JAVA_GRADLE_ENV ${CMAKE_COMMAND} -E env "JAVA_HOME=${TOPO_GRADLE_JAVA_HOME}")

# ── Build topo-debug-java.jar via gradle ────────────────────────────────────
set(_DEBUG_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(_DEBUG_JAR ${_DEBUG_DIR}/build/libs/topo-debug-java-0.1.0.jar)
file(GLOB_RECURSE _DEBUG_JAVA_SRC CONFIGURE_DEPENDS
    ${_DEBUG_DIR}/src/main/java/*.java)
add_custom_command(
    OUTPUT ${_DEBUG_JAR}
    COMMAND ${_JAVA_GRADLE_ENV} ${TOPO_GRADLEW_CMD} jar ${_JAVA_GRADLE_EXTRA_ARGS}
    WORKING_DIRECTORY ${_DEBUG_DIR}
    DEPENDS ${_DEBUG_JAVA_SRC} ${_DEBUG_DIR}/build.gradle.kts
    COMMENT "Building topo-debug-java"
)

# ── Compile the TinyMatrix fixture .class ────────────────────────────────────
# The fixture sits in the default package; place its .class in a per-build dir
# so the launcher invocation references a clean classpath root.
set(_FIXTURE_SRC ${_DEBUG_DIR}/test/fixtures/TinyMatrix.java)
set(_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixture_classes)
set(_FIXTURE_CLASS ${_FIXTURE_DIR}/TinyMatrix.class)
file(MAKE_DIRECTORY ${_FIXTURE_DIR})
# Set the cache var BEFORE add_custom_target so other dirs can pick it up.
set(TOPO_DEBUG_JAVA_FIXTURE_DIR ${_FIXTURE_DIR}
    CACHE INTERNAL "TinyMatrix.class directory for e2e tests")
set(TOPO_DEBUG_JAVA_FIXTURE_SOURCE ${_FIXTURE_SRC}
    CACHE INTERNAL "TinyMatrix.java source path for --break sites")
add_custom_command(
    OUTPUT ${_FIXTURE_CLASS}
    COMMAND ${_JAVA_GRADLE_ENV} "${TOPO_GRADLE_JAVA_HOME}/bin/javac"
            -g -d ${_FIXTURE_DIR} ${_FIXTURE_SRC}
    DEPENDS ${_FIXTURE_SRC}
    COMMENT "Compiling TinyMatrix.java fixture (-g for JDWP)"
)

# ── Write a per-fixture JDWP launcher script ────────────────────────────────
# The topo-debug CLI passes `--target <path>` to the adapter and that is the
# *only* way to convey a host-binary reference (no `--` passthrough), so the
# fixture-specific classpath + main-class + JDWP flags must be baked into a
# wrapper script. The adapter detects the file-existence + executable bit and
# execs it; the wrapper prints the JVM's "Listening for transport ... at: <port>"
# line on stderr, which the adapter parses for the JDWP attach port.
set(_FIXTURE_LAUNCHER ${CMAKE_CURRENT_BINARY_DIR}/run_tiny_matrix)
set(TOPO_DEBUG_JAVA_TINY_MATRIX_LAUNCHER ${_FIXTURE_LAUNCHER}
    CACHE INTERNAL "TinyMatrix JDWP launcher script")
file(WRITE ${_FIXTURE_LAUNCHER}
"#!/bin/sh
# JDWP launcher for the TinyMatrix fixture.
exec \"${TOPO_GRADLE_JAVA_HOME}/bin/java\" \\
    -agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0 \\
    -cp \"${_FIXTURE_DIR}\" TinyMatrix
")
file(CHMOD ${_FIXTURE_LAUNCHER} PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

# ── Write a launcher script next to the jar ─────────────────────────────────
# POSIX: a shell wrapper invoking `java -jar topo-debug-java.jar "$@"`.
# Windows: a .cmd wrapper. Both live at the build-tree root so the e2e tests
# can reference them directly. The filename `topo-debug-java.sh` (POSIX) +
# `topo-debug-java.cmd` (Windows) avoids colliding with the custom-target
# name `topo-debug-java` (ninja would otherwise see a dependency cycle).
set(_LAUNCHER ${CMAKE_CURRENT_BINARY_DIR}/topo-debug-java.sh)
set(_LAUNCHER_CMD ${CMAKE_CURRENT_BINARY_DIR}/topo-debug-java.cmd)
set(TOPO_DEBUG_JAVA_LAUNCHER ${_LAUNCHER}
    CACHE INTERNAL "topo-debug-java launcher script path")

# We need the JAR path *as a string the launcher will resolve at run time*.
# Configure-time placeholders keep this single-source.
file(WRITE ${_LAUNCHER}
"#!/bin/sh
# topo-debug-java launcher.
# Wraps the JDI adapter jar so the topo-debug CLI sees a single executable.
exec \"${TOPO_GRADLE_JAVA_HOME}/bin/java\" -jar \"${_DEBUG_JAR}\" \"$@\"
")
file(CHMOD ${_LAUNCHER} PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
file(WRITE ${_LAUNCHER_CMD}
"@echo off
\"${TOPO_GRADLE_JAVA_HOME}/bin/java.exe\" -jar \"${_DEBUG_JAR}\" %*
")

add_custom_target(topo-debug-java ALL
    DEPENDS ${_DEBUG_JAR} ${_FIXTURE_CLASS})

# Install the JDI adapter jar alongside the runtime/extract jars so a
# downstream `topo debug` invocation can locate it via
# <prefix>/share/topo-lang-java/lib/topo-debug-java-0.1.0.jar.
install(FILES ${_DEBUG_JAR}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/topo-lang-java/lib
    COMPONENT topo-lang-java-jvm)

# ── Project-level runtime JDK (25) ─────────────────────────────────────────
# Fixtures and transform/runtime jars use the same validated Java 25 baseline.
set(TOPO_DEBUG_JAVA_RUNTIME_HOME "${TOPO_GRADLE_JAVA_HOME}")
if(NOT TOPO_DEBUG_JAVA_RUNTIME_HOME)
    message(STATUS "topo-debug-java project fixtures: no JDK 25 configured — project_simple/project_multi will be skipped.")
endif()

# ── Project-level fixtures ───────────────────────────────────
#
# Counterpart of topo-lang-cpp/topo-debug/test/fixtures/project_{simple,multi}.
#
# Unlike TinyMatrix.java (compiled directly here with javac), these fixtures
# exercise the declaration-driven path end-to-end: `topo build` parses
# main.topo, dispatches to topo-build-jvm-java which compiles + transforms +
# packages the JAR, and writes main.topo-dbg.json (the view registry). `topo
# debug` then auto-discovers the dbg.json alongside the launcher and drives
# the JDI adapter against the running JVM.
#
# Each fixture is copied to the build tree at configure time so `topo build`
# writes its outputs there, not into the source checkout. A per-fixture
# launcher script bakes in the JDWP listen args + `java -jar <output>.jar`
# invocation so the adapter sees a single executable target.

if(TOPO_DEBUG_JAVA_RUNTIME_HOME)
    # project_simple: single int[8], two literal-bounded views + summary
    set(_PROJECT_SIMPLE_JAVA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/project_simple)
    set(TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/fixtures/project_simple
        CACHE INTERNAL "Project-simple Java fixture build dir")
    file(MAKE_DIRECTORY ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/src/main/java)
    configure_file(${_PROJECT_SIMPLE_JAVA_SRC}/Topo.toml
        ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/Topo.toml COPYONLY)
    configure_file(${_PROJECT_SIMPLE_JAVA_SRC}/main.topo
        ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/main.topo COPYONLY)
    configure_file(${_PROJECT_SIMPLE_JAVA_SRC}/src/main/java/Main.java
        ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/src/main/java/Main.java COPYONLY)

    # Per-fixture JDWP launcher — `topo debug --target <path>` execs this.
    # Prints `Listening for transport ... at: <port>` on stderr (the agent's
    # own message); the adapter parses the port and JDI-attaches. We bake the
    # runtime JDK (25) path in because the adapter doesn't forward JAVA_HOME
    # and the launcher needs the same JRE that compiled the JAR.
    set(_PROJECT_SIMPLE_JAVA_LAUNCHER
        ${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/run_project_simple)
    set(TOPO_DEBUG_JAVA_PROJECT_SIMPLE_LAUNCHER ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
        CACHE INTERNAL "project_simple Java JDWP launcher script")
    file(WRITE ${_PROJECT_SIMPLE_JAVA_LAUNCHER}
    "#!/bin/sh
# JDWP launcher for the project_simple Java fixture.
# Uses the Java 25 runtime that built the fixture and transform jars.
exec \"${TOPO_DEBUG_JAVA_RUNTIME_HOME}/bin/java\" \\
    -agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0 \\
    -jar \"${TOPO_DEBUG_JAVA_PROJECT_SIMPLE_DIR}/main.jar\"
")
    file(CHMOD ${_PROJECT_SIMPLE_JAVA_LAUNCHER} PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

    # project_multi: two int[4] locals in the same frame, cross-var summary.
    set(_PROJECT_MULTI_JAVA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/project_multi)
    set(TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/fixtures/project_multi
        CACHE INTERNAL "Project-multi Java fixture build dir")
    file(MAKE_DIRECTORY ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/src/main/java)
    configure_file(${_PROJECT_MULTI_JAVA_SRC}/Topo.toml
        ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/Topo.toml COPYONLY)
    configure_file(${_PROJECT_MULTI_JAVA_SRC}/main.topo
        ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/main.topo COPYONLY)
    configure_file(${_PROJECT_MULTI_JAVA_SRC}/src/main/java/Main.java
        ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/src/main/java/Main.java COPYONLY)

    set(_PROJECT_MULTI_JAVA_LAUNCHER
        ${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/run_project_multi)
    set(TOPO_DEBUG_JAVA_PROJECT_MULTI_LAUNCHER ${_PROJECT_MULTI_JAVA_LAUNCHER}
        CACHE INTERNAL "project_multi Java JDWP launcher script")
    file(WRITE ${_PROJECT_MULTI_JAVA_LAUNCHER}
    "#!/bin/sh
# JDWP launcher for the project_multi Java fixture.
exec \"${TOPO_DEBUG_JAVA_RUNTIME_HOME}/bin/java\" \\
    -agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=127.0.0.1:0 \\
    -jar \"${TOPO_DEBUG_JAVA_PROJECT_MULTI_DIR}/main.jar\"
")
    file(CHMOD ${_PROJECT_MULTI_JAVA_LAUNCHER} PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
endif()

# Expose the runtime JDK to the top-level test CMakeLists so its CTest
# `ENVIRONMENT` properties can set JAVA_HOME for the topo-build invocation.
set(TOPO_DEBUG_JAVA_RUNTIME_HOME ${TOPO_DEBUG_JAVA_RUNTIME_HOME}
    CACHE INTERNAL "JDK 25 home used by topo-debug-java project fixtures")
