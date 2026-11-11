set(WMB_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(TINYX_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}")
include("${WMB_SOURCE_ROOT}/cmake/WmbXfont.cmake")
include("${WMB_SOURCE_ROOT}/cmake/WmbModules.cmake")
get_target_property(kdrive_sources wmb_kdrive SOURCES)
get_target_property(kdrive_directory wmb_kdrive SOURCE_DIR)
set(selected_sources)
foreach(source IN LISTS TINYX_SERVER_SOURCES kdrive_sources)
    if(NOT IS_ABSOLUTE "${source}")
        set(source "${kdrive_directory}/${source}")
    endif()
    get_filename_component(source "${source}" REALPATH)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${source}")
    list(APPEND selected_sources "${relative}")
endforeach()
list(SORT selected_sources)
file(STRINGS "${WMB_SOURCE_ROOT}/docs/reference/tinyx-sources.reference"
    reference_sources REGEX "^[^#]")
if(NOT "${selected_sources}" STREQUAL "${reference_sources}")
    message(FATAL_ERROR "TinyX source groups differ from the frozen P4 inventory; review and update docs/reference/tinyx-sources.reference")
endif()
include(TestBigEndian)
test_big_endian(WMB_BIG_ENDIAN)
if(WMB_BIG_ENDIAN)
    set(WMB_X_BYTE_ORDER X_BIG_ENDIAN)
    set(WMB_BITMAP_BYTE_ORDER MSBFirst)
else()
    set(WMB_X_BYTE_ORDER X_LITTLE_ENDIAN)
    set(WMB_BITMAP_BYTE_ORDER LSBFirst)
endif()
set(WMB_PORTABLE_SERVERMD "${WMB_FREESTANDING}")
include(CheckTypeSize)
check_type_size("unsigned long" WMB_UNSIGNED_LONG_SIZE)
if(WMB_UNSIGNED_LONG_SIZE EQUAL 8)
    set(_XSERVER64 1)
else()
    unset(_XSERVER64)
endif()
foreach(header dix-config kdrive-config)
    configure_file("${WMB_SOURCE_ROOT}/cmake/${header}.h.in"
        "${CMAKE_BINARY_DIR}/generated/tinyx/${header}.h" @ONLY)
endforeach()
add_library(wmb_tinyx_config INTERFACE)
target_compile_definitions(wmb_tinyx_config INTERFACE
    HAVE_DIX_CONFIG_H=1 HAVE_KDRIVE_CONFIG_H=1 HAVE_CONFIG_H=1)
target_include_directories(wmb_tinyx_config INTERFACE
    "${CMAKE_BINARY_DIR}/generated/tinyx")
target_compile_options(wmb_tinyx_config INTERFACE -include
    "${CMAKE_BINARY_DIR}/generated/tinyx/kdrive-config.h")
target_link_libraries(wmb_tinyx_config INTERFACE wmb_tinyx_project)
target_link_libraries(wmb_kdrive PUBLIC wmb_tinyx_config)

add_library(wmb_tinyx STATIC ${TINYX_SERVER_SOURCES})
target_include_directories(wmb_tinyx PUBLIC
    "${TINYX_SOURCE_ROOT}/wmb"
    "${TINYX_SOURCE_ROOT}/xfixes" "${TINYX_SOURCE_ROOT}/damageext")
target_link_libraries(wmb_tinyx PUBLIC wmb_tinyx_config wmb_kdrive
    wmb_xfont wmb_hal wmb_transport_core)
target_compile_options(wmb_tinyx PRIVATE -ffunction-sections -fdata-sections)

if(WMB_FREESTANDING)
    return()
endif()

add_executable(wmb_tinyx_timer_test
    "${TINYX_SOURCE_ROOT}/os/timer_wmb.c" wmb/tests/timer_test.c)
target_include_directories(wmb_tinyx_timer_test PRIVATE
    $<TARGET_PROPERTY:wmb_kdrive,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(wmb_tinyx_timer_test PRIVATE wmb_tinyx_config)
target_compile_options(wmb_tinyx_timer_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME tinyx_p4_timers COMMAND wmb_tinyx_timer_test)
set_tests_properties(tinyx_p4_timers PROPERTIES TIMEOUT 10)

add_library(wmb_xlib_portability_test OBJECT
    "${WMB_SOURCE_ROOT}/transport/x11/tests/profile_test.c")
target_link_libraries(wmb_xlib_portability_test PRIVATE wmb_xlib)
target_compile_options(wmb_xlib_portability_test PRIVATE -Wall -Wextra -Werror)

add_library(wmb_font_portability_test OBJECT wmb/tests/font_profile_test.c)
target_link_libraries(wmb_font_portability_test PRIVATE wmb_xfont)
target_include_directories(wmb_font_portability_test PRIVATE
    "${WMB_SOURCE_ROOT}" "${CMAKE_BINARY_DIR}/generated/xfont")
target_compile_options(wmb_font_portability_test PRIVATE -Wall -Wextra -Werror)

add_executable(wmb_portability_link wmb/tests/portability_link.c
    wmb/tests/contract_stubs.c $<TARGET_OBJECTS:wmb_xlib_portability_test>
    $<TARGET_OBJECTS:wmb_font_portability_test>)
set(extra_floor_archives)
if(TARGET wmb_regex)
    list(APPEND extra_floor_archives "$<TARGET_FILE:wmb_regex>")
    add_dependencies(wmb_portability_link wmb_regex)
endif()
target_include_directories(wmb_portability_link PRIVATE
    $<TARGET_PROPERTY:wmb_kdrive,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:wmb_xfont,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:wmb_framework,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(wmb_portability_link PRIVATE wmb_tinyx_config
    -Wl,--start-group -Wl,--whole-archive
    "$<TARGET_FILE:wmb_tinyx>" "$<TARGET_FILE:wmb_kdrive>"
    "$<TARGET_FILE:wmb_xlib>" "$<TARGET_FILE:wmb_transport_core>"
    "$<TARGET_FILE:wmb_xfont>" "$<TARGET_FILE:wmb_framework>"
    "$<TARGET_FILE:wmb_jwm>" "$<TARGET_FILE:wmb_applications>"
    "$<TARGET_FILE:wmb_desktop>"
    "$<TARGET_FILE:wmb_api_runtime>" "$<TARGET_FILE:wmb_metrics>"
    ${extra_floor_archives}
    -Wl,--no-whole-archive -Wl,--end-group ZLIB::ZLIB m)
add_dependencies(wmb_portability_link wmb_tinyx wmb_kdrive wmb_xlib
    wmb_transport_core wmb_xfont wmb_framework wmb_jwm wmb_applications wmb_desktop
    wmb_api_runtime wmb_metrics)
target_link_libraries(wmb_portability_link PRIVATE wmb_transport_api)
target_compile_options(wmb_portability_link PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_portability_link PRIVATE -Wl,--no-gc-sections
    -Wl,--exclude-libs,libwmb_xlib.a -Wl,--exclude-libs,libwmb_jwm.a)
add_test(NAME tinyx_p6_detachability COMMAND wmb_portability_link)
set_tests_properties(tinyx_p6_detachability PROPERTIES TIMEOUT 10)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/portability-targets.json" CONTENT
    "{\"archives\":[\"$<TARGET_FILE:wmb_tinyx>\",\"$<TARGET_FILE:wmb_kdrive>\",\"$<TARGET_FILE:wmb_xlib>\",\"$<TARGET_FILE:wmb_xfont>\",\"$<TARGET_FILE:wmb_transport_core>\",\"$<TARGET_FILE:wmb_jwm>\",\"$<TARGET_FILE:wmb_applications>\",\"$<TARGET_FILE:wmb_desktop>\",\"$<TARGET_FILE:wmb_api_runtime>\",\"$<TARGET_FILE:wmb_metrics>\"],\"probe\":\"$<TARGET_FILE:wmb_portability_link>\"}\n")
add_test(NAME tinyx_p6_portability COMMAND "${Python3_EXECUTABLE}"
    "${WMB_SOURCE_ROOT}/tools/check_portability.py" --build-dir "${CMAKE_BINARY_DIR}")
set_tests_properties(tinyx_p6_portability PROPERTIES TIMEOUT 120)
add_test(NAME tinyx_p6_gate_checks COMMAND "${Python3_EXECUTABLE}"
    "${WMB_SOURCE_ROOT}/tools/test_check_portability.py")
