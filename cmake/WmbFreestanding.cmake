if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The approved P9a reference uses a 32-bit ABI")
endif()
foreach(host_option WMB_KDRIVE_BUILD_TESTS WMB_SDL_BUILD_TESTS WMB_JWM_BUILD_REFERENCE WMB_BUILD_WIRE_REFERENCE)
    if(${host_option})
        message(FATAL_ERROR "${host_option} is hosted-only; use a separate P9a build directory")
    endif()
endforeach()
enable_testing()
set(WMB_LOAD_BASE "0x08000000" CACHE STRING "Synthetic P9a ROM/API base, not a board address")
set(WMB_ROM_BYTES "0x400000" CACHE STRING "Synthetic P9a ROM budget")
set(WMB_RAM_BASE "0x20000000" CACHE STRING "Synthetic P9a RAM base")
set(WMB_RAM_BYTES "0x800000" CACHE STRING "Synthetic P9a RAM budget")
set(WMB_HEAP_BYTES "0x400000" CACHE STRING "Synthetic bounded newlib heap reservation")
set(WMB_STACK_BYTES "0x210000" CACHE STRING "Synthetic runtime/main stack reservation")
set(WMB_APP_BASE "0x08400000" CACHE STRING "Separate SDK application ROM base")
set(WMB_APP_RAM_BASE "0x20800000" CACHE STRING "Disjoint synthetic SDK application RAM base")
set(WMB_APP_RAM_BYTES "0x100000" CACHE STRING "Synthetic SDK application RAM budget")
set(WMB_APP_HEAP_BYTES "0x80000" CACHE STRING "Synthetic SDK application heap reservation")
set(WMB_APP_STACK_BYTES "0x10000" CACHE STRING "Synthetic SDK application stack reservation")
target_compile_definitions(wmb_framework PUBLIC FIBER_PORT_STACK_ALIGN=8u FIBER_DEFAULT_STACK=65536u)
add_library(wmb_p9a_platform STATIC
    "${PROJECT_SOURCE_DIR}/freestanding/p9a/platform_stubs.c"
    "${PROJECT_SOURCE_DIR}/freestanding/p9a/runtime.c")
target_include_directories(wmb_p9a_platform PUBLIC
    "${PROJECT_SOURCE_DIR}/freestanding/p9a")
target_link_libraries(wmb_p9a_platform PUBLIC wmb_framework)
target_link_libraries(wmb_framework PUBLIC wmb_p9a_platform)
add_subdirectory(tinyx/kdrive/hal)
add_subdirectory(transport/x11)
add_subdirectory(libX11)
include("${PROJECT_SOURCE_DIR}/cmake/WmbRegex.cmake")
add_subdirectory(jwm)
target_link_libraries(wmb_jwm PRIVATE wmb_regex)
add_subdirectory(tinyx)
add_subdirectory(transport/api)
set(wmb_archives wmb_tinyx wmb_kdrive wmb_xlib wmb_xfont wmb_transport_core
    wmb_jwm wmb_applications wmb_desktop wmb_api_runtime wmb_metrics wmb_framework)
set(raw_archives)
foreach(archive IN LISTS wmb_archives)
    list(APPEND raw_archives "$<TARGET_FILE:${archive}>")
endforeach()
file(STRINGS "${PROJECT_SOURCE_DIR}/transport/api/wmb_api.h" api_count
    REGEX "^#define WMB_API_SLOT_COUNT [0-9]+u$")
if(NOT api_count MATCHES "WMB_API_SLOT_COUNT ([0-9]+)u")
    message(FATAL_ERROR "Cannot determine the generated ABI slot count")
endif()
math(EXPR WMB_API_TABLE_BYTES "16 + ${CMAKE_MATCH_1} * ${CMAKE_SIZEOF_VOID_P}")
set(WMB_API_COUNT "${CMAKE_MATCH_1}")
file(STRINGS "${PROJECT_SOURCE_DIR}/transport/api/wmb_api.h" api_version
    REGEX "^#define WMB_API_VERSION [0-9]+u$")
if(NOT api_version MATCHES "WMB_API_VERSION ([0-9]+)u")
    message(FATAL_ERROR "Cannot determine the generated ABI version")
endif()
set(WMB_API_VERSION "${CMAKE_MATCH_1}")
file(STRINGS "${PROJECT_SOURCE_DIR}/transport/api/wmb_api.h" api_magic
    REGEX "^#define WMB_API_MAGIC ")
if(NOT api_magic MATCHES "WMB_API_MAGIC UINT32_C\\((0x[0-9a-fA-F]+)\\)")
    message(FATAL_ERROR "Cannot determine the generated ABI magic")
endif()
set(WMB_API_MAGIC "${CMAKE_MATCH_1}")
configure_file("${PROJECT_SOURCE_DIR}/freestanding/p9a/wmb.ld.in"
    "${CMAKE_BINARY_DIR}/generated/p9a/wmb.ld" @ONLY)
foreach(image wmb_p9a wmb_p9a_whole)
    add_executable(${image} "${PROJECT_SOURCE_DIR}/freestanding/p9a/boot.c")
    set_target_properties(${image} PROPERTIES SUFFIX ".elf")
    target_link_libraries(${image} PRIVATE wmb_transport_api)
    add_dependencies(${image} ${wmb_archives} wmb_p9a_platform wmb_zlib wmb_regex)
    if(image STREQUAL wmb_p9a_whole)
        target_link_libraries(${image} PRIVATE -Wl,--start-group -Wl,--whole-archive
            ${raw_archives} "$<TARGET_FILE:wmb_zlib>" "$<TARGET_FILE:wmb_regex>"
            "$<TARGET_FILE:wmb_p9a_platform>" -Wl,--no-whole-archive
            m c gcc -Wl,--end-group)
        target_link_options(${image} PRIVATE -Wl,--no-gc-sections)
    else()
        target_link_libraries(${image} PRIVATE -Wl,--start-group ${raw_archives}
            "$<TARGET_FILE:wmb_p9a_platform>" "$<TARGET_FILE:wmb_zlib>"
            "$<TARGET_FILE:wmb_regex>" m c gcc -Wl,--end-group)
        target_link_options(${image} PRIVATE -Wl,--gc-sections)
    endif()
    target_link_options(${image} PRIVATE
        "-T${CMAKE_BINARY_DIR}/generated/p9a/wmb.ld"
        "-Wl,-Map,${CMAKE_BINARY_DIR}/${image}.map" -Wl,--cref)
    set_property(TARGET ${image} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_BINARY_DIR}/generated/p9a/wmb.ld")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/freestanding/p9a/app.ld.in"
    "${CMAKE_BINARY_DIR}/generated/p9a/app.ld" @ONLY)
add_executable(wmb_p9a_app "${PROJECT_SOURCE_DIR}/apps/wmb_hello/hello.c"
    "${PROJECT_SOURCE_DIR}/transport/api/tests/all_macros.c"
    "${PROJECT_SOURCE_DIR}/freestanding/p9a/application.c")
set_target_properties(wmb_p9a_app PROPERTIES SUFFIX ".elf")
target_link_libraries(wmb_p9a_app PRIVATE wmb_app_wrapper -Wl,--start-group
    "$<TARGET_FILE:wmb_p9a_platform>" m c gcc -Wl,--end-group)
target_compile_definitions(wmb_p9a_app PRIVATE WMB_API_ADDRESS=${WMB_LOAD_BASE})
add_dependencies(wmb_p9a_app wmb_p9a_platform)
target_link_options(wmb_p9a_app PRIVATE "-T${CMAKE_BINARY_DIR}/generated/p9a/app.ld"
    "-Wl,-Map,${CMAKE_BINARY_DIR}/wmb_p9a_app.map" -Wl,--gc-sections)
set_property(TARGET wmb_p9a_app APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_BINARY_DIR}/generated/p9a/app.ld")
add_custom_command(TARGET wmb_p9a POST_BUILD COMMAND "${CMAKE_OBJCOPY}"
    -O binary "$<TARGET_FILE:wmb_p9a>" "${CMAKE_BINARY_DIR}/bin/wmb_p9a.bin" VERBATIM)
set(archive_json)
foreach(archive IN LISTS wmb_archives ITEMS wmb_zlib wmb_regex wmb_p9a_platform)
    list(APPEND archive_json "\"$<TARGET_FILE:${archive}>\"")
endforeach()
list(JOIN archive_json "," archive_json)
set(library_json)
foreach(library c m gcc)
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -mcpu=cortex-m3 -mthumb
        "-print-file-name=lib${library}.a"
        OUTPUT_VARIABLE library_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "The P9a compiler does not provide lib${library}.a")
    endif()
    list(APPEND library_json "\"${library}\":\"${library_path}\"")
endforeach()
list(JOIN library_json "," library_json)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/p9a-targets.json" CONTENT
    "{\"compiler\":\"${CMAKE_C_COMPILER}\",\"nm\":\"${CMAKE_NM}\",\"archives\":[${archive_json}],\"libraries\":{${library_json}},\"image\":\"$<TARGET_FILE:wmb_p9a>\",\"whole\":\"$<TARGET_FILE:wmb_p9a_whole>\",\"application\":\"$<TARGET_FILE:wmb_p9a_app>\",\"image_map\":\"${CMAKE_BINARY_DIR}/wmb_p9a.map\",\"whole_map\":\"${CMAKE_BINARY_DIR}/wmb_p9a_whole.map\",\"application_map\":\"${CMAKE_BINARY_DIR}/wmb_p9a_app.map\",\"raw\":\"${CMAKE_BINARY_DIR}/bin/wmb_p9a.bin\",\"sdk\":\"$<TARGET_FILE:wmb_app_wrapper>\",\"load_base\":\"${WMB_LOAD_BASE}\",\"app_base\":\"${WMB_APP_BASE}\",\"rom_bytes\":\"${WMB_ROM_BYTES}\",\"ram_base\":\"${WMB_RAM_BASE}\",\"ram_bytes\":\"${WMB_RAM_BYTES}\",\"app_ram_base\":\"${WMB_APP_RAM_BASE}\",\"app_ram_bytes\":\"${WMB_APP_RAM_BYTES}\",\"api_magic\":\"${WMB_API_MAGIC}\",\"api_version\":${WMB_API_VERSION},\"api_count\":${WMB_API_COUNT}}\n")
add_test(NAME tinyx_p9a_freestanding COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/check_freestanding.py" --build-dir "${CMAKE_BINARY_DIR}")
add_test(NAME tinyx_p9a_verifier_failures COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/test_check_freestanding.py" "${CMAKE_BINARY_DIR}")
add_custom_target(wmb_p9a_verify ALL
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_freestanding.py"
        --build-dir "${CMAKE_BINARY_DIR}"
    DEPENDS wmb_p9a wmb_p9a_whole wmb_p9a_app wmb_app_wrapper VERBATIM)
