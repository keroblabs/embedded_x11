cmake_minimum_required(VERSION 3.16)

project(fiber_example C)

set(FRAMEWORK_ROOT      ${CMAKE_CURRENT_LIST_DIR}/../../framework)
set(LINUX_HOSTED_ROOT   ${CMAKE_CURRENT_LIST_DIR}/..)
set(HAL_ROOT            ${CMAKE_CURRENT_LIST_DIR}/../../hal)

include(${HAL_ROOT}/hal.cmake)
include(${FRAMEWORK_ROOT}/fiber/fiber.cmake)
include(${FRAMEWORK_ROOT}/library/library.cmake)
include(${LINUX_HOSTED_ROOT}/fiber_linux/fiber_linux.cmake)
include(${LINUX_HOSTED_ROOT}/system_linux/system_linux.cmake)

add_executable(fiber_example
    ${CMAKE_CURRENT_LIST_DIR}/fiber_example.c
    ${SOURCE_FILES_LIBRARY}
    ${SOURCE_FILES_FIBER}
    ${SOURCE_FILES_FIBER_LINUX}
    ${SOURCE_FILES_SYSTEM_LINUX}
)

target_include_directories(fiber_example PRIVATE
    ${HAL_INCLUDE_PATH}
    ${FIBER_LINUX_INCLUDE_PATH}
    ${SYSTEM_LINUX_INCLUDE_PATH}
    ${FIBER_INCLUDE_PATH}
    ${LIBRARY_INCLUDE_PATH}
    ${FRAMEWORK_ROOT}
)

target_compile_definitions(fiber_example PRIVATE ${SYSTEM_LINUX_DEFINES} ${FIBER_DEFINES} ${FIBER_LINUX_DEFINES})
target_compile_options(fiber_example PRIVATE -Wall -Wextra)
target_link_libraries(fiber_example PRIVATE ${FIBER_LINUX_LIBS} m)
