execute_process(COMMAND "${HOST}" --wire-test "${CAPTURE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Subject wire capture failed: ${result}\n${errors}${output}")
endif()
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${CAPTURE}" --compare "${GOLDEN}"
    RESULT_VARIABLE comparison OUTPUT_VARIABLE report ERROR_VARIABLE failure)
if(NOT comparison STREQUAL "0")
    message(FATAL_ERROR "Stock-Xlib differential check failed:\n${report}${failure}")
endif()
message("${report}")
