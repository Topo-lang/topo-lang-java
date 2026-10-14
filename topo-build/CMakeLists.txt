add_library(TopoJavaDriver STATIC
    JavaDriver.cpp
    JavaBuildEvidence.cpp
)
add_library(topo::lang-java::TopoJavaDriver ALIAS TopoJavaDriver)

target_include_directories(TopoJavaDriver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-build>
)
# topo-core/include propagates via topo::core::TopoPlatform.
target_link_libraries(TopoJavaDriver PUBLIC
    topo::core::TopoPlatform
    topo::core::TopoDistribution
    topo::core::TopoBuildLib
    nlohmann_json::nlohmann_json)
topo_set_compiler_flags(TopoJavaDriver)

install(TARGETS TopoJavaDriver
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-build
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-build
    FILES_MATCHING PATTERN "*.h"
)
