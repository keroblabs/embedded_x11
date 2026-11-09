include(FetchContent)
find_package(ZLIB REQUIRED)
find_library(WMB_FONTENC_LIBRARY NAMES fontenc libfontenc.so.1)
if(NOT WMB_FONTENC_LIBRARY)
    message(FATAL_ERROR "The bitmap font dependency requires the libfontenc runtime library")
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(timestamp_option DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

FetchContent_Declare(wmb_xfont_source
    URL https://www.x.org/releases/individual/lib/libXfont-1.5.4.tar.bz2
    URL_HASH SHA256=1a7f7490774c87f2052d146d1e0e64518d32e6848184a18654e8d0bb57883242
    ${timestamp_option}
)
FetchContent_MakeAvailable(wmb_xfont_source)
set(XFONT_ROOT "${wmb_xfont_source_SOURCE_DIR}")
find_program(WMB_PATCH_EXECUTABLE patch)
if(NOT WMB_PATCH_EXECUTABLE)
    message(FATAL_ERROR "The pinned font dependency requires the patch utility")
endif()
foreach(patch_name empty-directory pcf-unsigned-reads)
    set(font_patch "${WMB_SOURCE_ROOT}/cmake/patches/libxfont-1.5.4-${patch_name}.patch")
    execute_process(COMMAND "${WMB_PATCH_EXECUTABLE}" --batch --dry-run --forward -p1 -i "${font_patch}"
        WORKING_DIRECTORY "${XFONT_ROOT}" RESULT_VARIABLE patch_needed
        OUTPUT_QUIET ERROR_QUIET)
    if(patch_needed EQUAL 0)
        execute_process(COMMAND "${WMB_PATCH_EXECUTABLE}" --batch --forward -p1 -i "${font_patch}"
            WORKING_DIRECTORY "${XFONT_ROOT}" RESULT_VARIABLE patch_result)
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply the libXfont repair: ${patch_name}")
        endif()
    else()
        execute_process(COMMAND "${WMB_PATCH_EXECUTABLE}" --batch --dry-run --reverse -p1 -i "${font_patch}"
            WORKING_DIRECTORY "${XFONT_ROOT}" RESULT_VARIABLE already_patched
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT already_patched EQUAL 0)
            message(FATAL_ERROR "The libXfont source differs from the pinned, supported patch state")
        endif()
    endif()
endforeach()
set(XFONT_SOURCES)
foreach(group fontfile bitmap builtins util)
    if(group STREQUAL fontfile)
        set(names bitsource bufio decompress defaults dirfile fileio filewr fontdir
            fontencc fontfile fontscale gunzip register renderers catalogue)
    elseif(group STREQUAL bitmap)
        set(names bitmap bitmapfunc bitmaputil bitscale fontink pcfread pcfwrite)
    elseif(group STREQUAL builtins)
        set(names dir file fonts fpe render)
    else()
        set(names atom fontaccel fontnames fontutil fontxlfd format miscutil
            patcache private utilbitmap)
    endif()
    foreach(name IN LISTS names)
        list(APPEND XFONT_SOURCES "${XFONT_ROOT}/src/${group}/${name}.c")
    endforeach()
endforeach()
configure_file("${WMB_SOURCE_ROOT}/cmake/xfont-config.h.in"
    "${CMAKE_BINARY_DIR}/generated/xfont/config.h" @ONLY)
add_library(wmb_xfont STATIC ${XFONT_SOURCES})
target_include_directories(wmb_xfont PUBLIC "${XFONT_ROOT}/include" PRIVATE
    "${CMAKE_BINARY_DIR}/generated/xfont" "${XFONT_ROOT}/src/bitmap"
    "${XFONT_ROOT}/src/builtins" "${XFONT_ROOT}/src/stubs" "${WMB_SOURCE_ROOT}")
target_compile_definitions(wmb_xfont PRIVATE HAVE_CONFIG_H=1)
target_link_libraries(wmb_xfont PUBLIC ZLIB::ZLIB "${WMB_FONTENC_LIBRARY}")
