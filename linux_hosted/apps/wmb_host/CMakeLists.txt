add_library(wmb_desktop_clients OBJECT desktop_client.c wm_test.c)
target_link_libraries(wmb_desktop_clients PRIVATE wmb_xlib wmb_transport_api wmb_jwm)
target_compile_options(wmb_desktop_clients PRIVATE -Wall -Wextra -Werror)

add_executable(wmb_host wmb_host.c input_test.c desktop.c)
target_link_libraries(wmb_host PRIVATE
    wmb_xlib_test_client wmb_xlib wmb_jwm
    -Wl,--start-group wmb_tinyx wmb_kdrive -Wl,--end-group
    wmb_hosted_display wmb_transport_test_client
    wmb_sdl_library wmb_desktop wmb_desktop_clients)
target_include_directories(wmb_host PRIVATE "${PROJECT_SOURCE_DIR}/linux_hosted/sdl_drivers")
target_compile_options(wmb_host PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_host PRIVATE -Wl,--gc-sections -Wl,--wrap=hal_display_flush
    -Wl,--wrap=fiber_sched_swap
    -Wl,--exclude-libs,libwmb_xlib.a -Wl,--exclude-libs,libwmb_jwm.a)
add_test(NAME tinyx_p4_root COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:wmb_host>"
    "-DREFERENCE=${PROJECT_SOURCE_DIR}/docs/reference/tinyx-startup.reference.cmake"
    "-DWIDTH=${WMB_SDL_WIDTH}" "-DHEIGHT=${WMB_SDL_HEIGHT}"
    "-DPIXEL_BITS=${WMB_SDL_PIXEL_BITS}"
    -P "${PROJECT_SOURCE_DIR}/tinyx/wmb/tests/check_startup.cmake")
set_tests_properties(tinyx_p4_root PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P4: root=valid.*PASS" TIMEOUT 15)
add_test(NAME tinyx_p5_raw COMMAND wmb_host --raw-test)
set_tests_properties(tinyx_p5_raw PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P5 raw:.*PASS" TIMEOUT 15)
add_test(NAME tinyx_p5_client COMMAND wmb_host --client-test)
set_tests_properties(tinyx_p5_client PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;RESOURCE_NAME=ignored-native-environment"
    PASS_REGULAR_EXPRESSION "P5 client PASS: close" TIMEOUT 35)
add_test(NAME tinyx_p5_flow COMMAND wmb_host --flow-test)
set_tests_properties(tinyx_p5_flow PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P5 flow:.*PASS" TIMEOUT 15)
add_test(NAME tinyx_p5_reply_timeout COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:wmb_host>"
    -P "${PROJECT_SOURCE_DIR}/transport/x11/check_timeout.cmake")
set_tests_properties(tinyx_p5_reply_timeout PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    TIMEOUT 15)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_test(NAME tinyx_p5_wire COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:wmb_host>"
    "-DPYTHON=${Python3_EXECUTABLE}"
    "-DSCRIPT=${PROJECT_SOURCE_DIR}/transport/x11/tests/compare_wire.py"
    "-DCAPTURE=${CMAKE_CURRENT_BINARY_DIR}/subject.wire"
    "-DGOLDEN=${PROJECT_SOURCE_DIR}/docs/reference/xlib-wire-golden-${WMB_SDL_PIXEL_BITS}.json"
    -P "${PROJECT_SOURCE_DIR}/transport/x11/tests/check_wire.cmake")
set_tests_properties(tinyx_p5_wire PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "PASS P5 wire" TIMEOUT 20)
add_test(NAME tinyx_p5_wire_parser COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/transport/x11/tests/test_compare_wire.py")

add_test(NAME tinyx_p7_desktop COMMAND wmb_host --wm-test)
set_tests_properties(tinyx_p7_desktop PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P7 desktop: orderly stop PASS" TIMEOUT 35)
add_test(NAME tinyx_p7_namespace COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/jwm_namespace.py" --archive "$<TARGET_FILE:wmb_jwm>")
foreach(mode menu move resize)
    add_test(NAME tinyx_p7_cancel_${mode} COMMAND wmb_host --wm-cancel-${mode})
    set_tests_properties(tinyx_p7_cancel_${mode} PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        PASS_REGULAR_EXPRESSION "P7 desktop: orderly stop PASS" TIMEOUT 20)
endforeach()
