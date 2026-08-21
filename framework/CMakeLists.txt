cmake_minimum_required(VERSION 3.16)

set(MODULE_NAME         wmb_tima_build)

set(SDL_ROOT            "/home/wifi/projects/SDL")
set(SDL_INCLUDE_DIR     "${SDL_ROOT}/include")
set(SDL_LIB_DIR         "${SDL_ROOT}/build/build/.libs")

if(NOT EXISTS "${SDL_INCLUDE_DIR}/SDL.h")
    message(FATAL_ERROR "SDL header not found at ${SDL_INCLUDE_DIR}/SDL.h")
endif()

if(NOT EXISTS "${SDL_LIB_DIR}")
    message(FATAL_ERROR "SDL library directory not found at ${SDL_LIB_DIR}")
endif()

# Sources include <SDL.h>; the include directory is supplied here.
# (docs/30-host-build.md section 5 - the sdl2_shim is removed.)

add_definitions(-D_USE_SCREEN_WIDTH=800)
add_definitions(-D_USE_SCREEN_HEIGHT=480)
add_definitions(-DTIMA_USE_CONFIG)
add_definitions(-DPIXEL_TYPE_T=uint16_t)
# UNIX selects the POSIX branch of linux_hosted/system_linux/hal_time_hosted.c
add_definitions(-DUNIX)

# tinyx/kdrive/tima sources include <kdrive-config.h> only when HAVE_CONFIG_H is
# set. In this standalone framework build, that generated header is not present.
# Keep HAVE_CONFIG_H undefined for the initial compile path.
# add_definitions(-DHAVE_CONFIG_H)

set(SOURCE_FILES
                        ../transport/x11/wmb_main.c
                        ../linux_hosted/sdl_library/sdl_input.c
                        ../linux_hosted/sdl_library/sdl_main.c
                        ../linux_hosted/sdl_library/sdl_mouse.c
                        ../linux_hosted/sdl_library/sdl_window.c

                        ../linux_hosted/sdl_drivers/hal_display_sdl.c
                        ../linux_hosted/sdl_drivers/hal_input_sdl.c
                        ../linux_hosted/sdl_drivers/lcd_driver.c
                        ../linux_hosted/sdl_drivers/mouse_driver.c
                        ../linux_hosted/sdl_drivers/keyraw_driver.c

                        ./library/heap_mngr.c
                        ./library/buffer.c

                        ../linux_hosted/system_linux/hal_time_hosted.c

                        ../tinyx/kdrive/tima/tima_kdrive_fb.c
                        ../tinyx/kdrive/tima/tima_kdrive_kbd.c
                        ../tinyx/kdrive/tima/tima_kdrive_mouse.c
                        ../tinyx/kdrive/tima/tima_kdrive_system.c

                        )

include_directories(BEFORE
                        ./
                        ../hal
                        ../linux_hosted/sdl_drivers
                        ../linux_hosted/sdl_library
                        ./library
                        ../linux_hosted/system_linux
                        ../tinyx/kdrive/tima

                        ${SDL_INCLUDE_DIR}
                        # X11 headers - parent dir so #include <X11/X.h> works
                        ../X11


                        # TinyX KDrive includes (based on configure.ac KDRIVE_INCS)
                        ../tinyx/kdrive/src         # KDRIVE_INC
                        ../tinyx/kdrive/linux       # KDRIVE_OS_INC

                        # TinyX core includes (CORE_INCS)
                        ../tinyx/include

                        # TinyX module includes
                        ../tinyx/mi                 # MI_INC
                        ../tinyx/fb                 # FB_INC
                        ../tinyx/Xext               # XEXT_INC
                        ../tinyx/miext/shadow       # MIEXT_SHADOW_INC
                        ../tinyx/miext/damage       # MIEXT_DAMAGE_INC
                        ../tinyx/render             # RENDER_INC
                        ../tinyx/randr              # RANDR_INC
                        ../tinyx/xfixes             # FIXES_INC
                        ../tinyx/damageext          # DAMAGE_INC
                        ../tinyx/os                 # OS headers
                        ../tinyx/dix                # DIX headers

                        )

link_directories(       ${SDL_LIB_DIR}
                        #${RTL_MMU_OBJ_DIR}
                        #${RTL_IOU_OBJ_DIR}
                        #${RTL_OBJ_DIR}
                        #${RTL_TB_OBJ_DIR}
                                            )

project(${MODULE_NAME})
add_executable(${MODULE_NAME} ${SOURCE_FILES})
target_link_libraries(${MODULE_NAME} SDL2 m dl)
