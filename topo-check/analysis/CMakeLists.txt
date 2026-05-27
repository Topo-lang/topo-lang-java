add_library(TopoJavaAnalysis
    JavaAnalysisProvider.cpp
    stub/JavaStubGenerator.cpp
    catalog/JavaUnsafeCatalog.cpp
    extract/JavaCallEdgeExtractor.cpp
    extract/JavaSymbolExtractor.cpp
    extract/JavaSymbolAccessExtractor.cpp
    extract/JavaCallSiteExtractor.cpp
    extract/JavaImportExtractor.cpp
    extract/JavaLSPSymbolExtractor.cpp
    extract/JavaLSPCallSiteExtractor.cpp
    extract/JavaLSPImportExtractor.cpp
    catalog/JavaSafePatterns.cpp
    catalog/JavaSafetyAnalyzer.cpp
)
add_library(topo::lang-java::TopoJavaAnalysis ALIAS TopoJavaAnalysis)

find_path(TOMLPLUSPLUS_INCLUDE_DIR toml++/toml.hpp REQUIRED)

target_include_directories(TopoJavaAnalysis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/analysis>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extract
        ${CMAKE_CURRENT_SOURCE_DIR}/catalog
        ${CMAKE_CURRENT_SOURCE_DIR}/stub
        ${TOMLPLUSPLUS_INCLUDE_DIR}
)
# topo-core's include path propagates via topo::core::TopoCheck.
# TopoJavaLSP carries the local Java LSP bridge headers.
target_link_libraries(TopoJavaAnalysis
    PUBLIC topo::core::TopoCheck TopoJavaLSP
    PRIVATE topo::core::TopoPlatform)
# TOPO_SOURCE_DIR is consulted by JavaSafePatterns.cpp for source-tree catalog
# discovery (with runtime fallback when absent). In standalone mode it points
# at the topo-lang-java source root; install-time consumers can override via
# -DTOPO_SOURCE_DIR=<your-checkout-root>.
target_compile_definitions(TopoJavaAnalysis PRIVATE
    TOPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}/.."
)
topo_set_compiler_flags(TopoJavaAnalysis)

install(TARGETS TopoJavaAnalysis
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/analysis
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/analysis
    FILES_MATCHING PATTERN "*.h"
)
