add_library(TopoJavaInit STATIC JavaInitTemplateProvider.cpp)
add_library(topo::lang-java::TopoJavaInit ALIAS TopoJavaInit)

target_include_directories(TopoJavaInit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-init>
)
target_link_libraries(TopoJavaInit PUBLIC topo::lang::TopoLang)
topo_set_compiler_flags(TopoJavaInit)

install(TARGETS TopoJavaInit
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-init
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-init
    FILES_MATCHING PATTERN "*.h"
)
