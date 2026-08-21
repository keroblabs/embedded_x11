cmake_minimum_required(VERSION 3.16)

project(sdl_fiber_example C)

set(FRAMEWORK_ROOT      ${CMAKE_CURRENT_LIST_DIR}/../../framework)
set(LINUX_HOSTED_ROOT   ${CMAKE_CURRENT_LIST_DIR}/..)
set(HAL_ROOT            ${CMAKE_CURRENT_LIST_DIR}/../../hal)

include(${HAL_ROOT}/hal.cmake)
include(${FRAMEWORK_ROOT}/fiber/fiber.cmake)
include(${FRAMEWORK_ROOT}/library/library.cmake)
include(${LINUX_HOSTED_ROOT}/fiber_linux/fiber_linux.cmake)
include(${LINUX_HOSTED_ROOT}/system_linux/system_linux.cmake)
include(${LINUX_HOSTED_ROOT}/sdl_library/sdl_library.cmake)
include(${LINUX_HOSTED_ROOT}/sdl_drivers/sdl_drivers.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(sdl_fiber_example
    ${CMAKE_CURRENT_LIST_DIR}/sdl_fiber_example.c
    ${SOURCE_FILES_LIBRARY}
    ${SOURCE_FILES_FIBER}
    ${SOURCE_FILES_FIBER_LINUX}
    ${SOURCE_FILES_SYSTEM_LINUX}
    ${SOURCE_FILES_SDL_LIBRARY}
    ${SOURCE_FILES_SDL_DRIVERS}
)

target_include_directories(sdl_fiber_example PRIVATE
    ${HAL_INCLUDE_PATH}
    ${FIBER_LINUX_INCLUDE_PATH}
    ${SYSTEM_LINUX_INCLUDE_PATH}
    ${FIBER_INCLUDE_PATH}
    ${LIBRARY_INCLUDE_PATH}
    ${FRAMEWORK_ROOT}
    ${SDL_DRIVERS_INCLUDE_PATH}
    ${SDL_LIBRARY_INCLUDE_PATH}
)

# No _USE_LINUX here: docs/90-decisions.md D-18 deletes that macro.
target_compile_definitions(sdl_fiber_example PRIVATE
    ${SYSTEM_LINUX_DEFINES}
    ${FIBER_DEFINES}
    ${FIBER_LINUX_DEFINES}
    _USE_SCREEN_WIDTH=800
    _USE_SCREEN_HEIGHT=480
    PIXEL_TYPE_T=uint16_t
)

target_compile_options(sdl_fiber_example PRIVATE -Wall -Wextra)
target_link_libraries(sdl_fiber_example PRIVATE PkgConfig::SDL2 ${FIBER_LINUX_LIBS} m)
