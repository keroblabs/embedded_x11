execute_process(COMMAND "${HOST}" --timeout-test
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
message("${errors}")
if(NOT result STREQUAL "77" OR NOT errors MATCHES "WMB Xlib: reply timeout" OR
   NOT errors MATCHES "P5 timeout: detected after")
    message(FATAL_ERROR "Missing-reply failure was not bounded/diagnosed: ${result}")
endif()
message("PASS P5 bounded reply timeout")
