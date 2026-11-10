set(JWM_ROOT "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB JWM_SOURCES CONFIGURE_DEPENDS "${JWM_ROOT}/src/*.c")
list(REMOVE_ITEM JWM_SOURCES "${JWM_ROOT}/src/timing.c")
configure_file("${PROJECT_SOURCE_DIR}/cmake/jwm-config.h.in"
    "${CMAKE_BINARY_DIR}/generated/jwm/jwm_config.h" @ONLY)
add_library(wmb_jwm STATIC ${JWM_SOURCES}
    wmb/jwm_entry.c wmb/jwm_config.c wmb/timing_wmb.c)
target_include_directories(wmb_jwm PRIVATE "${JWM_ROOT}/src"
    "${CMAKE_BINARY_DIR}/generated/jwm" PUBLIC "${JWM_ROOT}/wmb")
target_link_libraries(wmb_jwm PRIVATE wmb_xlib wmb_transport_api m)
target_compile_options(wmb_jwm PRIVATE -include "${JWM_ROOT}/wmb/jwm_namespace.h"
    -ffunction-sections -fdata-sections)
set_target_properties(wmb_jwm PROPERTIES C_VISIBILITY_PRESET hidden)

get_target_property(selected_jwm_sources wmb_jwm SOURCES)
set(jwm_source_manifest)
foreach(source IN LISTS selected_jwm_sources)
    if(NOT IS_ABSOLUTE "${source}")
        set(source "${JWM_ROOT}/${source}")
    endif()
    file(RELATIVE_PATH source "${PROJECT_SOURCE_DIR}" "${source}")
    list(APPEND jwm_source_manifest "${source}")
endforeach()
list(SORT jwm_source_manifest)
file(STRINGS "${PROJECT_SOURCE_DIR}/docs/reference/jwm-sources.reference"
    reference_jwm_sources REGEX "^[^#]")
if(NOT "${jwm_source_manifest}" STREQUAL "${reference_jwm_sources}")
    message(FATAL_ERROR "JWM source cohort differs from docs/reference/jwm-sources.reference")
endif()

option(WMB_JWM_BUILD_REFERENCE "Build the independent stock-Xlib JWM harness" OFF)
if(WMB_JWM_BUILD_REFERENCE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WMB_JWM_SYSTEM_X11 REQUIRED IMPORTED_TARGET x11)
    find_program(WMB_JWM_XVFB NAMES Xvfb)
    if(NOT WMB_JWM_XVFB)
        message(FATAL_ERROR "The optional stock-Xlib reference harness requires Xvfb")
    endif()
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_executable(wmb_jwm_reference "${PROJECT_SOURCE_DIR}/linux_hosted/jwm_standalone/main.c")
    target_include_directories(wmb_jwm_reference PRIVATE
        "${JWM_ROOT}/wmb" "${PROJECT_SOURCE_DIR}/transport/x11"
        "${PROJECT_SOURCE_DIR}/libX11/include" "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(wmb_jwm_reference PRIVATE WMB_INPROC=1)
    target_compile_options(wmb_jwm_reference PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(wmb_jwm_reference PRIVATE "$<TARGET_FILE:wmb_jwm>"
        wmb_applications wmb_hosted PkgConfig::WMB_JWM_SYSTEM_X11 m)
    add_dependencies(wmb_jwm_reference wmb_jwm)
    add_test(NAME tinyx_p7_standalone COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/linux_hosted/jwm_standalone/run.py"
        --xvfb "${WMB_JWM_XVFB}" --program "$<TARGET_FILE:wmb_jwm_reference>")
    set_tests_properties(tinyx_p7_standalone PROPERTIES TIMEOUT 20)
endif()
