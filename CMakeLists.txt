cmake_minimum_required(VERSION 3.16)

project(wmb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# All executables land in build/bin/ regardless of source subtree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------
# Load the .cmake fragments the modules ship.  Each fragment sets
# SOURCE_FILES_* and *_INCLUDE_PATH (and *_DEFINES / *_LIBS as needed).
# ---------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/hal/hal.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/framework/fiber/fiber.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/framework/library/library.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/linux_hosted/fiber_linux/fiber_linux.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/linux_hosted/system_linux/system_linux.cmake)

# ---------------------------------------------------------------------
# wmb_hal: interface library for the HAL contract headers.
# Semantic dependency for anything that talks to a platform backend.
# ---------------------------------------------------------------------
add_library(wmb_hal INTERFACE)
target_include_directories(wmb_hal INTERFACE ${HAL_INCLUDE_PATH})

# ---------------------------------------------------------------------
# wmb_framework: fiber runtime + library primitives.  No platform, no
# X11.  L8 and L9 in docs/44-framework-module-plan.md hold structurally
# for this target - see hal/, transport/ and linux_hosted/ for the
# other layers.
#
# FIBER_LINUX_DEFINES is applied PUBLIC because it carries
# FIBER_DEFAULT_STACK / FIBER_PORT_STACK_ALIGN, which fiber.c and
# fiber_port.c must both see with the same values.
# ---------------------------------------------------------------------
add_library(wmb_framework STATIC
    ${SOURCE_FILES_FIBER}
    ${SOURCE_FILES_LIBRARY}
)
target_include_directories(wmb_framework PUBLIC
    ${FIBER_INCLUDE_PATH}
    ${LIBRARY_INCLUDE_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/framework          # types.h, config_tima.h
)
target_link_libraries(wmb_framework PUBLIC wmb_hal)
target_compile_definitions(wmb_framework PUBLIC
    ${FIBER_DEFINES}
    ${FIBER_LINUX_DEFINES}
)

# ---------------------------------------------------------------------
# wmb_fiber_linux: hosted fiber port (mmap + guard page + ucontext).
# ---------------------------------------------------------------------
add_library(wmb_fiber_linux STATIC ${SOURCE_FILES_FIBER_LINUX})
target_include_directories(wmb_fiber_linux PUBLIC ${FIBER_LINUX_INCLUDE_PATH})
target_link_libraries(wmb_fiber_linux PUBLIC wmb_framework ${FIBER_LINUX_LIBS})

# ---------------------------------------------------------------------
# wmb_system_linux: hosted hal_time + malloc-backed wmb_alloc.
# ---------------------------------------------------------------------
add_library(wmb_system_linux STATIC ${SOURCE_FILES_SYSTEM_LINUX})
target_include_directories(wmb_system_linux PUBLIC ${SYSTEM_LINUX_INCLUDE_PATH})
target_link_libraries(wmb_system_linux PUBLIC wmb_framework)
target_compile_definitions(wmb_system_linux PUBLIC ${SYSTEM_LINUX_DEFINES})

# wmb_framework calls back into the port (fiber_port_irq_lock,
# intr_handler_call_crash) and into the system layer (wmb_alloc via
# heap_mngr.c).  Expressing the cycle here lets CMake wrap the static
# archives in --start-group / --end-group at link time so the linker
# resolves both directions in one pass.
target_link_libraries(wmb_framework PUBLIC wmb_fiber_linux wmb_system_linux)

# ---------------------------------------------------------------------
# wmb_hosted: meta-target that pulls in everything a non-graphical
# hosted app needs.  App CMakeLists just link this one.
# ---------------------------------------------------------------------
add_library(wmb_hosted INTERFACE)
target_link_libraries(wmb_hosted INTERFACE
    wmb_framework
    wmb_fiber_linux
    wmb_system_linux
)

# ---------------------------------------------------------------------
# Optional: SDL2-backed hal_display / hal_input, and the SDL library
# wrappers.  When SDL2 is absent the SDL apps are skipped.
# ---------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
endif()

set(WMB_HAVE_SDL FALSE)
if(SDL2_FOUND)
    include(${CMAKE_CURRENT_LIST_DIR}/linux_hosted/sdl_library/sdl_library.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/linux_hosted/sdl_drivers/sdl_drivers.cmake)

    add_library(wmb_sdl_library STATIC ${SOURCE_FILES_SDL_LIBRARY})
    target_include_directories(wmb_sdl_library PUBLIC ${SDL_LIBRARY_INCLUDE_PATH})
    target_link_libraries(wmb_sdl_library PUBLIC wmb_framework PkgConfig::SDL2)

    add_library(wmb_sdl_drivers STATIC ${SOURCE_FILES_SDL_DRIVERS})
    target_include_directories(wmb_sdl_drivers PUBLIC ${SDL_DRIVERS_INCLUDE_PATH})
    target_link_libraries(wmb_sdl_drivers PUBLIC wmb_framework wmb_sdl_library)
    # PIXEL_TYPE_T appears in the SDL driver headers (lcd_driver.h et al.)
    # so it must be visible everywhere they are; PUBLIC propagates it to
    # every consumer.  Screen extents are grouped in for consistency.
    target_compile_definitions(wmb_sdl_drivers PUBLIC
        PIXEL_TYPE_T=uint16_t
        _USE_SCREEN_WIDTH=800
        _USE_SCREEN_HEIGHT=480
    )

    add_library(wmb_hosted_sdl INTERFACE)
    target_link_libraries(wmb_hosted_sdl INTERFACE
        wmb_hosted
        wmb_sdl_drivers
        wmb_sdl_library
    )

    set(WMB_HAVE_SDL TRUE)
    message(STATUS "wmb: SDL2 ${SDL2_VERSION} - SDL demos enabled")
else()
    message(STATUS "wmb: SDL2 not found - SDL demos will be skipped")
endif()

# ---------------------------------------------------------------------
# Apps
# ---------------------------------------------------------------------
add_subdirectory(linux_hosted/apps/wmb_fiber_smoke)
add_subdirectory(linux_hosted/apps/wmb_task_smoke)
add_subdirectory(linux_hosted/fiber_example)
if(WMB_HAVE_SDL)
    add_subdirectory(linux_hosted/sdl_fiber_example)
    add_subdirectory(linux_hosted/sdl_fiber_example_v2)
endif()
