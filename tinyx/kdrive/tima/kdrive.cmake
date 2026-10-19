# Portable WMB-owned KDrive DDX; upstream tinyx/kdrive/src is unchanged.
# These source lists will also be reused by the P4 TinyX server target.
set(WMB_KDRIVE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/tima_kdrive_fb.c
    ${CMAKE_CURRENT_LIST_DIR}/tima_kdrive_kbd.c
    ${CMAKE_CURRENT_LIST_DIR}/tima_kdrive_mouse.c
    ${CMAKE_CURRENT_LIST_DIR}/tima_kdrive_system.c
)
set(WMB_KDRIVE_INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tinyx/include
    ${PROJECT_SOURCE_DIR}/tinyx/dix
    ${PROJECT_SOURCE_DIR}/tinyx/os
    ${PROJECT_SOURCE_DIR}/tinyx/mi
    ${PROJECT_SOURCE_DIR}/tinyx/fb
    ${PROJECT_SOURCE_DIR}/tinyx/Xext
    ${PROJECT_SOURCE_DIR}/tinyx/miext/shadow
    ${PROJECT_SOURCE_DIR}/tinyx/miext/damage
    ${PROJECT_SOURCE_DIR}/tinyx/render
    ${PROJECT_SOURCE_DIR}/tinyx/randr
    ${PROJECT_SOURCE_DIR}/tinyx/xfixes
    ${PROJECT_SOURCE_DIR}/tinyx/damageext
    ${PROJECT_SOURCE_DIR}/tinyx/kdrive/src
    ${PROJECT_SOURCE_DIR}/tinyx/kdrive/tima
)
