add_library(TopoJavaCheck STATIC JavaCheckRunner.cpp)
add_library(topo::lang-java::TopoJavaCheck ALIAS TopoJavaCheck)

target_include_directories(TopoJavaCheck PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/runner>
)
target_link_libraries(TopoJavaCheck PUBLIC topo::lang::TopoLang)
topo_set_compiler_flags(TopoJavaCheck)

install(TARGETS TopoJavaCheck
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/runner
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-check/runner
    FILES_MATCHING PATTERN "*.h"
)
