add_executable(wmb_host wmb_host.c)
target_link_libraries(wmb_host PRIVATE
    -Wl,--start-group wmb_tinyx wmb_kdrive -Wl,--end-group wmb_hosted_display)
target_compile_options(wmb_host PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_host PRIVATE -Wl,--gc-sections -Wl,--wrap=hal_display_flush)
add_test(NAME tinyx_p4_root COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:wmb_host>"
    "-DREFERENCE=${PROJECT_SOURCE_DIR}/docs/reference/tinyx-startup.reference.cmake"
    "-DWIDTH=${WMB_SDL_WIDTH}" "-DHEIGHT=${WMB_SDL_HEIGHT}"
    "-DPIXEL_BITS=${WMB_SDL_PIXEL_BITS}"
    -P "${PROJECT_SOURCE_DIR}/tinyx/wmb/tests/check_startup.cmake")
set_tests_properties(tinyx_p4_root PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
    PASS_REGULAR_EXPRESSION "P4: root=valid.*PASS" TIMEOUT 15)
