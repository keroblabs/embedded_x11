set(XLIB_ROOT "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB XLIB_SOURCES CONFIGURE_DEPENDS "${XLIB_ROOT}/src/*.c")
foreach(name xcb_io xcb_disp x11_xcb locking os2Stubs SetLocale Xrm ClDisplay
    DisName GetDflt ParseCmd FSWrap OCWrap OMWrap imConv RdBitF WrBitF)
    list(REMOVE_ITEM XLIB_SOURCES "${XLIB_ROOT}/src/${name}.c")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/cmake/xlib-config.h.in"
    "${CMAKE_BINARY_DIR}/generated/xlib/config.h" @ONLY)
add_executable(wmb_makekeys "${XLIB_ROOT}/src/util/makekeys.c")
file(GLOB keysym_headers "${PROJECT_SOURCE_DIR}/X11/*keysym.h")
list(APPEND keysym_headers "${PROJECT_SOURCE_DIR}/X11/keysymdef.h")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/xlib/ks_tables.h"
    COMMAND "${CMAKE_COMMAND}" "-DGENERATOR=$<TARGET_FILE:wmb_makekeys>"
        "-DHEADERS=${keysym_headers}"
        "-DOUTPUT=${CMAKE_BINARY_DIR}/generated/xlib/ks_tables.h"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateXlibKeys.cmake"
    DEPENDS wmb_makekeys ${keysym_headers}
    VERBATIM)
add_library(wmb_xlib STATIC ${XLIB_SOURCES}
    "${CMAKE_BINARY_DIR}/generated/xlib/ks_tables.h"
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_transport.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_close.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_xlib_profile.c")
get_target_property(client_sources wmb_xlib SOURCES)
set(selected_client_sources)
foreach(source IN LISTS client_sources)
    if(source MATCHES "\\.c$")
        get_filename_component(source "${source}" REALPATH)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
        list(APPEND selected_client_sources "${relative}")
    endif()
endforeach()
list(SORT selected_client_sources)
file(STRINGS "${PROJECT_SOURCE_DIR}/docs/reference/xlib-sources.reference"
    reference_client_sources REGEX "^[^#]")
if(NOT "${selected_client_sources}" STREQUAL "${reference_client_sources}")
    message(FATAL_ERROR "Client source cohort differs from docs/reference/xlib-sources.reference")
endif()
target_include_directories(wmb_xlib PUBLIC
    "${XLIB_ROOT}/include" "${XLIB_ROOT}/include/X11" "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/transport/x11"
    PRIVATE "${CMAKE_BINARY_DIR}/generated/xlib" "${XLIB_ROOT}/src"
    "${XLIB_ROOT}/src/xcms" "${XLIB_ROOT}/src/xkb" "${XLIB_ROOT}/src/xlibi18n")
target_compile_definitions(wmb_xlib PUBLIC WMB_INPROC=1 PRIVATE HAVE_CONFIG_H=1)
target_compile_options(wmb_xlib PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(wmb_xlib PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(wmb_xlib PUBLIC wmb_transport_core)

add_library(wmb_xlib_test_client STATIC
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_client_test.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_flow_test.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/wmb_timeout_test.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/tests/wire_script.c"
    "${PROJECT_SOURCE_DIR}/transport/x11/tests/wmb_wire_client.c")
target_link_libraries(wmb_xlib_test_client PUBLIC wmb_xlib)
target_compile_options(wmb_xlib_test_client PRIVATE -Wall -Wextra -Werror)

add_executable(wmb_xlib_buffer_test "${PROJECT_SOURCE_DIR}/transport/x11/tests/buffer_test.c")
target_link_libraries(wmb_xlib_buffer_test PRIVATE wmb_xlib)
target_compile_options(wmb_xlib_buffer_test PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_xlib_buffer_test PRIVATE -Wl,--wrap=realloc -Wl,--gc-sections)
add_test(NAME tinyx_p5_buffer COMMAND wmb_xlib_buffer_test)

option(WMB_BUILD_WIRE_REFERENCE "Build the independent system-Xlib wire capture helper" OFF)
if(WMB_BUILD_WIRE_REFERENCE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WMB_SYSTEM_X11 REQUIRED IMPORTED_TARGET x11)
    add_executable(wmb_wire_reference
        "${PROJECT_SOURCE_DIR}/transport/x11/tests/wire_script.c"
        "${PROJECT_SOURCE_DIR}/transport/x11/tests/native_capture.c"
        "${PROJECT_SOURCE_DIR}/transport/x11/tests/native_reference.c")
    target_link_libraries(wmb_wire_reference PRIVATE PkgConfig::WMB_SYSTEM_X11 ${CMAKE_DL_LIBS})
    target_link_options(wmb_wire_reference PRIVATE -Wl,--export-dynamic)
    target_compile_options(wmb_wire_reference PRIVATE -Wall -Wextra -Werror)
endif()
