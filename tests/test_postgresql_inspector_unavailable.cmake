string(
    RANDOM
    LENGTH 16
    ALPHABET 0123456789abcdef RUN_ID)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PGHOST=${CMAKE_CURRENT_BINARY_DIR}/no-cluster-${RUN_ID}" "PGPORT=5432" ${APP_TESTS}
            "generic: postgresql inspector dumps server databases"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERRORS
    TIMEOUT 15)

if(NOT "${STATUS}" STREQUAL "1"
   OR NOT OUTPUT MATCHES "0 passed, 1 failed, 0 skipped, 1 total"
   OR OUTPUT MATCHES "\\[PASS\\]|\\[SKIP\\]")
    message(FATAL_ERROR "Unavailable PostgreSQL inspector must fail, got ${STATUS}:\n${OUTPUT}\n${ERRORS}")
endif()
