add_executable(wmb_sdl_driver_test sdl_driver_test.c)
target_link_libraries(wmb_sdl_driver_test PRIVATE wmb_display_input wmb_sdl_library)
target_compile_definitions(wmb_sdl_driver_test PRIVATE
    TEST_NATIVE_WIDTH=${WMB_SDL_WIDTH}
    TEST_NATIVE_HEIGHT=${WMB_SDL_HEIGHT}
)
target_link_options(wmb_sdl_driver_test PRIVATE
    -Wl,--wrap=SDL_calloc
    -Wl,--wrap=SDL_CreateWindow
    -Wl,--wrap=SDL_CreateRenderer
    -Wl,--wrap=SDL_CreateRGBSurfaceWithFormat
    -Wl,--wrap=SDL_CreateTexture
    -Wl,--wrap=SDL_UpdateTexture
)
target_include_directories(wmb_sdl_driver_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../../sdl_library
)

foreach(scenario display keyboard mouse lifecycle ownership)
    add_test(NAME sdl_${scenario} COMMAND wmb_sdl_driver_test ${scenario})
    set_tests_properties(sdl_${scenario} PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
        TIMEOUT 30
    )
endforeach()

add_test(NAME sdl_init_failure COMMAND wmb_sdl_driver_test init-failure)
set_tests_properties(sdl_init_failure PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=wmb-invalid-video-driver;SDL_RENDER_DRIVER=software"
    TIMEOUT 30
)

add_test(NAME sdl_external_consumer COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/integration
    -DBACKEND=sdl
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/integration
    -DPIXEL_BITS=${WMB_SDL_PIXEL_BITS}
    -DSCREEN_WIDTH=${WMB_SDL_WIDTH}
    -DSCREEN_HEIGHT=${WMB_SDL_HEIGHT}
    -DC_COMPILER=${CMAKE_C_COMPILER}
    "-DC_FLAGS=${CMAKE_C_FLAGS}"
    "-DLINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    -P ${CMAKE_CURRENT_LIST_DIR}/integration.cmake
)
set_tests_properties(sdl_external_consumer PROPERTIES TIMEOUT 120)

add_test(NAME hal_reference_consumer COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/integration
    -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/reference-consumer
    -DBACKEND=reference
    -DPIXEL_BITS=${WMB_SDL_PIXEL_BITS}
    -DC_COMPILER=${CMAKE_C_COMPILER}
    "-DC_FLAGS=${CMAKE_C_FLAGS}"
    "-DLINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
    -P ${CMAKE_CURRENT_LIST_DIR}/integration.cmake
)
set_tests_properties(hal_reference_consumer PROPERTIES TIMEOUT 120)
