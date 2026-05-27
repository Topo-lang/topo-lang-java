add_library(TopoJavaPlugin
    JavaPlugin.cpp
)
add_library(topo::lang-java::TopoJavaPlugin ALIAS TopoJavaPlugin)

target_include_directories(TopoJavaPlugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lang>
)
target_link_libraries(TopoJavaPlugin PUBLIC
    topo::lang::TopoLang
    TopoJavaAnalysis
    TopoJavaCheck
    TopoJavaTranspile
    TopoJavaLSP
    TopoJavaInit
)
topo_set_compiler_flags(TopoJavaPlugin)

install(TARGETS TopoJavaPlugin
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lang
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lang
    FILES_MATCHING PATTERN "*.h"
)
