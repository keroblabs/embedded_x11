add_library(wmb_transport_core STATIC wmb_queue.c)
target_include_directories(wmb_transport_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(wmb_transport_core PUBLIC wmb_framework)
target_compile_options(wmb_transport_core PRIVATE -Wall -Wextra -Werror)

add_library(wmb_transport_test_client STATIC wmb_raw_test.c)
target_link_libraries(wmb_transport_test_client PUBLIC wmb_transport_core)
target_compile_options(wmb_transport_test_client PRIVATE -Wall -Wextra -Werror)

add_executable(wmb_transport_test wmb_transport_test.c)
target_link_libraries(wmb_transport_test PRIVATE wmb_transport_core)
target_compile_options(wmb_transport_test PRIVATE -Wall -Wextra -Werror)
target_link_options(wmb_transport_test PRIVATE -Wl,--wrap=wmb_alloc)
add_test(NAME tinyx_p5_transport COMMAND wmb_transport_test)
