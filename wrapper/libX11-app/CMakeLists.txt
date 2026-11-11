cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(wmb_application_sdk C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()
get_filename_component(WMB_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(wmb_sdk_headers INTERFACE)
target_include_directories(wmb_sdk_headers INTERFACE
    "${WMB_SDK_ROOT}/transport/api" "${CMAKE_CURRENT_LIST_DIR}"
    "${WMB_SDK_ROOT}/libX11/include" "${WMB_SDK_ROOT}")
target_compile_definitions(wmb_sdk_headers INTERFACE WMB_INPROC=1 WMB_APP_OPAQUE=1)
target_compile_options(wmb_sdk_headers INTERFACE
    "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/wmb_app_namespace.h\""
    "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/wmb_xlib_compat.h\"")
add_library(wmb_app_wrapper STATIC wrappers.c)
target_link_libraries(wmb_app_wrapper PUBLIC wmb_sdk_headers)
target_compile_features(wmb_app_wrapper PRIVATE c_std_11)
target_compile_options(wmb_app_wrapper PRIVATE
    -Os -g0 -fno-function-sections -fno-data-sections
    -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror)
set_target_properties(wmb_app_wrapper PROPERTIES OUTPUT_NAME X11-app)

add_library(wmb_sdk_v1_headers INTERFACE)
target_include_directories(wmb_sdk_v1_headers INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/v1" "${CMAKE_CURRENT_LIST_DIR}"
    "${WMB_SDK_ROOT}/libX11/include" "${WMB_SDK_ROOT}")
target_compile_definitions(wmb_sdk_v1_headers INTERFACE WMB_INPROC=1 WMB_APP_OPAQUE=1)
target_compile_options(wmb_sdk_v1_headers INTERFACE
    "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/v1/wmb_app_namespace.h\""
    "SHELL:-include \"${CMAKE_CURRENT_LIST_DIR}/wmb_xlib_compat.h\"")
add_library(wmb_app_wrapper_v1 STATIC v1/wrappers.c)
target_link_libraries(wmb_app_wrapper_v1 PUBLIC wmb_sdk_v1_headers)
target_compile_features(wmb_app_wrapper_v1 PRIVATE c_std_11)
target_compile_options(wmb_app_wrapper_v1 PRIVATE
    -Os -g0 -fno-function-sections -fno-data-sections
    -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror)
set_target_properties(wmb_app_wrapper_v1 PROPERTIES OUTPUT_NAME X11-app-v1)
