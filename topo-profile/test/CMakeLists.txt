# Java span-emitter fixture for topo-profile.
#
# Compiles SpansDemo.java directly with javac (no Gradle — this fixture
# has no dependencies and reusing the Java compiler keeps configure-time
# cheap). Output `SpansDemo.class` lands in the build tree; a tiny shell
# wrapper `spans_demo_java` invokes `java -cp <dir> SpansDemo` so the
# topo-profile CLI can spawn it like any other executable.

if(NOT TOPO_GRADLE_JAVA_HOME)
    message(STATUS "topo-lang-java/topo-profile: TOPO_GRADLE_JAVA_HOME not set; "
                   "skipping Java span fixture.")
    return()
endif()

set(_SPANS_JAVA_HOME "${TOPO_GRADLE_JAVA_HOME}")
set(_JAVAC "${_SPANS_JAVA_HOME}/bin/javac")
set(_JAVA  "${_SPANS_JAVA_HOME}/bin/java")

if(NOT EXISTS "${_JAVAC}" OR NOT EXISTS "${_JAVA}")
    message(WARNING
        "topo-lang-java/topo-profile: javac/java not found under "
        "${_SPANS_JAVA_HOME}; fixture disabled.")
    return()
endif()

set(_FIXTURE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/SpansDemo.java")
set(_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(_FIXTURE_CLASS "${_FIXTURE_DIR}/SpansDemo.class")

add_custom_command(
    OUTPUT "${_FIXTURE_CLASS}"
    COMMAND "${_JAVAC}" -d "${_FIXTURE_DIR}" "${_FIXTURE_SRC}"
    DEPENDS "${_FIXTURE_SRC}"
    COMMENT "Compiling Java span fixture (SpansDemo.java)"
    VERBATIM)

# Wrapper script: lets the topo-profile e2e test treat the fixture as a
# plain executable. `java -cp <dir> SpansDemo` is the canonical invocation;
# stderr is redirected to /dev/null so the JVM startup banner can't
# contaminate topo-profile's NDJSON parser (which already skips non-{ lines,
# but the wrapper keeps the contract clean).
set(_WRAPPER "${_FIXTURE_DIR}/spans_demo_java.sh")
file(GENERATE OUTPUT "${_WRAPPER}"
     CONTENT "#!/bin/sh\nexec \"${_JAVA}\" -cp \"${_FIXTURE_DIR}\" SpansDemo \"$@\"\n")
# file(GENERATE) doesn't set exec permissions; do that at build time.
add_custom_command(
    OUTPUT "${_WRAPPER}.stamp"
    COMMAND ${CMAKE_COMMAND} -E touch "${_WRAPPER}.stamp"
    COMMAND chmod +x "${_WRAPPER}"
    DEPENDS "${_FIXTURE_CLASS}" "${_WRAPPER}"
    VERBATIM)

add_custom_target(spans_demo_java ALL
    DEPENDS "${_FIXTURE_CLASS}" "${_WRAPPER}.stamp")
set_target_properties(spans_demo_java PROPERTIES
    TOPO_FIXTURE_PATH "${_WRAPPER}")

# JfrDemo: compile + run-once to produce a `.jfr` recording.
#
# JfrDemo uses `jdk.jfr.Recording` (JDK 11+) to record itself for ~150 ms,
# writing the resulting binary file to the build tree. The e2e test
# `e2e.topo-profile.sampling_java_jfr_binary` then runs topo-profile with
# `--jfr-binary-input <jfr>`, which spawns the topo-profile-jvm-jfr bridge
# to translate the recording back to NDJSON.
#
# We commit the source (JfrDemo.java) but not the binary (jfr files are
# host-recorded and not deterministic across runs); regenerating at build
# time is cheap (a few hundred ms) and keeps the fixture self-contained.
set(_JFR_DEMO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/JfrDemo.java")
set(_JFR_DEMO_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(_JFR_DEMO_CLASS "${_JFR_DEMO_DIR}/JfrDemo.class")
set(_JFR_BINARY_SAMPLE "${_JFR_DEMO_DIR}/jfr_binary_sample.jfr")

add_custom_command(
    OUTPUT "${_JFR_DEMO_CLASS}"
    COMMAND "${_JAVAC}" -d "${_JFR_DEMO_DIR}" "${_JFR_DEMO_SRC}"
    DEPENDS "${_JFR_DEMO_SRC}"
    COMMENT "Compiling JfrDemo (JFR recorder)"
    VERBATIM)

add_custom_command(
    OUTPUT "${_JFR_BINARY_SAMPLE}"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${_JFR_BINARY_SAMPLE}"
    COMMAND "${_JAVA}" -cp "${_JFR_DEMO_DIR}" JfrDemo "${_JFR_BINARY_SAMPLE}"
    DEPENDS "${_JFR_DEMO_CLASS}"
    COMMENT "Recording jfr_binary_sample.jfr"
    VERBATIM)

add_custom_target(jfr_binary_sample ALL
    DEPENDS "${_JFR_BINARY_SAMPLE}")
set_target_properties(jfr_binary_sample PROPERTIES
    TOPO_JFR_BINARY_PATH "${_JFR_BINARY_SAMPLE}")
