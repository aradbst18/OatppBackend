# OatppBackend

Modern C++17 backend foundation: Oat++ REST + WebSocket echo, JWT auth,
validated DTOs, SQLite/SQLCipher, JSON/TOML/XML config, optional
Swagger and gzip — with a clean layered architecture and 59+ tests.

## Architecture overview

```text
controllers -> services -> repositories -> IDatabase (SQLite/SQLCipher)
     |             |             |
   DTOs      JwtService    bound-params SQL only
     |             |
AuthInterceptor  validation + range-checked ObjectMapper
```

Full detail: [`docs/architecture.md`](docs/architecture.md). New endpoints:
[`docs/adding-api.md`](docs/adding-api.md). Rules for automation:
[`docs/codex-guide.md`](docs/codex-guide.md).

## Prerequisites

- CMake >= 3.20, GCC/Clang with C++17, OpenSSL, ZLIB
- Oat++ 1.3.0 modules: `oatpp`, `oatpp-sqlite`, `oatpp-websocket`,
  `oatpp-swagger` (if `APP_ENABLE_SWAGGER=ON`), `oatpp-zlib` (if
  `APP_ENABLE_ZLIB=ON`)
- `jwt-cpp` 0.7.2, `jsoncpp` 1.9.6, `pugixml` 1.15, `toml11` 4.4.0
- SQLite3 (or SQLCipher dev for `SQLCIPHER` builds)

All are discovered with `find_package` (see [`docs/cmake.md`](docs/cmake.md)).

## Build instructions

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Options (`-D`): `APP_ENABLE_ZLIB` (ON), `APP_ENABLE_SWAGGER` (ON),
`APP_ENABLE_JSON/TOML/XML/YAML_PARSER` (ON), `APP_BUILD_TESTS` (ON),
`ENABLE_SANITIZERS` (ON; the build type selects the set),
`VERBOSE_BUILD_SUMMARY` (OFF),
`APP_DATABASE_BACKEND` (`SQLITE`/`SQLCIPHER`), `APP_PASSWORD_HASH_ALGO` (`SHA256`/`SHA384`/`SHA512`),
`APP_CONFIG_FORMAT` (`JSON`/`TOML`/`XML`/`YAML`),
`APP_VERSION` (`0.0.1`), `APP_RELEASE_DATE` (empty = build date), `APP_VERSION_FILE_FORMAT` (`JSON`).
Invalid values (including a format whose parser is off) fail at configure
time. `CMakePresets.json` holds `base` (all of the above) plus one preset per
build type — `debug`, `debug-tsan`, `debug-msan`, `relwithdebinfo`,
`relwithdebinfo-tsan`, `release`, `minsizerel` — override anything per
configure with `-D`
(e.g. `cmake --preset debug -DAPP_CONFIG_FORMAT=YAML`); plus a Ninja
`-ninja` overlay in `CMakeUserPresets.json`. The full backend x format x
feature matrix is driven by `scripts/test_matrix.sh` (plain `cmake -D...`
invocations, no checked-in matrix); see [`docs/cmake.md`](docs/cmake.md).

## Configuration options

One format active at compile time (`APP_CONFIG_JSON/_TOML/_XML/_YAML` define).
Same `AppSettings` fields in every format; see [`docs/configuration.md`](docs/configuration.md)
and `configs/app.{json,toml,xml,yaml}` samples. The runtime file is always
`configs/app.conf`, created from defaults when missing.

## SQLite vs SQLCipher

- `SQLITE` (default): plaintext `SqliteDatabase`, no key, no sqlcipher symbols
  linked or compiled.
- `SQLCIPHER`: `SqlcipherDatabase` applies `PRAGMA key` at open (never logged)
  and verifies `PRAGMA cipher_version`; empty key is a hard error.
  `SqlcipherDatabase.hpp` cannot even be included in SQLite builds.

## Swagger

When enabled: `GET /api-docs/oas-3.0.0.json`, `GET /swagger/ui`. Every endpoint
documents real request/response DTOs and producible statuses; auth endpoints
declare the `bearer_auth` scheme. When disabled the routes are gone (404).
See [`docs/swagger.md`](docs/swagger.md).

## Zlib

When enabled the HTTP handler negotiates gzip/deflate via `oatpp-zlib`
providers, plus a tested `gzipCompress/gzipDecompress` helper. When disabled
nothing is compiled or linked.

## Running the server

```bash
./build/oatpp-backend --config configs/app.conf
# --help for usage; missing files are created from defaults on startup
```

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

- [Architecture](docs/architecture.md) · [Adding an API](docs/adding-api.md) ·
  [Controller](docs/adding-controller.md) · [DTO](docs/adding-dto.md) ·
  [Service](docs/adding-service.md) · [Repository](docs/adding-repository.md)
- [Authentication](docs/authentication.md) · [Configuration](docs/configuration.md) ·
  [CMake](docs/cmake.md) · [WebSocket](docs/websocket.md) · [Swagger](docs/swagger.md)
- [Testing](docs/testing.md) · [Coding standards](docs/coding-standards.md) ·
  [Codex guide](docs/codex-guide.md) · [Examples](docs/examples.md)
