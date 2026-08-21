set(FIBER_LINUX_SOURCE_PATH ${CMAKE_CURRENT_LIST_DIR})

list(APPEND SOURCE_FILES_FIBER_LINUX
    ${FIBER_LINUX_SOURCE_PATH}/fiber_port.c
    ${FIBER_LINUX_SOURCE_PATH}/intr_handler.c
)

set(FIBER_LINUX_INCLUDE_PATH ${FIBER_LINUX_SOURCE_PATH})

# x86_64/aarch64 SysV ABI keeps the stack 16-byte aligned; hosted fibers
# call into libc (printf & friends) so they need real stacks.
set(FIBER_LINUX_DEFINES
    FIBER_PORT_STACK_ALIGN=16u
    FIBER_DEFAULT_STACK=65536u
)

# libucontext - see README.md for installation instructions.
find_path(LIBUCONTEXT_INCLUDE_DIR NAMES libucontext/libucontext.h)
find_library(LIBUCONTEXT_LIBRARY NAMES ucontext)

if(LIBUCONTEXT_INCLUDE_DIR AND LIBUCONTEXT_LIBRARY)
    message(STATUS "fiber_linux: using libucontext (${LIBUCONTEXT_LIBRARY})")
    list(APPEND FIBER_LINUX_INCLUDE_PATH ${LIBUCONTEXT_INCLUDE_DIR})
    list(APPEND FIBER_LINUX_DEFINES FIBER_USE_LIBUCONTEXT=1)
    set(FIBER_LINUX_LIBS ${LIBUCONTEXT_LIBRARY})
else()
    message(WARNING
        "fiber_linux: libucontext not found, falling back to the glibc "
        "<ucontext.h> implementation. See linux_hosted/fiber_linux/README.md")
    set(FIBER_LINUX_LIBS "")
endif()
