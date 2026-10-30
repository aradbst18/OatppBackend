# OatppBackend

Modern C++17 backend foundation: Oat++ REST APIs (JWT Authentication, User CRUD & batch management with role & admin protection), WebSocket echo, self-validating DTOs, dual SQLite/SQLCipher & PostgreSQL storage, JSON/TOML/XML/YAML config, Swagger OpenAPI 3.0, and gzip — with a clean layered architecture and 120+ tests.

## Architecture overview

```text
controllers -> services -> repositories -> IDatabase (SQLCipher/libpq)
     |             |             |
   DTOs      JwtService    bound-params ANSI SQL only
     |             |
AuthInterceptor  dto->validate() + range-checked ObjectMapper
```

Full detail: [`docs/architecture.md`](docs/architecture.md). New endpoints:
[`docs/guides/adding-api.md`](docs/guides/adding-api.md). Rules for AI agents:
[`AGENTS.md`](AGENTS.md).


## Prerequisites

- CMake >= 3.20, GCC/Clang with C++17, OpenSSL, ZLIB
- Oat++ 1.3.0 modules: `oatpp`, `oatpp-sqlite` (if
  `APP_ENABLE_SQLCIPHER=ON`), `oatpp-postgresql` (if `APP_ENABLE_POSTGRESQL=ON`),
  `oatpp-swagger` (if `APP_ENABLE_SWAGGER=ON`), `oatpp-zlib` (if
  `APP_ENABLE_ZLIB=ON`)
- `oatpp-websocket` 1.3.0 is built offline from the bundled archive with a
  continuation-frame correction; no system installation is needed.
- `jwt-cpp` 0.7.2, `jsoncpp` 1.9.8, `pugixml` 1.16, `toml11` 4.4.0,
  `yaml-cpp` 0.9.0
- SQLCipher dev (the file database library; opens plaintext files too)

Other dependencies are discovered at configure time — `find_package`, plus
`pkg_check_modules` for SQLCipher (see [`docs/cmake.md`](docs/cmake.md)).
CI builds the pinned CMake dependencies from the bundled archives into a
job-local prefix and installs system libraries through apt.

## Build instructions

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Options (`-D`): `APP_ENABLE_ZLIB` (OFF), `APP_ENABLE_SWAGGER` (OFF),
`APP_BUILD_TESTS` (ON), `ENABLE_SANITIZERS` (OFF; the build type selects the set),
`VERBOSE_BUILD_SUMMARY` (OFF),
`APP_ENABLE_POSTGRESQL` (OFF), `APP_ENABLE_SQLCIPHER` (ON),
`APP_USERS_DATABASE_TYPE` (`SQLCIPHER`/`POSTGRESQL`),
`APP_ENCRYPTION_USER_DATABASE` (OFF),
`APP_ENABLE_JSON_PARSER` (ON), `APP_ENABLE_TOML/XML/YAML_PARSER` (OFF),
`APP_PASSWORD_HASH_ALGO` (`SHA256`/`SHA384`/`SHA512`),
`APP_CONFIG_FORMAT` (`JSON`/`TOML`/`XML`/`YAML`),
`APP_VERSION` (`0.0.1`), `APP_RELEASE_DATE` (empty = build date), `APP_VERSION_FILE_FORMAT` (`JSON`).
Invalid values (including a format whose parser is off) fail at configure
time. `CMakePresets.json` holds `base` (all of the above) plus one preset per
configuration — sanitizer-free `debug`/`relwithdebinfo`,
instrumented `debug-asan-ubsan`/`debug-tsan`/`debug-msan` (Clang) and
`relwithdebinfo-asan-ubsan`/`relwithdebinfo-tsan`/`relwithdebinfo-msan`
(Clang), plus `release`/`minsizerel` — override anything per
configure with `-D`
(e.g. `cmake --preset debug -DAPP_CONFIG_FORMAT=YAML`); plus a Ninja
`-ninja` overlay in `CMakeUserPresets.json`. The full backend x format x
feature matrix is driven by `scripts/test_matrix.sh` (plain `cmake -D...`
invocations, no checked-in matrix); see [`docs/cmake.md`](docs/cmake.md).

## Configuration options

One format active at compile time (`APP_CONFIG_JSON/_TOML/_XML/_YAML` define).
Same `AppSettings` fields in every format; see [`docs/configuration.md`](docs/configuration.md)
and `configs/app.{json,toml,xml,yaml}` samples. The runtime file is always
`configs/app.conf`, created with unique JWT and database secrets when missing.
The generated file is owner-only (`0600`). Checked-in samples contain rejected
`REPLACE_ME` placeholders; existing configurations must provide private secrets.

## SQLCipher (file database) vs PostgreSQL

- `SQLCIPHER` (default): `SqlcipherDatabase` over the SQLCipher library, executed
  by the `oatpp-sqlite` adapter (standard SQLite is never linked; the
  connection provider applies the key via SQLCipher's `sqlite3_key()`).
  Empty key means plaintext; with `APP_ENCRYPTION_USER_DATABASE=ON` the
  `database.key` is applied to every connection (never logged) and validated
  with a schema read, so a wrong key fails `open()`. A missing key is
  rejected. An explicitly empty key (`key = ""`) opens the file plaintext
  and logs a startup warning. `SqlcipherDatabase.hpp` cannot even be included without
  `APP_WITH_SQLCIPHER`.
- `POSTGRESQL`: `PostgresqlDatabase` over the `oatpp-postgresql` adapter (libpq
  underneath, see [`docs/postgresql.md`](docs/postgresql.md) and `docs/architecture.md` §5); connection from the
  `database.path`/`port`/`name`/`user`/`key` settings (scram-sha-256).
  Requires `APP_ENABLE_POSTGRESQL=ON`
  (configure-time error otherwise). Whether the SQLCipher file library is
  compiled alongside is governed separately by `APP_ENABLE_SQLCIPHER` (`OFF`
  links no file code at all); see `docs/cmake.md`. PostgreSQL encryption
  (beyond scram auth) is not implemented — see
  [`docs/roadmap/postgresql_encryption.md`](docs/roadmap/postgresql_encryption.md); requesting it with `POSTGRESQL` is a
  configure-time error.


## Swagger

When enabled: `GET /api-docs/oas-3.0.0.json`, `GET /swagger/ui`. Every endpoint
documents real request/response DTOs and producible statuses; auth endpoints
declare the `bearer_auth` scheme. When disabled the routes are gone (404).
See [`docs/swagger.md`](docs/swagger.md).

## Zlib

When enabled the HTTP handler negotiates gzip/deflate via `oatpp-zlib`
providers, plus a tested `gzipCompress/gzipDecompress` helper. When disabled
nothing is compiled or linked. See [`docs/zlib.md`](docs/zlib.md).

## Running the server

```bash
# First startup: supply a private administrator password (input is hidden).
read -r -s -p 'Initial administrator password: ' APP_SEED_ADMIN_PASSWORD
export APP_SEED_ADMIN_PASSWORD
printf '\n'
./build/oatpp-backend --config configs/app.conf
# --help for usage; missing files are created from defaults on startup
```

The bootstrap username defaults to `admin`; set `APP_SEED_ADMIN_USERNAME` to
choose another name and keep that setting on subsequent starts. There is no
default password. Once the account exists, the bootstrap password may be
removed from the environment. See [administrator provisioning](docs/authentication.md#administrator-provisioning)
for validation and migration from the old sample password.

VS Code server debug configurations prompt for this password. Enter it on the
first start; leave the prompt blank on later starts. The prompt overrides any
`APP_SEED_ADMIN_PASSWORD` inherited by VS Code.

## API examples

Login (public) and authenticated who-am-i, unauthorized shape, Int8 overflow
rejection, WebSocket echo, and config/CMake snippets:
[`docs/examples.md`](docs/examples.md). Authentication detail:
[`docs/authentication.md`](docs/authentication.md).

## WebSocket example

`GET /ws/echo` upgrades to WebSocket; every text message returns
`Server echo: <message>`. See [`docs/websocket.md`](docs/websocket.md).

## Testing

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Coverage (config, serialization ranges incl. `Int8=64654646`, auth, live HTTP
API, WebSocket echo, database incl. injection, lifecycle, features):
[`docs/testing.md`](docs/testing.md).

## Documentation links

- [Architecture](docs/architecture.md) · [Guides](docs/guides/README.md)
- [CMake](docs/cmake.md) · [Configuration](docs/configuration.md)
- [Authentication](docs/authentication.md) · [SQLCipher](docs/sqlcipher.md) · [PostgreSQL](docs/postgresql.md) · [WebSocket](docs/websocket.md) · [Swagger](docs/swagger.md) · [Zlib](docs/zlib.md)
- [Testing](docs/testing.md) · [Examples](docs/examples.md)
- [Coding standards](docs/coding-standards.md)
- [Agent Guidelines](AGENTS.md)
- [Roadmap](docs/roadmap/README.md)
