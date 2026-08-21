add_executable(wmb_fiber_smoke ${CMAKE_CURRENT_LIST_DIR}/wmb_fiber_smoke.c)
target_link_libraries(wmb_fiber_smoke PRIVATE wmb_hosted)
target_compile_options(wmb_fiber_smoke PRIVATE -Wall -Wextra)
