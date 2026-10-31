# Testing

Framework: dependency-free harness (`tests/support/TestRunner.hpp`) + CTest.
No gtest, builds offline. `TEST_CASE("suite: name")`, `EXPECT_TRUE/FALSE/EQ`,
`EXPECT_THROW_AS`. One binary `app_tests` runs all application suites.
Use `app::tests::skip("reason")` only for an explicitly unavailable optional
prerequisite. The runner reports `[PASS]`, `[FAIL]`, and `[SKIP]` separately,
and its total includes skipped cases. Any failure returns exit code 1; a run
with passed cases and no failures returns 0. A run where every matched case
skips returns 77, which CTest marks as skipped. An unmatched filter still fails.
The standalone `test_runner_tests` binary verifies these reporting rules without
application or database dependencies.
Linux GNU/Clang builds also run `private_text_file_tests`, which wraps `fsync`
to verify publication and new-parent syncing, interrupted-call retries,
and failure propagation without affecting the database tests. A non-root permission
test covers existing and fresh configs beneath search-only ancestors (skipped when
run as root). Process tests pause a publisher after `mkdir` and exercise both a
concurrent helper and recovery after killing the creator. These are syscall and
process fault-injection tests; they do not simulate a physical power loss.
Marker tests also inject foreign ownership and sticky-directory unlink failures,
including replacement during cleanup. The `multi-user sticky directories` case
uses a root-owned mode-1777 parent, an application UID and a different marker-owner
UID; it proves the kernel returns `EPERM` for deletion, then verifies successful
unprivileged startup. This case requires root to create the UID-owned fixtures
(no accounts are created), so ordinary non-root runs explicitly skip it. Run it
separately with `sudo -- build/cpp001/tests/private_text_file_tests 'multi-user sticky directories'`.

## Suites

| File                   | Covers                                                      |
| ---------------------- | ----------------------------------------------------------- |
| `test_config.cpp`      | defaults, validation, JSON/TOML/XML/YAML round-trips, corrupt and missing files, save refusal, active-format match, auto-create, missing-param completion, legacy-key rejection, exe-side critical log, version read/complete/validate |
| `test_validation.cpp`  | `NumericBounds` for all int widths, username/password rules, required fields, `UserRole` parse/reject |
| `test_serialization.cpp` | valid parse, `Int8=64654646` rejection, boundary overflow, negative `UInt8`, malformed JSON, missing/null fields |
| `test_auth.cpp`        | JWT create/verify, wrong secret, expired, malformed, bearer extraction, login success/401/422, whoami, explicit bootstrap credentials, published-password rejection, existing-account preservation |
| `test_database.cpp`    | backend name, CRUD, injection stored literally, bad SQL (message names the failing table), `?`/`:` translation rules + parameter-count checks, repository CRUD (create/get/update/delete, not-found, conflict, invalid input), empty-key plaintext mode, wrong-key open failure, password-hash protocol |
| `test_pg_test_db.cpp`  | PostgreSQL provisioning preserves unrelated databases, independent run ownership, failed creation, cleanup retry, original connection settings |
| `test_generic_document.cpp` | `DocumentValue` core, JSON/TOML/XML/YAML parse+serialize round-trips, file-database inspector, PostgreSQL inspector (needs server) |
| `test_api.cpp`         | live HTTP: login 200/token, bad credentials 401, Int8 overflow 400, who-am-i 401/200, interceptor classification; error-code mapping, direct/nested exception privacy, framework errors, safe headers, event/log correlation, failing log sink |
| `test_websocket.cpp`   | real WS handshake (101), text echo length and bytes for `hello` and embedded, leading, trailing, and NUL-only payloads |
| `test_lifecycle.cpp`   | `ManagedThread` start/join/idempotent stop, natural-completion restart, startup-copy failure recovery, concurrent lifecycle calls, worker self-calls, `Application` init idempotency, bootstrap startup failures, custom administrator protection, terminate handler, defaults fallback |
| `test_features.cpp`    | exactly-one config/users-db define, file-support/user-db consistency, gzip round-trip (or absence, see `docs/zlib.md`), swagger doc build, C++17 check |

Application fixtures set dedicated bootstrap credentials through scoped
environment overrides and restore the previous environment afterward. Service
fixtures pass credentials explicitly; tests never rely on production defaults.

Integration tests bind 127.0.0.1 on ports 18081–18090 (HTTP) and
18281–18299 (WS), waiting ~3s for readiness. File backends use `:memory:`
databases with `tests::createTestDatabase()` (correct key per backend);
the wrong-key case writes a scratch file under `/tmp/opencode` instead
(it must survive across opens to prove the failure is the key);
`APP_USERS_DATABASE_TYPE=POSTGRESQL` builds provision a fresh
`oatpp_test_<random-run-id>_<counter>` database per call instead (PostgreSQL
has no `:memory:`). Successful creations are registered with their original
connection settings. After all test fixtures finish, `test_main.cpp` drops
only the exact registered names. Cleanup failures identify the database,
retain it for retry, and make the test process fail.

## PostgreSQL tests

Builds with `APP_ENABLE_POSTGRESQL=ON` link libpq; builds with
`APP_USERS_DATABASE_TYPE=POSTGRESQL` additionally require a reachable
server for every test run (the users table lives there).

The checked-in presets retain PostgreSQL support alongside SQLCipher user
storage. In these file-users builds, the PostgreSQL inspector reports `[SKIP]`
when `PGHOST` is unset or empty, so default CTest runs need no PostgreSQL server.
Set `PGHOST` to a host or socket directory to request inspector coverage:
connection, authentication, and database-creation errors then fail the test.
PostgreSQL-users builds always require the server, even without `PGHOST`.
Provisioning exceptions never count as skips.

CI builds the presets as written, sets `PGHOST`, and starts a disposable server
for PostgreSQL coverage. Its plain SQLCipher feature job explicitly disables
PostgreSQL support to also cover builds without it.

Connection parameters come from `PGHOST`, `PGPORT`, `PGDATABASE`, `PGUSER`, and
`PGPASSWORD`, with local-throwaway defaults. Full-application PostgreSQL tests
require a private `PGPASSWORD`; the old published `oatpp_test_pw` is rejected
by application configuration validation:

```bash
# Scratch cluster with a private password; keep PGPASSWORD in this shell for test runs.
read -r -s -p 'Private test PostgreSQL password: ' PGPASSWORD
export PGPASSWORD
printf '\n'
mkdir -p /tmp/opencode
(umask 077; printf '%s' "$PGPASSWORD" > /tmp/opencode/.pwfile)
initdb -D /tmp/opencode/pgdata -U postgres --auth=scram-sha-256 --pwfile=/tmp/opencode/.pwfile
rm /tmp/opencode/.pwfile
pg_ctl -D /tmp/opencode/pgdata -o "-k /tmp/opencode -p 5433 -h 127.0.0.1" -l /tmp/opencode/pg.log start
# Enter the same private password for the new role when prompted.
createuser -h /tmp/opencode -p 5433 -U postgres --createdb --login --pwprompt oatpp_tester

cmake -S . -B build-pg -DAPP_ENABLE_POSTGRESQL=ON -DAPP_USERS_DATABASE_TYPE=POSTGRESQL
cmake --build build-pg --parallel
PGHOST=/tmp/opencode PGPORT=5433 PGUSER=oatpp_tester \
  ctest --test-dir build-pg --output-on-failure
pg_ctl -D /tmp/opencode/pgdata stop
```

`PGDATABASE` selects the maintenance database for test provisioning
(default `postgres`). `scripts/test_matrix.sh` runs its additional PostgreSQL
combos only when
`pg_isready` answers at `$PGHOST:$PGPORT` (`MATRIX_PG=1` forces,
`MATRIX_PG=0` skips). Explicitly selecting `POSTGRESQL` in `MATRIX_BACKENDS`
also requires a server for those main cases. The additional PostgreSQL cases
set `PGHOST` to its local default if necessary, so forced inspector coverage
cannot silently skip.

Provisioning never scans for or deletes databases by prefix. Databases left
by a crash are deliberately preserved; use a disposable cluster per run or
remove known leftovers explicitly. Sharing a PostgreSQL role or having no
active connections does not establish which run owns a database.

PostgreSQL-users builds register `test_pg_test_db`; run it against a
disposable cluster with the same `PG*` environment:

```bash
ctest --test-dir build-pg -R '^test_pg_test_db$' --output-on-failure
```

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
