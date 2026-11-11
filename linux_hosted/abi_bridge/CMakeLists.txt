find_package(Threads REQUIRED)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

add_library(wmb_rpc_codec STATIC wire.c codec.c event_codec.c)
target_include_directories(wmb_rpc_codec PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    "${PROJECT_SOURCE_DIR}/transport/api" "${PROJECT_SOURCE_DIR}/libX11/include"
    "${PROJECT_SOURCE_DIR}")
target_compile_definitions(wmb_rpc_codec PRIVATE WMB_INPROC=1)
target_compile_options(wmb_rpc_codec PRIVATE -Wall -Wextra -Werror)
add_executable(wmb_rpc_codec_test codec_test.c)
target_link_libraries(wmb_rpc_codec_test PRIVATE wmb_rpc_codec wmb_xlib)
target_compile_definitions(wmb_rpc_codec_test PRIVATE WMB_INPROC=1)
target_compile_options(wmb_rpc_codec_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
add_test(NAME tinyx_p8_rpc_codec COMMAND wmb_rpc_codec_test)

add_library(wmb_rpc_proxy STATIC proxy.c)
target_include_directories(wmb_rpc_proxy PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(wmb_rpc_proxy PUBLIC wmb_app_wrapper PRIVATE wmb_rpc_codec)
target_compile_options(wmb_rpc_proxy PRIVATE -Wall -Wextra -Werror)
add_library(wmb_rpc_proxy_v1 STATIC proxy.c)
target_include_directories(wmb_rpc_proxy_v1 PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(wmb_rpc_proxy_v1 PUBLIC wmb_app_wrapper_v1 PRIVATE wmb_rpc_codec)
target_compile_options(wmb_rpc_proxy_v1 PRIVATE -Wall -Wextra -Werror)
add_library(wmb_rpc_server STATIC server.c server_helpers.c)
target_include_directories(wmb_rpc_server PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(wmb_rpc_server PRIVATE wmb_xlib wmb_api_runtime
    wmb_rpc_codec Threads::Threads wmb_transport_api)
target_compile_options(wmb_rpc_server PRIVATE -Wall -Wextra -Werror)
add_executable(wmb_rpc_startup_test startup_test.c)
target_link_libraries(wmb_rpc_startup_test PRIVATE wmb_rpc_server)
target_compile_options(wmb_rpc_startup_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(wmb_rpc_startup_test PRIVATE -Wl,--wrap=listen)
add_test(NAME tinyx_p8_rpc_startup COMMAND wmb_rpc_startup_test)
add_executable(wmb_rpc_host server_main.c)
target_link_libraries(wmb_rpc_host PRIVATE wmb_desktop wmb_rpc_server wmb_api_runtime
    wmb_metrics wmb_hosted_display wmb_host_capture Threads::Threads)
target_compile_options(wmb_rpc_host PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_rpc_host PRIVATE -Wl,--gc-sections
    -Wl,--exclude-libs,libwmb_xlib.a -Wl,--exclude-libs,libwmb_jwm.a)
foreach(version current v1)
    if(version STREQUAL current)
        set(proxy wmb_rpc_proxy)
        set(hello wmb_hello)
    else()
        set(proxy wmb_rpc_proxy_v1)
        set(hello wmb_hello_v1)
    endif()
    add_executable(${hello} client_main.c "${PROJECT_SOURCE_DIR}/apps/wmb_hello/hello.c"
        "${PROJECT_SOURCE_DIR}/transport/api/tests/all_macros.c")
    target_link_libraries(${hello} PRIVATE ${proxy})
    target_compile_options(${hello} PRIVATE -Wall -Wextra -Werror)
endforeach()

add_library(wmb_abi_probe_v1 OBJECT sdk_probe.c
    "${PROJECT_SOURCE_DIR}/transport/api/tests/all_macros.c")
target_link_libraries(wmb_abi_probe_v1 PRIVATE wmb_sdk_v1_headers)
target_compile_definitions(wmb_abi_probe_v1 PRIVATE WMB_TEST_LEGACY=1
    wmb_test_all_macros=wmb_v1_all_macros)
target_compile_options(wmb_abi_probe_v1 PRIVATE -Wall -Wextra -Werror
    "SHELL:-include \"${PROJECT_SOURCE_DIR}/wrapper/libX11-app/v1/wmb_test_wrappers.h\"")
add_library(wmb_abi_probe_v2 OBJECT sdk_probe.c
    "${PROJECT_SOURCE_DIR}/transport/api/tests/all_macros.c")
target_link_libraries(wmb_abi_probe_v2 PRIVATE wmb_sdk_headers)
target_compile_definitions(wmb_abi_probe_v2 PRIVATE wmb_test_all_macros=wmb_v2_all_macros)
target_compile_options(wmb_abi_probe_v2 PRIVATE -Wall -Wextra -Werror)

add_library(wmb_abi_thread_test OBJECT thread_test.c)
target_link_libraries(wmb_abi_thread_test PRIVATE wmb_xlib Threads::Threads wmb_transport_api)
target_compile_options(wmb_abi_thread_test PRIVATE -Wall -Wextra -Werror)

add_executable(wmb_abi_host main.c tenant.c
    $<TARGET_OBJECTS:wmb_abi_probe_v1> $<TARGET_OBJECTS:wmb_abi_probe_v2>
    $<TARGET_OBJECTS:wmb_abi_thread_test>)
target_include_directories(wmb_abi_host PRIVATE
    "${PROJECT_SOURCE_DIR}/jwm/wmb" "${PROJECT_SOURCE_DIR}/transport/x11"
    "${PROJECT_SOURCE_DIR}/linux_hosted/sdl_drivers")
target_link_libraries(wmb_abi_host PRIVATE wmb_desktop wmb_applications
    wmb_api_runtime wmb_metrics wmb_hosted_display wmb_sdl_library Threads::Threads
    "$<TARGET_FILE:wmb_app_wrapper_v1_test>" "$<TARGET_FILE:wmb_app_wrapper>")
add_dependencies(wmb_abi_host wmb_app_wrapper_v1_test wmb_app_wrapper)
target_compile_options(wmb_abi_host PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_abi_host PRIVATE -Wl,--gc-sections
    -Wl,--exclude-libs,libwmb_xlib.a -Wl,--exclude-libs,libwmb_jwm.a)
add_test(NAME tinyx_p8_native COMMAND wmb_abi_host)
add_test(NAME tinyx_p8_cycle_smoke COMMAND wmb_abi_host --cycle-smoke)
set_tests_properties(tinyx_p8_cycle_smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P8: ordered WM/apps/server shutdown PASS" TIMEOUT 90)
add_test(NAME tinyx_p8_sampling COMMAND wmb_abi_host --sampling-test)
set_tests_properties(tinyx_p8_sampling PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P8: ordered WM/apps/server shutdown PASS" TIMEOUT 20)
set_tests_properties(tinyx_p8_native PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P8: ordered WM/apps/server shutdown PASS" TIMEOUT 40)
add_test(NAME tinyx_p8_cycles COMMAND wmb_abi_host --cycles)
set_tests_properties(tinyx_p8_cycles PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P8: ordered WM/apps/server shutdown PASS" TIMEOUT 3600)
add_test(NAME tinyx_p8_hour COMMAND wmb_abi_host --stability 3600)
set_tests_properties(tinyx_p8_hour PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P8: ordered WM/apps/server shutdown PASS"
    LABELS endurance TIMEOUT 3700)

add_executable(wmb_gateway_test gateway_test.c)
target_link_libraries(wmb_gateway_test PRIVATE wmb_desktop wmb_xlib
    wmb_api_runtime wmb_hosted_display Threads::Threads)
target_compile_options(wmb_gateway_test PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_gateway_test PRIVATE -Wl,--gc-sections)
add_test(NAME tinyx_p8_gateway_cancel COMMAND wmb_gateway_test)
set_tests_properties(tinyx_p8_gateway_cancel PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "all borrowed calls drained PASS" TIMEOUT 10)
add_test(NAME tinyx_p8_sdk COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/check_application_abi.py"
    --build-dir "${CMAKE_BINARY_DIR}" --archive "$<TARGET_FILE:wmb_app_wrapper>"
    --application "$<TARGET_FILE:wmb_hello>" --gateway "$<TARGET_FILE:wmb_gateway_test>")
set_tests_properties(tinyx_p8_sdk PROPERTIES TIMEOUT 30)
add_test(NAME tinyx_p8_process_bridge COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/run.py" --server "$<TARGET_FILE:wmb_rpc_host>"
    --client "$<TARGET_FILE:wmb_hello>" --legacy "$<TARGET_FILE:wmb_hello_v1>")
set_tests_properties(tinyx_p8_process_bridge PROPERTIES TIMEOUT 100)
set_tests_properties(tinyx_p8_cycles PROPERTIES LABELS endurance)
