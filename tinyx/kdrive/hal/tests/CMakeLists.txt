add_executable(wmb_kdrive_harness
    kdrive_harness.c
    mock_hal.c
    mock_xserver.c
)
target_link_libraries(wmb_kdrive_harness PRIVATE wmb_kdrive)

add_library(wmb_kdrive_project_probe OBJECT kdrive_header_probe.c)
target_link_libraries(wmb_kdrive_project_probe PRIVATE wmb_kdrive)
target_compile_definitions(wmb_kdrive_project_probe PRIVATE WMB_TEST_EXTENDED_BUTTONS=1)

foreach(variant reference zero empty)
    add_library(wmb_kdrive_${variant}_probe OBJECT
        kdrive_header_probe.c
        ${KDRIVE_CORE_ROOT}/kdrive.c
        ${KDRIVE_CORE_ROOT}/kinput.c
    )
    target_compile_features(wmb_kdrive_${variant}_probe PRIVATE c_std_11)
    if(variant STREQUAL "reference")
        target_compile_definitions(wmb_kdrive_${variant}_probe PRIVATE WMB_TEST_EXTENDED_BUTTONS=0)
    else()
        target_compile_definitions(wmb_kdrive_${variant}_probe PRIVATE WMB_TEST_EXTENDED_BUTTONS=1)
    endif()
    target_include_directories(wmb_kdrive_${variant}_probe PRIVATE
        $<TARGET_PROPERTY:wmb_kdrive,INTERFACE_INCLUDE_DIRECTORIES>)
endforeach()
target_compile_definitions(wmb_kdrive_zero_probe PRIVATE WMB_DEVEL=0)
target_compile_definitions(wmb_kdrive_empty_probe PRIVATE WMB_DEVEL=)
add_dependencies(wmb_kdrive_harness
    wmb_kdrive_project_probe wmb_kdrive_reference_probe
    wmb_kdrive_zero_probe wmb_kdrive_empty_probe)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wmb_kdrive_harness PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
    target_link_options(wmb_kdrive_harness PRIVATE -Wl,--gc-sections -Wl,--wrap=calloc)
endif()

foreach(scenario framebuffer damage close keyboard pointer lifecycle faults core arguments)
    add_test(NAME kdrive_hal_${scenario} COMMAND wmb_kdrive_harness ${scenario})
    set_tests_properties(kdrive_hal_${scenario} PROPERTIES TIMEOUT 30)
endforeach()
