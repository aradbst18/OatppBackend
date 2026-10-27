add_executable(
    app_tests
    test_main.cpp
    test_config.cpp
    test_validation.cpp
    test_serialization.cpp
    test_auth.cpp
    test_database.cpp
    test_api.cpp
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
    "websocket"
    "lifecycle"
    "features"
    "generic"
    "logging"
    "version")

foreach(SUITE ${TEST_SUITES})
    add_test(NAME "test_${SUITE}" COMMAND app_tests "${SUITE}:")
endforeach()

add_test(NAME app_tests_all COMMAND app_tests)

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
endif()
