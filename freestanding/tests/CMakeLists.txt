add_library(wmb_system_regex OBJECT regex_reference.c)
target_compile_options(wmb_system_regex PRIVATE -Wall -Wextra -Werror)
add_executable(wmb_floor_regex_test regex_test.c $<TARGET_OBJECTS:wmb_system_regex>)
target_link_libraries(wmb_floor_regex_test PRIVATE
    "$<TARGET_FILE:wmb_jwm>" wmb_regex wmb_hosted)
target_include_directories(wmb_floor_regex_test PRIVATE "${PROJECT_SOURCE_DIR}/jwm/src")
add_dependencies(wmb_floor_regex_test wmb_jwm)
target_compile_options(wmb_floor_regex_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(wmb_floor_regex_test PRIVATE -Wl,--wrap=wmb_alloc)
add_test(NAME tinyx_p9a_regex COMMAND wmb_floor_regex_test)
add_executable(wmb_floor_runtime_test runtime_test.c ../p9a/runtime.c)
target_link_libraries(wmb_floor_runtime_test PRIVATE wmb_hal)
target_include_directories(wmb_floor_runtime_test PRIVATE "${PROJECT_SOURCE_DIR}/freestanding/p9a")
target_compile_options(wmb_floor_runtime_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_options(wmb_floor_runtime_test PRIVATE
    -Wl,--defsym,__heap_start=wmb_test_heap
    -Wl,--defsym,__heap_end=wmb_test_heap+256)
add_test(NAME tinyx_p9a_runtime COMMAND wmb_floor_runtime_test)
