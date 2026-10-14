# Testing

Framework: dependency-free harness (`tests/support/TestRunner.hpp`) + CTest.
No gtest, builds offline. `TEST_CASE("suite: name")`, `EXPECT_TRUE/FALSE/EQ`,
`EXPECT_THROW_AS`. One binary `app_tests` runs all suites.

## Suites

| File                   | Covers                                                      |
| ---------------------- | ----------------------------------------------------------- |
| `test_config.cpp`      | defaults, validation, JSON/TOML/XML/YAML round-trips, corrupt and missing files, save refusal, active-format match, auto-create |
| `test_validation.cpp`  | `NumericBounds` for all int widths, username/password rules, required fields, `UserRole` parse/reject |
| `test_serialization.cpp` | valid parse, `Int8=64654646` rejection, boundary overflow, negative `UInt8`, malformed JSON, missing/null fields |
| `test_auth.cpp`        | JWT create/verify, wrong secret, expired, malformed, bearer extraction, login success/401/422, whoami |
| `test_database.cpp`    | backend name, CRUD, injection stored literally, bad SQL, repository CRUD, SQLCipher empty-key rejection |
| `test_api.cpp`         | live HTTP: login 200/token, bad credentials 401, Int8 overflow 400, who-am-i 401/200, interceptor classification, error-code mapping |
| `test_websocket.cpp`   | real WS handshake (101) + masked `hello` -> `Server echo: hello` |
| `test_lifecycle.cpp`   | `ManagedThread` start/join/idempotent stop, `Application` init idempotency, terminate handler, defaults fallback |
| `test_features.cpp`    | exactly-one config/backend define, gzip round-trip (or absence), swagger doc build, C++17 check |

Integration tests bind 127.0.0.1 on ports 18081–18090 (HTTP) and
18281–18299 (WS), waiting ~3s for readiness. `:memory:` databases with
`tests::createTestDatabase()` (correct key per backend).

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# or directly:
./build/tests/app_tests
```

Sanitizer runs use the matching presets (see `docs/cmake.md`): `ctest
--preset debug` exercises the suite under ASan/UBSan, `ctest --preset
debug-tsan` under TSan (races fail via TSan's nonzero exit status).
`release` registers no tests (`APP_BUILD_TESTS=OFF`), so there is no `release`
test preset — test `debug`/`relwithdebinfo` instead.

## Adding tests for a new feature

1. Unit-test service/validation/DTO behavior in the matching suite.
2. For endpoints: extend `test_api.cpp` (or the relevant suite) with success
   + each documented error status over real HTTP.
3. For persistence: extend `test_database.cpp` with schema + CRUD + an
   injection case.
4. Tests must assert behavior (status codes, bodies, DB rows), never just
   instantiate classes.
