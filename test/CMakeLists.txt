# MinGW on GHA: the cached upstream topo-install was linked against a
# different libstdc++/libgcc ABI than C:\mingw64\bin\ currently provides,
# which trips STATUS_ENTRYPOINT_NOT_FOUND (0xc0000139) the moment
# gtest_discover_tests launches each test binary. Static-link the C++
# runtime for every test exe in this directory so they're hermetic.
if(MINGW)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Lang type tests (header-only, no JVM toolchain dependency)
add_executable(topo-lang-java-tests
    unit/JavaStubGeneratorTest.cpp
    unit/JavaEmitterStdlibTest.cpp
    unit/JavaAnalysisProviderTest.cpp
)
# TopoJavaAnalysis PUBLIC-propagates TopoJavaLSP → topo::core::TopoLSPBridge
# → topo::core::TopoPlatform. TopoJavaTranspile brings in JavaEmitter
# (PUBLIC-propagates topo::core::TopoTranspile) for the stdlib emitter tests.
target_link_libraries(topo-lang-java-tests PRIVATE
    TopoJavaAnalysis TopoJavaTranspile
    GTest::gtest_main
)
target_include_directories(topo-lang-java-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/topo-check       # for analysis/stub/JavaStubGenerator.h
    ${PROJECT_SOURCE_DIR}/topo-transpile   # for JavaEmitter.h
)
topo_set_compiler_flags(topo-lang-java-tests)
gtest_discover_tests(topo-lang-java-tests DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "toolchain\;lang-java" TIMEOUT 60)

# JavaDriver input-trust boundary tests — exercises validateClasspath /
# validateMainClass / validateJavaHome rejects of hostile inputs.
# Pure validation logic, no JVM dependency.
add_executable(topo-lang-java-driver-input-trust-tests
    unit/JavaDriverInputTrustTest.cpp
)
target_link_libraries(topo-lang-java-driver-input-trust-tests PRIVATE
    TopoJavaDriver
    GTest::gtest_main
)
topo_set_compiler_flags(topo-lang-java-driver-input-trust-tests)
gtest_discover_tests(topo-lang-java-driver-input-trust-tests DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "toolchain\;lang-java" TIMEOUT 30)

# ── javac probe for the real-javac roundtrip test ────────────
# Standalone build does not inherit topo-core's TopoToolProbes; probe locally.
if(NOT DEFINED TOPO_HAS_JAVAC)
    set(_javac_exe "")
    if(DEFINED TOPO_GRADLE_JAVA_HOME AND EXISTS "${TOPO_GRADLE_JAVA_HOME}/bin/javac")
        set(_javac_exe "${TOPO_GRADLE_JAVA_HOME}/bin/javac")
    else()
        find_program(_javac_exe NAMES javac)
    endif()
    if(_javac_exe)
        execute_process(
            COMMAND "${_javac_exe}" -version
            RESULT_VARIABLE _javac_rc
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_javac_rc EQUAL 0)
            set(TOPO_HAS_JAVAC ON CACHE BOOL "javac is available for real-javac tests")
        else()
            set(TOPO_HAS_JAVAC OFF CACHE BOOL "javac is available for real-javac tests")
        endif()
    else()
        set(TOPO_HAS_JAVAC OFF CACHE BOOL "javac is available for real-javac tests")
    endif()
endif()
message(STATUS "topo-lang-java tests: TOPO_HAS_JAVAC=${TOPO_HAS_JAVAC}")

# Real-javac roundtrip tests for JavaEmitter — compiles emitter output via
# real javac so regressions producing syntactically-Java text that javac
# rejects are caught.
if(TOPO_HAS_JAVAC)
    add_executable(topo-lang-java-emitter-realjavac-tests
        unit/JavaEmitterRealJavacTest.cpp
    )
    target_link_libraries(topo-lang-java-emitter-realjavac-tests PRIVATE
        TopoJavaTranspile
        GTest::gtest_main
    )
    target_include_directories(topo-lang-java-emitter-realjavac-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/topo-transpile
    )
    topo_set_compiler_flags(topo-lang-java-emitter-realjavac-tests)
    gtest_discover_tests(topo-lang-java-emitter-realjavac-tests DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "toolchain\;lang-java\;realpath" TIMEOUT 120)
else()
    message(STATUS "topo-lang-java-emitter-realjavac-tests: NOT built "
        "(TOPO_HAS_JAVAC=OFF; the JavaEmitter synth tests still run; "
        "install a JDK and reconfigure to enable the roundtrip suite)")
endif()

# Extractor golden-fidelity tests depend on the Gradle-built
# topo-extract-java jar (JVM_BACKEND gate).
if(TOPO_LANG_JAVA_ENABLE_JVM_BACKEND)
    set(_JAVA_EXTRACT_JAR
        "${PROJECT_SOURCE_DIR}/topo-check/extractor/build/libs/topo-extract-java-0.1.0.jar"
    )
    add_executable(topo-lang-java-extractor-fidelity-tests
        unit/JavaExtractorFidelityTest.cpp
    )
    target_link_libraries(topo-lang-java-extractor-fidelity-tests PRIVATE
        GTest::gtest_main
        nlohmann_json::nlohmann_json
    )
    target_compile_definitions(topo-lang-java-extractor-fidelity-tests PRIVATE
        TOPO_EXTRACT_JAVA_JAR="${_JAVA_EXTRACT_JAR}"
        JAVA_FIDELITY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/extractor_fidelity"
    )
    add_dependencies(topo-lang-java-extractor-fidelity-tests topo-extract-java)
    topo_set_compiler_flags(topo-lang-java-extractor-fidelity-tests)
    gtest_discover_tests(topo-lang-java-extractor-fidelity-tests DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "toolchain\;lang-java\;fidelity" TIMEOUT 120
        ENVIRONMENT "TOPO_EXTRACT_ROOT=${PROJECT_SOURCE_DIR}"
    )
endif()

# Check E2E tests link TopoCheckRunner from topo-cli. When topo-cli's
# cmake config isn't on CMAKE_PREFIX_PATH the suite skips cleanly with
# a STATUS message; topo-core's eventual export of TopoCheckRunner will
# let us drop the topo-cli dependency entirely.
# Deliberately OUTSIDE the JVM_BACKEND gate: the check pipeline uses the
# in-process extractors plus JdtBridge (jdtls) — it never spawns the
# Gradle-built topo-extract-java jar — so coupling it to the Gradle
# toolchain only shrank where the suite could run (CI's verify-l2 job
# builds it with just topo-cli on the prefix). TIMEOUT 180: each L2 deep
# case boots its own jdtls instance, which dominates the 60s default on
# CI runners.
find_package(topo-cli CONFIG QUIET)
if(TARGET topo::cli::TopoCheckRunner)
    add_executable(topo-lang-java-check-tests
        e2e/TestMain.cpp
        e2e/JavaCompletenessTest.cpp
        e2e/JavaContainmentTest.cpp
        e2e/JavaPurityTest.cpp
        e2e/JavaVisibilityTest.cpp
        e2e/JavaStageIsolationTest.cpp
    )
    target_link_libraries(topo-lang-java-check-tests PRIVATE
        topo::cli::TopoCheckRunner TopoJavaPlugin
        GTest::gtest
    )
    target_compile_definitions(topo-lang-java-check-tests PRIVATE
        TOPO_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/check"
    )
    topo_set_compiler_flags(topo-lang-java-check-tests)
    gtest_discover_tests(topo-lang-java-check-tests DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "toolchain\;checker\;lang-java" TIMEOUT 180)
else()
    message(STATUS "topo-lang-java-check-tests: NOT built — topo-cli "
        "config not on CMAKE_PREFIX_PATH (provides TopoCheckRunner). "
        "Install topo-cli and re-configure to enable.")
endif()
