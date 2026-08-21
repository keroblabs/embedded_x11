set(FIBER_SOURCE_PATH       ${CMAKE_CURRENT_LIST_DIR})

list(APPEND SOURCE_FILES_FIBER
    ${FIBER_SOURCE_PATH}/fiber.c
    ${FIBER_SOURCE_PATH}/fiber_sync.c
    ${FIBER_SOURCE_PATH}/hal_task.c
)

set(FIBER_INCLUDE_PATH      ${FIBER_SOURCE_PATH})

# Routes framework/library/buffer.c's locks to fiber_critical_enter/exit.
# Any target linking the fiber core must define this.
set(FIBER_DEFINES           USE_FIBER)
