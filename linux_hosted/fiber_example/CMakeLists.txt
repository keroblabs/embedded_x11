add_executable(fiber_example ${CMAKE_CURRENT_LIST_DIR}/fiber_example.c)
target_link_libraries(fiber_example PRIVATE wmb_hosted)
target_compile_options(fiber_example PRIVATE -Wall -Wextra)
