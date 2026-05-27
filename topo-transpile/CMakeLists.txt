add_library(TopoJavaTranspile
    JavaEmitter.cpp
)
add_library(topo::lang-java::TopoJavaTranspile ALIAS TopoJavaTranspile)

target_include_directories(TopoJavaTranspile PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-transpile>
)
# topo-core/include comes via topo::core::TopoTranspile's PUBLIC propagation.
target_link_libraries(TopoJavaTranspile PUBLIC topo::core::TopoTranspile)
topo_set_compiler_flags(TopoJavaTranspile)

install(TARGETS TopoJavaTranspile
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-transpile
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-transpile
    FILES_MATCHING PATTERN "*.h"
)
