add_library(TopoJavaLSP
    JdtBridge.cpp
)
add_library(topo::lang-java::TopoJavaLSP ALIAS TopoJavaLSP)

target_include_directories(TopoJavaLSP PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lsp>
)
target_link_libraries(TopoJavaLSP PUBLIC topo::core::TopoLSPBridge)
topo_set_compiler_flags(TopoJavaLSP)

install(TARGETS TopoJavaLSP
    EXPORT topo-lang-javaTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lsp
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/topo-lang-java/topo-lsp
    FILES_MATCHING PATTERN "*.h"
)
