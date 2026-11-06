cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(wmb_sdl_library C)
endif()

set(WMB_SDL_PIXEL_BITS "16" CACHE STRING "SDL pixel depth: 16 (RGB565) or 32 (XRGB8888)")
set_property(CACHE WMB_SDL_PIXEL_BITS PROPERTY STRINGS 16 32)
if(NOT WMB_SDL_PIXEL_BITS MATCHES "^(16|32)$")
    message(FATAL_ERROR "WMB_SDL_PIXEL_BITS must be 16 or 32")
endif()

if(NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static AND NOT TARGET PkgConfig::SDL2)
    find_package(SDL2 2.0.5 QUIET CONFIG)
    if(NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2::SDL2-static)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET "sdl2>=2.0.5")
    endif()
endif()

if(TARGET SDL2::SDL2)
    set(WMB_SDL2_TARGET SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    set(WMB_SDL2_TARGET SDL2::SDL2-static)
else()
    set(WMB_SDL2_TARGET PkgConfig::SDL2)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/sdl_library.cmake)
add_library(wmb_sdl_library STATIC ${SOURCE_FILES_SDL_LIBRARY})
target_compile_features(wmb_sdl_library PUBLIC c_std_11)
target_include_directories(wmb_sdl_library PUBLIC
    ${SDL_LIBRARY_INCLUDE_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/../../framework
    ${CMAKE_CURRENT_LIST_DIR}/../../hal
)
target_compile_definitions(wmb_sdl_library PUBLIC
    SDL_MAIN_HANDLED
    WMB_SDL_PIXEL_BITS=${WMB_SDL_PIXEL_BITS}
)
target_link_libraries(wmb_sdl_library PUBLIC ${WMB_SDL2_TARGET})
