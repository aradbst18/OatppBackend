# Testing

Framework: dependency-free harness (`tests/support/TestRunner.hpp`) + CTest.
No gtest, builds offline. `TEST_CASE("suite: name")`, `EXPECT_TRUE/FALSE/EQ`,
`EXPECT_THROW_AS`. One binary `app_tests` runs all suites.

## Suites

| File                   | Covers                                                      |
| ---------------------- | ----------------------------------------------------------- |
| `test_config.cpp`      | defaults, validation, JSON/TOML/XML/YAML round-trips, corrupt and missing files, save refusal, active-format match, auto-create, missing-param completion, legacy-key rejection, exe-side critical log, version read/complete/validate |
| `test_validation.cpp`  | `NumericBounds` for all int widths, username/password rules, required fields, `UserRole` parse/reject |
| `test_serialization.cpp` | valid parse, `Int8=64654646` rejection, boundary overflow, negative `UInt8`, malformed JSON, missing/null fields |
| `test_auth.cpp`        | JWT create/verify, wrong secret, expired, malformed, bearer extraction, login success/401/422, whoami |
| `test_database.cpp`    | backend name, CRUD, injection stored literally, bad SQL (message names the failing table), `?`/`:` translation rules + parameter-count checks, repository CRUD (create/get/update/delete, not-found, conflict, invalid input), empty-key plaintext mode, wrong-key open failure, password-hash protocol |
| `test_generic_document.cpp` | `DocumentValue` core, JSON/TOML/XML/YAML parse+serialize round-trips, file-database inspector, PostgreSQL inspector (needs server) |
| `test_api.cpp`         | live HTTP: login 200/token, bad credentials 401, Int8 overflow 400, who-am-i 401/200, interceptor classification, error-code mapping |
| `test_websocket.cpp`   | real WS handshake (101) + masked `hello` -> `Server echo: hello` |
| `test_lifecycle.cpp`   | `ManagedThread` start/join/idempotent stop, `Application` init idempotency, terminate handler, defaults fallback |
| `test_features.cpp`    | exactly-one config/users-db define, file-support/user-db consistency, gzip round-trip (or absence, see `docs/zlib.md`), swagger doc build, C++17 check |

Integration tests bind 127.0.0.1 on ports 18081–18090 (HTTP) and
18281–18299 (WS), waiting ~3s for readiness. File backends use `:memory:`
databases with `tests::createTestDatabase()` (correct key per backend);
the wrong-key case writes a scratch file under `/tmp/opencode` instead
(it must survive across opens to prove the failure is the key);
`APP_USERS_DATABASE_TYPE=POSTGRESQL` builds provision a fresh
`oatpp_test_<pid>_<counter>` database per test instead (PostgreSQL has no
`:memory:`), dropping the previous one on the next call.

## PostgreSQL tests

Builds with `APP_ENABLE_POSTGRESQL=ON` link libpq; builds with
`APP_USERS_DATABASE_TYPE=POSTGRESQL` additionally require a reachable
server for every test run (the users table lives there). File-users
builds only need the server for the `postgres inspector` cases in
`test_generic_document.cpp`. Connection parameters come from the
environment with local-throwaway defaults:

```bash
# One-time local setup (no sudo needed for a scratch instance, scram-sha-256
# from the start so no trust window; PGPASSWORD is the database.key):
echo -n "oatpp_test_pw" > /tmp/opencode/.pwfile && chmod 600 /tmp/opencode/.pwfile
initdb -D /tmp/opencode/pgdata -U postgres --auth=scram-sha-256 --pwfile=/tmp/opencode/.pwfile
rm /tmp/opencode/.pwfile
pg_ctl -D /tmp/opencode/pgdata -o "-k /tmp/opencode -p 5433 -h 127.0.0.1" -l /tmp/opencode/pg.log start
PGPASSWORD=oatpp_test_pw psql -h /tmp/opencode -p 5433 -U postgres -d postgres \
  -c "CREATE ROLE oatpp_tester LOGIN PASSWORD 'oatpp_test_pw' CREATEDB;"
pg_ctl -D /tmp/opencode/pgdata stop

# Test runs (defaults match the setup above; PGHOST is database.path, PGPASSWORD is database.key):
PGHOST=/tmp/opencode PGPORT=5433 PGUSER=oatpp_tester PGPASSWORD=oatpp_test_pw \
  cmake -S . -B build-pg -DAPP_ENABLE_POSTGRESQL=ON -DAPP_USERS_DATABASE_TYPE=POSTGRESQL
cmake --build build-pg --parallel
PGHOST=/tmp/opencode PGPORT=5433 PGUSER=oatpp_tester PGPASSWORD=oatpp_test_pw \
  ctest --test-dir build-pg --output-on-failure
```

`PGDATABASE` selects the maintenance database for test provisioning
(default `postgres`). `scripts/test_matrix.sh` runs its PostgreSQL
combos only when `pg_isready` answers at `$PGHOST:$PGPORT` (`MATRIX_PG=1`
forces, `MATRIX_PG=0` skips).

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
# or directly:
./build/tests/app_tests
```

Sanitizer runs use the matching presets (see `docs/cmake.md`): `ctest
--preset debug-asan-ubsan` exercises the suite under ASan/UBSan, `ctest
--preset debug-tsan` under TSan (races fail via TSan's nonzero exit status).
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
