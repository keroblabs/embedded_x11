include(FetchContent)
FetchContent_Declare(wmb_zlib_source
    URL https://zlib.net/fossils/zlib-1.4.tar.gz
    URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
    SOURCE_SUBDIR wmb-inflate-only
    ${timestamp_option})
FetchContent_MakeAvailable(wmb_zlib_source)
set(zlib_sources)
foreach(source adler32 crc32 inffast inflate inftrees zutil)
    list(APPEND zlib_sources "${wmb_zlib_source_SOURCE_DIR}/${source}.c")
endforeach()
add_library(wmb_zlib STATIC ${zlib_sources})
target_compile_definitions(wmb_zlib PUBLIC Z_SOLO=1)
target_include_directories(wmb_zlib PUBLIC "${wmb_zlib_source_SOURCE_DIR}")
target_compile_options(wmb_zlib PRIVATE
    "SHELL:-include \"${PROJECT_SOURCE_DIR}/transport/api/allocation_redirect.h\"")
target_link_libraries(wmb_zlib PRIVATE wmb_framework)
add_library(ZLIB::ZLIB ALIAS wmb_zlib)
