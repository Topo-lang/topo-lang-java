# topo-profile-jvm-jfr — (C2) `.jfr` → NDJSON bridge.
#
# Soft-gated on TOPO_GRADLE_JAVA_HOME so machines without a JDK still
# configure cleanly. Downstream wiring (the topo-profile CLI's baked-in
# default jar path, and the e2e test that exercises the binary `.jfr` path)
# checks `if(TARGET topo-profile-jvm-jfr)` and skips when absent.

if(NOT TOPO_GRADLE_JAVA_HOME)
    message(STATUS "topo-profile-jvm-jfr: TOPO_GRADLE_JAVA_HOME not set — "
                   "skipping Java JFR bridge.")
    return()
endif()

set(_JAVA_GRADLE_ENV ${CMAKE_COMMAND} -E env "JAVA_HOME=${TOPO_GRADLE_JAVA_HOME}")
set(_BRIDGE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(_BRIDGE_JAR_RAW ${_BRIDGE_DIR}/build/libs/topo-profile-jvm-jfr-0.1.0.jar)

# Copy the produced jar to a stable, predictable location next to the
# topo-profile binary so the CLI's baked-in default works without depending
# on Gradle output paths leaking through to consumers.
set(_BRIDGE_JAR_STAGED ${CMAKE_BINARY_DIR}/topo-profile-jvm-jfr.jar)

file(GLOB_RECURSE _BRIDGE_JAVA_SRC CONFIGURE_DEPENDS
    ${_BRIDGE_DIR}/src/main/java/*.java)

add_custom_command(
    OUTPUT ${_BRIDGE_JAR_RAW}
    COMMAND ${_JAVA_GRADLE_ENV} ${TOPO_GRADLEW_CMD} jar ${_JAVA_GRADLE_EXTRA_ARGS}
    WORKING_DIRECTORY ${_BRIDGE_DIR}
    DEPENDS ${_BRIDGE_JAVA_SRC} ${_BRIDGE_DIR}/build.gradle.kts
    COMMENT "Building topo-profile-jvm-jfr"
)

add_custom_command(
    OUTPUT ${_BRIDGE_JAR_STAGED}
    COMMAND ${CMAKE_COMMAND} -E copy ${_BRIDGE_JAR_RAW} ${_BRIDGE_JAR_STAGED}
    DEPENDS ${_BRIDGE_JAR_RAW}
    COMMENT "Staging topo-profile-jvm-jfr.jar next to topo-profile binary"
)

add_custom_target(topo-profile-jvm-jfr ALL
    DEPENDS ${_BRIDGE_JAR_STAGED})

# Expose the staged jar path to the rest of the build (the topo-profile
# binary bakes it in as TOPO_JFR_BRIDGE_JAR_DEFAULT; the e2e tests reference
# it directly).
set(TOPO_JFR_BRIDGE_JAR_PATH ${_BRIDGE_JAR_STAGED}
    CACHE INTERNAL "staged topo-profile-jvm-jfr.jar path")
set(TOPO_JFR_BRIDGE_JAVA_HOME ${TOPO_GRADLE_JAVA_HOME}
    CACHE INTERNAL "JDK used to compile + run the JFR bridge")

# Install the JFR bridge jar alongside the other JVM-side jars.
install(FILES ${_BRIDGE_JAR_RAW}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/topo-lang-java/lib
    COMPONENT topo-lang-java-jvm)
