# Test Directory Structure

Visual layout of unit, integration, and contract test suites located under `tests/`.

```text
└── 📁tests
    ├── 📁support
    │   ├── ScopedEnvironment.hpp
    │   ├── TestDb.hpp
    │   └── TestRunner.hpp
    ├── test_api.cpp
    ├── test_auth.cpp
    ├── test_ci_matrix.py
    ├── test_config.cpp
    ├── test_database.cpp
    ├── test_features.cpp
    ├── test_generic_document.cpp
    ├── test_lifecycle.cpp
    ├── test_logging.cpp
    ├── test_main.cpp
    ├── test_pg_test_db.cpp
    ├── test_postgresql_inspector_unavailable.cmake
    ├── test_postgresql_local_server.cpp
    ├── test_private_text_file.cpp
    ├── test_rate_limit.cpp
    ├── test_serialization.cpp
    ├── test_test_runner.cpp
    ├── test_validation.cpp
    ├── test_version.cpp
    └── test_websocket.cpp
```

## Test Suites Reference

| File | Primary Coverage |
| :--- | :--- |
| **`test_api.cpp`** | Live HTTP integration tests: authentication, user CRUD, batch operations, pagination, error mapping, safe headers, request correlation IDs, and access logs |
| **`test_auth.cpp`** | JWT token generation and verification, OpenSSL password hashing with salt, whoami identity resolution, and administrator seeding |
| **`test_config.cpp`** | JSON, TOML, XML, and YAML configuration loading, validation rules, secret protection, atomic saving, parameter completion, and case-insensitive log level parsing |
| **`test_database.cpp`** | Dual-backend database abstraction, ORM query mapping (`?` vs `:`), repository operations, schema migrations, and password hash protocols |
| **`test_features.cpp`** | Compile-time feature flags, preprocessor define invariants, and build option mutual exclusivity |
| **`test_generic_document.cpp`** | Generic document tree (`DocumentValue`), multi-format serialization round-trips, and database document inspectors |
| **`test_lifecycle.cpp`** | Process startup/shutdown sequences, initialization idempotency, bootstrap failure recovery, and `ManagedThread` execution |
| **`test_logging.cpp`** | Thread-safe `Logger`, severity levels (`TRACE..CRITICAL`, `OFF`), case-insensitive parsing, correlation context (`RequestContext`), console milestones, and `LoggingInterceptor` |
| **`test_main.cpp`** | Unified test executable entry point registering and executing all test cases via `app_tests` |
| **`test_pg_test_db.cpp`** | Isolated PostgreSQL test database provisioning, independent run tracking, and clean resource destruction |
| **`test_postgresql_local_server.cpp`** | Automated local PostgreSQL server management, `initdb` permissions security audit, and process lifecycle |
| **`test_private_text_file.cpp`** | Atomic file replacement, directory sync recovery markers, and restrictive permissions (`0600`/`0700`) |
| **`test_rate_limit.cpp`** | Sliding-window token-bucket rate limiter, quota enforcement, and trusted reverse proxy IP extraction |
| **`test_serialization.cpp`** | Oat++ DTO serialization, strict integer range bounds validation, and malformed payload handling |
| **`test_test_runner.cpp`** | Verification of the custom test harness assertion macros, skip reporting, and exit code contracts |
| **`test_validation.cpp`** | Input validation rules for usernames, passwords, required fields, and numeric range limits |
| **`test_version.cpp`** | Runtime version file generation, formatting (`x.y.z`, `yyyy-mm-dd`), and auto-completion |
| **`test_websocket.cpp`** | Real-time WebSocket connection upgrades, frame handling, ping/pong keep-alives, and echo payload verification |

## Test Support Harness (`tests/support/`)

| Helper | Purpose |
| :--- | :--- |
| **`TestRunner.hpp`** | Dependency-free, offline test framework providing `TEST_CASE`, `EXPECT_TRUE`, `EXPECT_FALSE`, `EXPECT_EQ`, `EXPECT_THROW_AS`, and skip tracking |
| **`TestDb.hpp`** | Hermetic test database provisioning supporting in-memory SQLCipher (`:memory:`) and dynamic PostgreSQL test databases (`oatpp_test_<pid>_<seq>`) |
| **`ScopedEnvironment.hpp`** | RAII helper for overriding and safely restoring environment variables across test execution |

## Executing Tests

Run all test suites via CTest from the build directory:

```bash
# Run all tests with failure output:
ctest --test-dir build/debug --output-on-failure

# Run a specific test suite (e.g. logging and config):
ctest --test-dir build/debug -R "test_logging|test_config" --output-on-failure
```
