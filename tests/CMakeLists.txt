add_executable(
    app_tests
    test_main.cpp
    test_config.cpp
    test_validation.cpp
    test_serialization.cpp
    test_auth.cpp
    test_database.cpp
    test_pg_test_db.cpp
    test_api.cpp
    test_rate_limit.cpp
    test_websocket.cpp
    test_lifecycle.cpp
    test_features.cpp
    test_generic_document.cpp
    test_logging.cpp
    test_version.cpp)

target_include_directories(app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(app_tests PRIVATE app_core)

apply_app_definitions(app_tests)
apply_app_warnings(app_tests)
apply_app_build_type_flags(app_tests)
apply_app_hardening(app_tests)
app_apply_sanitizers(app_tests)

set(TEST_SUITES
    "config"
    "validation"
    "serialization"
    "domain"
    "auth"
    "database"
    "api"
    "rate_limit"
    "websocket"
    "lifecycle"
    "features"
    "generic"
    "logging"
    "version")

if(APP_USERS_DATABASE_TYPE STREQUAL "POSTGRESQL")
    list(APPEND TEST_SUITES "pg_test_db")
endif()

foreach(SUITE ${TEST_SUITES})
    add_test(NAME "test_${SUITE}" COMMAND app_tests "${SUITE}:")
endforeach()

add_test(NAME app_tests_all COMMAND app_tests)
# The combined suite shares listening ports and temporary paths with individual suites.
set_tests_properties(app_tests_all PROPERTIES RUN_SERIAL TRUE)

# A separate process guarantees the environment has not been initialized by another test.
add_test(NAME test_environment_first_use COMMAND app_tests "environment:")

add_executable(test_runner_tests test_test_runner.cpp)
apply_app_warnings(test_runner_tests)
apply_app_build_type_flags(test_runner_tests)
apply_app_hardening(test_runner_tests)
app_apply_sanitizers(test_runner_tests)
add_test(NAME test_test_runner COMMAND test_runner_tests)

if(APP_ENABLE_POSTGRESQL)
    add_test(NAME test_postgresql_inspector_unavailable
             COMMAND ${CMAKE_COMMAND} -DAPP_TESTS=$<TARGET_FILE:app_tests>
                     -DAPP_USERS_DATABASE_TYPE=${APP_USERS_DATABASE_TYPE} -P
                     ${CMAKE_CURRENT_SOURCE_DIR}/test_postgresql_inspector_unavailable.cmake)
endif()

# Keep syscall fault injection isolated from application/database tests.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(private_text_file_tests test_main.cpp test_private_text_file.cpp)
    target_link_libraries(private_text_file_tests PRIVATE app_core)
    target_link_options(private_text_file_tests PRIVATE "-Wl,--wrap=fsync" "-Wl,--wrap=lstat" "-Wl,--wrap=unlink")
    apply_app_warnings(private_text_file_tests)
    apply_app_build_type_flags(private_text_file_tests)
    apply_app_hardening(private_text_file_tests)
    app_apply_sanitizers(private_text_file_tests)
    add_test(NAME test_private_text_file COMMAND private_text_file_tests)

    if(APP_ENABLE_POSTGRESQL)
        add_executable(postgresql_local_server_tests test_main.cpp test_postgresql_local_server.cpp)
        target_link_libraries(postgresql_local_server_tests PRIVATE app_core)
        target_link_options(
            postgresql_local_server_tests
            PRIVATE
            "-Wl,--wrap=system"
            "-Wl,--wrap=mkstemp"
            "-Wl,--wrap=write"
            "-Wl,--wrap=fchmod"
            "-Wl,--wrap=close"
            "-Wl,--wrap=unlink"
            "-Wl,--wrap=lstat")
        apply_app_warnings(postgresql_local_server_tests)
        apply_app_build_type_flags(postgresql_local_server_tests)
        apply_app_hardening(postgresql_local_server_tests)
        app_apply_sanitizers(postgresql_local_server_tests)
        add_test(NAME test_postgresql_local_server COMMAND postgresql_local_server_tests)
    endif()
endif()

get_property(
    REGISTERED_TESTS
    DIRECTORY
    PROPERTY TESTS)
set_tests_properties(${REGISTERED_TESTS} PROPERTIES SKIP_RETURN_CODE 77)
