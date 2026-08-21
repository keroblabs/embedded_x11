add_executable(sdl_fiber_example ${CMAKE_CURRENT_LIST_DIR}/sdl_fiber_example.c)
target_link_libraries(sdl_fiber_example PRIVATE wmb_hosted_sdl)
target_compile_options(sdl_fiber_example PRIVATE -Wall -Wextra)
