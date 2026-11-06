cmake_minimum_required(VERSION 3.16)
project(hal_external_consumer C CXX)

get_filename_component(HAL_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../hal ABSOLUTE)
set(HAL_CONSUMER_BACKEND "sdl" CACHE STRING "Consumer test backend: sdl or reference")
if(HAL_CONSUMER_BACKEND STREQUAL "sdl")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. wmb-backend)
elseif(HAL_CONSUMER_BACKEND STREQUAL "reference")
    add_library(wmb_display_input STATIC reference_backend.c)
    target_include_directories(wmb_display_input PUBLIC ${HAL_INCLUDE_DIR})
    target_compile_features(wmb_display_input PUBLIC c_std_11)
    target_compile_definitions(wmb_display_input PRIVATE REFERENCE_PIXEL_BITS=${WMB_SDL_PIXEL_BITS})
else()
    message(FATAL_ERROR "Unknown HAL_CONSUMER_BACKEND: ${HAL_CONSUMER_BACKEND}")
endif()

add_library(hal_public_headers OBJECT public_headers.c)
target_link_libraries(hal_public_headers PRIVATE wmb_display_input)
target_include_directories(hal_public_headers BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR}/freestanding)
target_compile_options(hal_public_headers PRIVATE -ffreestanding -nostdinc)

add_executable(hal_external_consumer main.cpp $<TARGET_OBJECTS:hal_public_headers>)
target_compile_features(hal_external_consumer PRIVATE cxx_std_11)
target_link_libraries(hal_external_consumer PRIVATE wmb_display_input)
set_target_properties(hal_external_consumer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/consumer_interface.txt CONTENT
    "$<TARGET_PROPERTY:hal_external_consumer,INCLUDE_DIRECTORIES>\n$<TARGET_PROPERTY:hal_external_consumer,COMPILE_DEFINITIONS>\n")
