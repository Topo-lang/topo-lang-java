# TopoLangJavaCompilerFlags.cmake — standalone compiler-flag helper for topo-lang-java.

if(NOT WIN32)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(APPLE)
        set(CMAKE_MACOSX_RPATH ON)
    endif()
endif()

set(TOPO_LANG_JAVA_SANITIZER "" CACHE STRING
    "Enable sanitizers (address, undefined, thread, memory)")

function(topo_lang_java_apply_sanitizer target)
    if(NOT TOPO_LANG_JAVA_SANITIZER)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target}
            PRIVATE -fsanitize=${TOPO_LANG_JAVA_SANITIZER} -fno-omit-frame-pointer)
        target_link_options(${target}
            PRIVATE -fsanitize=${TOPO_LANG_JAVA_SANITIZER})
    endif()
endfunction()

function(topo_set_compiler_flags target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
    topo_lang_java_apply_sanitizer(${target})
endfunction()

function(topo_set_llvm_flags target)
    # topo-lang-java doesn't itself link LLVM — JVM is its backend ecosystem.
    # The helper exists for symmetry with topo-lang-cpp/topo-lang-rust so
    # vendored subdir CMakeLists that conditionally call it still configure.
    topo_set_compiler_flags(${target})
endfunction()

# PCH stub — no-op in standalone topo-lang-java. Guarded so a parent
# meta-repo that already provides a real topo_apply_std_pch wins.
if(NOT COMMAND topo_apply_std_pch)
    function(topo_apply_std_pch target)
        # PCH stub — no-op in standalone topo-lang-java.
    endfunction()
endif()
