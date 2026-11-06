execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BINARY_DIR}"
    "-DWMB_SDL_PIXEL_BITS=${PIXEL_BITS}"
    "-DWMB_SDL_WIDTH=${SCREEN_WIDTH}"
    "-DWMB_SDL_HEIGHT=${SCREEN_HEIGHT}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}"
    "-DCMAKE_C_FLAGS=${C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${C_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${LINKER_FLAGS}"
    "-DHAL_CONSUMER_BACKEND=${BACKEND}"
    -DWMB_SDL_BUILD_TESTS=OFF
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External consumer configure failed: ${result}")
endif()

file(READ "${BINARY_DIR}/consumer_interface.txt" public_interface)
if(public_interface MATCHES "SDL|sdl_library|/framework|PIXEL_TYPE_T|_USE_SCREEN")
    message(FATAL_ERROR "Backend details leaked into consumer interface: ${public_interface}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --parallel 2
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External consumer build failed: ${result}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
    "${BINARY_DIR}/bin/hal_external_consumer"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External consumer run failed: ${result}")
endif()
