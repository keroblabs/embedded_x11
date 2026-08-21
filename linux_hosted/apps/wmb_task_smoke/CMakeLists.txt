add_executable(wmb_task_smoke ${CMAKE_CURRENT_LIST_DIR}/wmb_task_smoke.c)
target_link_libraries(wmb_task_smoke PRIVATE wmb_hosted)
target_compile_options(wmb_task_smoke PRIVATE -Wall -Wextra)
