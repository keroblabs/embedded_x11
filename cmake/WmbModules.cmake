file(GLOB TINYX_DIX_SOURCES CONFIGURE_DEPENDS "${TINYX_SOURCE_ROOT}/dix/*.c")
file(GLOB TINYX_MI_SOURCES CONFIGURE_DEPENDS "${TINYX_SOURCE_ROOT}/mi/*.c")
file(GLOB TINYX_FB_SOURCES CONFIGURE_DEPENDS "${TINYX_SOURCE_ROOT}/fb/*.c")
# fbcmap.c is selected by KDrive's libkdrivestubs in its upstream Makefile.am.
file(GLOB TINYX_EXT_SOURCES CONFIGURE_DEPENDS
    "${TINYX_SOURCE_ROOT}/render/*.c" "${TINYX_SOURCE_ROOT}/randr/*.c"
    "${TINYX_SOURCE_ROOT}/xfixes/*.c" "${TINYX_SOURCE_ROOT}/damageext/*.c")
foreach(name bigreq shape sync xcmisc xtest sleepuntil saver dpms xres)
    list(APPEND TINYX_EXT_SOURCES "${TINYX_SOURCE_ROOT}/Xext/${name}.c")
endforeach()
set(TINYX_MIEXT_SOURCES "${TINYX_SOURCE_ROOT}/miext/damage/damage.c")
foreach(name shadow shalloc shpacked shplanar8 shplanar
    shrot16pack_180 shrot16pack_270 shrot16pack_270YX shrot16pack_90
    shrot16pack_90YX shrot16pack shrot32pack_180 shrot32pack_270
    shrot32pack_90 shrot32pack shrot8pack_180 shrot8pack_270 shrot8pack_90
    shrot8pack shrotate)
    list(APPEND TINYX_MIEXT_SOURCES "${TINYX_SOURCE_ROOT}/miext/shadow/${name}.c")
endforeach()
set(TINYX_OS_SOURCES)
foreach(name log oscolor strlcat strlcpy osinit utils
    connection_wmb io_wmb waitfor_wmb timer_wmb excluded_stubs_wmb)
    list(APPEND TINYX_OS_SOURCES "${TINYX_SOURCE_ROOT}/os/${name}.c")
endforeach()
set(TINYX_SERVER_SOURCES ${TINYX_DIX_SOURCES} ${TINYX_MI_SOURCES}
    ${TINYX_FB_SOURCES} ${TINYX_EXT_SOURCES} ${TINYX_MIEXT_SOURCES}
    ${TINYX_OS_SOURCES} "${TINYX_SOURCE_ROOT}/wmb/server.c")
foreach(source IN LISTS TINYX_SERVER_SOURCES)
    if(source MATCHES "/kdrive/(linux|fbdev|vesa)/|/kdrive/src/kmap\\.c$|/os/(access|auth|mitauth|xdmauth|xauembed|xdmcp|xstrans|connection|io|WaitFor)\\.c$|/dbe/|/Xext/(shm|xf86bigfont)\\.c$")
        message(FATAL_ERROR "Excluded native TinyX source selected: ${source}")
    endif()
endforeach()
