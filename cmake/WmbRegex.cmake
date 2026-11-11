include(FetchContent)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(timestamp_option DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
FetchContent_Declare(wmb_tre_source
    URL https://github.com/laurikari/tre/releases/download/v0.9.0/tre-0.9.0.tar.gz
    URL_HASH SHA256=f57f5698cafdfe516d11fb0b71705916fe1162f14b08cf69d7cf86923b5a2477
    SOURCE_SUBDIR wmb-posix-only
    ${timestamp_option})
FetchContent_MakeAvailable(wmb_tre_source)
include("${CMAKE_CURRENT_LIST_DIR}/WmbDependencyPatch.cmake")
wmb_apply_dependency_patch("${wmb_tre_source_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/cmake/patches/tre-0.9.0-oom-tags.patch")
set(regex_include "${CMAKE_BINARY_DIR}/generated/regex")
configure_file("${PROJECT_SOURCE_DIR}/cmake/tre-config.h.in"
    "${regex_include}/tre/tre-config.h" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/cmake/tre-config.h.in"
    "${regex_include}/config.h" @ONLY)
# The release includes a host-configured generated header beside tre.h.
configure_file("${PROJECT_SOURCE_DIR}/cmake/tre-config.h.in"
    "${wmb_tre_source_SOURCE_DIR}/local_includes/tre-config.h" @ONLY)
configure_file("${wmb_tre_source_SOURCE_DIR}/local_includes/tre.h"
    "${regex_include}/tre/tre.h" COPYONLY)
configure_file("${wmb_tre_source_SOURCE_DIR}/local_includes/regex.h"
    "${regex_include}/regex.h" COPYONLY)
set(regex_sources)
foreach(source tre-ast tre-compile tre-match-backtrack tre-match-parallel
        tre-mem tre-parse tre-stack regcomp regexec regerror)
    list(APPEND regex_sources "${wmb_tre_source_SOURCE_DIR}/lib/${source}.c")
endforeach()
add_library(wmb_regex STATIC ${regex_sources})
target_compile_definitions(wmb_regex PRIVATE HAVE_CONFIG_H=1)
target_include_directories(wmb_regex PUBLIC "${regex_include}"
    PRIVATE "${regex_include}/tre" "${wmb_tre_source_SOURCE_DIR}/lib")
target_compile_options(wmb_regex PRIVATE
    "SHELL:-include \"${PROJECT_SOURCE_DIR}/transport/api/allocation_redirect.h\"")
target_link_libraries(wmb_regex PRIVATE wmb_framework)
