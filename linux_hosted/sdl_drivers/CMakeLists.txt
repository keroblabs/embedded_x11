cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(wmb_sdl_backend C)
endif()

set(WMB_SDL_WIDTH "800" CACHE STRING "Fixed SDL LCD width in pixels")
set(WMB_SDL_HEIGHT "480" CACHE STRING "Fixed SDL LCD height in pixels")
foreach(extent WMB_SDL_WIDTH WMB_SDL_HEIGHT)
    if(NOT "${${extent}}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${extent} must be an integer from 1 to 65535")
    endif()
    if(${extent} GREATER 65535)
        message(FATAL_ERROR "${extent} must be an integer from 1 to 65535")
    endif()
endforeach()

if(NOT TARGET wmb_sdl_library)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../sdl_library ${CMAKE_CURRENT_BINARY_DIR}/sdl_library)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/sdl_drivers.cmake)
if(NOT TARGET wmb_hal)
    get_filename_component(WMB_HAL_HEADER_PATH ${CMAKE_CURRENT_LIST_DIR}/../../hal ABSOLUTE)
    add_library(wmb_hal INTERFACE)
    target_include_directories(wmb_hal INTERFACE ${WMB_HAL_HEADER_PATH})
endif()

add_library(wmb_sdl_drivers STATIC ${SOURCE_FILES_SDL_DRIVERS})
add_library(wmb_display_input ALIAS wmb_sdl_drivers)
target_compile_features(wmb_sdl_drivers PUBLIC c_std_11)
target_include_directories(wmb_sdl_drivers PRIVATE ${SDL_DRIVERS_PRIVATE_INCLUDE_PATH})
target_compile_definitions(wmb_sdl_drivers PRIVATE
    WMB_SDL_SCREEN_WIDTH=${WMB_SDL_WIDTH}
    WMB_SDL_SCREEN_HEIGHT=${WMB_SDL_HEIGHT}
)
target_link_libraries(wmb_sdl_drivers PUBLIC wmb_hal PRIVATE wmb_sdl_library)

option(WMB_SDL_BUILD_TESTS "Build the SDL backend regression tests" OFF)
if(WMB_SDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
