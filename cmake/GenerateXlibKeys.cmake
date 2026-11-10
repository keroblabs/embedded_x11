execute_process(COMMAND "${GENERATOR}" ${HEADERS}
    OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Xlib keysym table generation failed: ${result}")
endif()
