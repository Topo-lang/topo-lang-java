# Checker-e2e suite, extracted so it can be added from two scopes:
#   (a) inline by ../CMakeLists.txt — the standalone/CI path, where
#       find_package(topo-cli) resolves from CMAKE_PREFIX_PATH;
#   (b) re-added late by an embedding super-build after topo-cli has
#       configured, where the in-tree topo::cli::TopoCheckRunner alias
#       satisfies the gate that the inline pass had to skip.
# Every gate self-skips with a STATUS message and the whole file is
# idempotent, so a double add is harmless.
#
# Note: directory-scoped options from ../CMakeLists.txt (e.g. the MinGW
# static-link add_link_options) reach this suite only via the inline
# path; the late path is never exercised on MinGW.

if(TARGET topo-lang-java-check-tests)
    return()
endif()

# Deliberately OUTSIDE the JVM_BACKEND gate: the check pipeline uses the
# in-process extractors plus JdtBridge (jdtls) — it never spawns the
# Gradle-built topo-extract-java jar — so coupling it to the Gradle
# toolchain only shrank where the suite could run (CI's verify-l2 job
# builds it with just topo-cli on the prefix).
if(NOT TARGET TopoJavaPlugin)
    message(STATUS "topo-lang-java-check-tests: NOT built — TopoJavaPlugin "
        "target not available in this scope.")
    return()
endif()

# Links TopoCheckRunner from topo-cli. When topo-cli's cmake config isn't
# on CMAKE_PREFIX_PATH the suite skips cleanly with a STATUS message;
# topo-core's eventual export of TopoCheckRunner will let us drop the
# topo-cli dependency entirely.
if(NOT TARGET topo::cli::TopoCheckRunner)
    find_package(topo-cli CONFIG QUIET)
endif()
if(NOT TARGET topo::cli::TopoCheckRunner)
    message(STATUS "topo-lang-java-check-tests: NOT built — topo-cli "
        "config not on CMAKE_PREFIX_PATH (provides TopoCheckRunner). "
        "Install topo-cli and re-configure to enable.")
    return()
endif()

# Sources and fixtures stay under test/ — only the registration moved.
# Paths derive from this file's location, not PROJECT_SOURCE_DIR, so
# they stay correct when an embedding super-build adds this directory.
get_filename_component(_test_root ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
get_filename_component(_repo_root ${_test_root} DIRECTORY)

# TIMEOUT 180: each L2 deep case boots its own jdtls instance, which
# dominates the 60s default on CI runners.
add_executable(topo-lang-java-check-tests
    ${_test_root}/e2e/TestMain.cpp
    ${_test_root}/e2e/JavaCompletenessTest.cpp
    ${_test_root}/e2e/JavaContainmentTest.cpp
    ${_test_root}/e2e/JavaPurityTest.cpp
    ${_test_root}/e2e/JavaVisibilityTest.cpp
    ${_test_root}/e2e/JavaStageIsolationTest.cpp
    ${_test_root}/e2e/JavaShowcaseTest.cpp
)
target_link_libraries(topo-lang-java-check-tests PRIVATE
    topo::cli::TopoCheckRunner TopoJavaPlugin
    GTest::gtest
)
target_compile_definitions(topo-lang-java-check-tests PRIVATE
    TOPO_TEST_FIXTURES_DIR="${_test_root}/fixtures/check"
    TOPO_EXAMPLES_DIR="${_repo_root}/examples"
)
topo_set_compiler_flags(topo-lang-java-check-tests)
gtest_discover_tests(topo-lang-java-check-tests DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "toolchain\;checker\;lang-java" TIMEOUT 180)
