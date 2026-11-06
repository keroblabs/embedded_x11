cmake_minimum_required(VERSION 3.16)

set(KDRIVE_TEST_DEFAULT OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(wmb_kdrive_hal C)
    set(KDRIVE_TEST_DEFAULT ON)
endif()

get_filename_component(WMB_SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
set(TINYX_SOURCE_ROOT ${WMB_SOURCE_ROOT}/tinyx)
set(KDRIVE_CORE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../src)
include(${TINYX_SOURCE_ROOT}/wmb_devel.cmake)

if(NOT TARGET wmb_hal)
    add_library(wmb_hal INTERFACE)
    target_include_directories(wmb_hal INTERFACE ${WMB_SOURCE_ROOT}/hal)
endif()

add_library(wmb_kdrive STATIC
    ${KDRIVE_CORE_ROOT}/kdrive.c
    ${KDRIVE_CORE_ROOT}/kinfo.c
    ${KDRIVE_CORE_ROOT}/kinput.c
    ${KDRIVE_CORE_ROOT}/kcmap.c
    ${KDRIVE_CORE_ROOT}/kmode.c
    ${KDRIVE_CORE_ROOT}/kshadow.c
    hal_display.c
    hal_input.c
    hal_os.c
)
target_compile_features(wmb_kdrive PUBLIC c_std_11)
target_include_directories(wmb_kdrive PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${KDRIVE_CORE_ROOT}
    ${WMB_SOURCE_ROOT}
    ${TINYX_SOURCE_ROOT}/include
    ${TINYX_SOURCE_ROOT}/fb
    ${TINYX_SOURCE_ROOT}/mi
    ${TINYX_SOURCE_ROOT}/render
    ${TINYX_SOURCE_ROOT}/randr
    ${TINYX_SOURCE_ROOT}/xkb
    ${TINYX_SOURCE_ROOT}/Xext
    ${TINYX_SOURCE_ROOT}/dix
    ${TINYX_SOURCE_ROOT}/os
    ${TINYX_SOURCE_ROOT}/miext/damage
    ${TINYX_SOURCE_ROOT}/miext/shadow
)
target_link_libraries(wmb_kdrive PUBLIC wmb_tinyx_project wmb_hal m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wmb_kdrive PRIVATE -ffunction-sections -fdata-sections)
endif()

option(WMB_KDRIVE_BUILD_TESTS "Build the standalone KDrive/HAL harness" ${KDRIVE_TEST_DEFAULT})
if(WMB_KDRIVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
