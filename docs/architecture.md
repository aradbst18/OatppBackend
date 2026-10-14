# Architecture

Modern C++17 backend foundation on Oat++ 1.3.0. Layered, explicit ownership,
compile-time feature selection. No singletons except the process logger; no
global component registry (oatpp `OATPP_COMPONENT` is intentionally not used —
dependencies are constructor-injected).

## Layers and dependency direction

```text
main.cpp
  └── application::Application        lifecycle owner (explicit phases)
        ├── config       AppSettings + IConfigStore (JSON/TOML/XML/YAML, over infrastructure documents)
        ├── infrastructure TextFile + documents/ (DocumentValue + Json/Toml/Xml/Yaml/DatabaseDocument, general-purpose) + database/ (IDatabase <- SqliteDatabase <- SqlcipherDatabase) + Gzip/Swagger (optional)
        ├── logging      Logger + OatppLoggerBridge
        ├── errors       AppException hierarchy + AppErrorHandler
        ├── repositories UserRepository (SQL via bound parameters only)
        ├── auth         JwtService (jwt-cpp HS256) + PasswordHash (SHA-2 via OpenSSL EVP) — the only crypto owners
        ├── services     AuthService (credential check + token issuance)
        ├── validation   NumericBounds, Validators (user/pass rules)
        ├── serialization RangeValidatingDeserializer + AppObjectMapper
        ├── domain       UserRole (enum class + string mapping)
        ├── dto          oatpp DTOs (wire shapes only, no logic)
        ├── middlewares  AuthInterceptor (Bearer verify + bundle claims)
        ├── controllers  AuthController, EchoController (DTO in/out only)
        ├── websocket    EchoListener / EchoInstanceListener
```

Rules:

- Controllers never touch SQL, crypto, or the filesystem. They validate DTO
  presence, call exactly one service method, and map the result to a DTO.
- Services own use-cases. Only `JwtService` touches jwt-cpp; only
  repositories touch `IDatabase`.
- Repositories own SQL. Every value is a `?` bound parameter. String
  interpolation of values into SQL is forbidden (see `test_database.cpp`
  injection test).
- DTOs are wire shapes. Validation lives in `validation/` + service checks,
  range enforcement lives in the object mapper.
- `domain/` types (`UserRole`) are plain C++ (`enum class`). The DTO layer
  carries strings and converts through `toString`/`tryParseUserRole`.

## Request lifecycle

```text
TCP -> HttpConnectionHandler -> AuthInterceptor -> Router -> Controller
  -> Service -> Repository -> IDatabase
  <- DTO <- DTO <- record(s)
  <- JSON (AppObjectMapper) <- error JSON (AppErrorHandler on throw)
```

- `AuthInterceptor` short-circuits public paths (`POST /auth/login`,
  `GET /ws/echo`, `/swagger/*`, `/api-docs/*`) and verifies Bearer tokens
  elsewhere, stashing `subject|role|issuer` in the request bundle.
- `AppErrorHandler` converts every uncaught framework error (400/404/...) and
  every `AppException` into `Status + text/plain` via `ResponseFactory::createResponse`. Services throw domain exceptions; controllers let them bubble (or throw `request::*` directly for early validation).

## Response lifecycle

Success: service result -> DTO -> `createDtoResponse(200, dto)` serialized by
the range-checked `AppObjectMapper`. Errors: `Status + text/plain` via `createResponse`/`AppErrorHandler` (status,
appCode, name, message)` — always JSON, never plaintext, never leaking token
or key material.

## Application lifecycle

```text
configure (loadSettingsOrDefaults)
  -> initInfrastructure (log level, oatpp Environment + bridge, once per process)
  -> initDatabase (factory -> open -> ensureSchema)
  -> initServices (JwtService, AuthService + seed)
  -> initRouting (router, controllers, swagger?, zlib encoders?, interceptors)
  -> run (blocking oatpp Server)
  -> stop (server->stop, provider->stop, db->close; idempotent)
```

`main()` only parses `--config/--help`, installs the terminate handler, and
calls `run()`. Tests drive `initialize()` without blocking to get routers and
services without sockets.

## Threading

- HTTP serving is oatpp's internal thread pool (owned by `Server`).
- Application-owned threads use `ManagedThread` (RAII `std::thread`,
  `start/requestStop/stop/join`, never detached). The worker polls
  `stopRequested()`. Joining from the worker itself is a safe no-op.
- `Logger` serializes all writes with a mutex. `IDatabase` implementations
  guard the sqlite handle with a mutex. `EchoInstanceListener` counts sockets
  with an atomic.

## Key decisions

1. **Range-checked deserializers** (not DTO types alone): oatpp parses JSON
   integers as 64-bit then narrows, so `64654646` would wrap into an `Int8`.
   `createAppObjectMapper()` installs MIN/MAX guards per type; overflow fails
   with "Value out of range" (HTTP 400). Covered by `test_serialization.cpp`.
2. **Explicit DI over oatpp components**: controllers take handles in
   constructors. Result: constructible in unit tests, no hidden globals.
3. **Compile-time features**: `APP_ENABLE_ZLIB/SWAGGER`, `APP_DATABASE_BACKEND`,
   `APP_CONFIG_FORMAT` produce `APP_WITH_*` / `APP_DATABASE_*` / `APP_CONFIG_*`
   defines. Disabled code is not compiled (`target_sources` conditionals) and
   disabled libs are not linked or even `find_package`d. SQLCipher headers
   `#error` when included in SQLite builds.
4. **oatpp-sqlite is used for transport types only where valuable**; the
   repository layer uses the raw sqlite3 API through `IDatabase` so SQLCipher
   key handling (`PRAGMA key`) stays explicit and testable. `SQLITE_HAS_CODEC`
   is defined in SQLCipher builds and `PRAGMA cipher_version` is verified at
   open.
5. **Single `app_core` static library** + `oatpp-backend` executable +
   `app_tests` CTest binary (no gtest, builds offline).
6. **Gzip and Swagger live in `infrastructure/` by rule, not convenience.**
   Both are delivery-side technical adapters, not business logic: gzip is an
   HTTP content-encoding transport detail implemented as thin glue over the
   oatpp-zlib framework (providers registered in `Application::initRouting`;
   the `gzipCompress/gzipDecompress` helpers operate on arbitrary bytes and
   are covered by backend-agnostic tests), and Swagger UI is interactive API
   documentation tooling over the HTTP API (OpenAPI description + doc/test
   harness in Cockburn's adapter sense), implemented as oatpp-swagger glue
   (`DocumentInfo` builder, endpoint mounting, res-path serving). Neither
   belongs in `controllers/` (controllers declare only `ENDPOINT_INFO`
   metadata and must never touch transport or doc-serving frameworks) nor in
   `serialization/` (oatpp DTO wire-mapping with range validation — a
   different concern from byte transport and doc delivery). Both follow the
   optional-capability pattern: flag-gated, `#error` on wrong inclusion,
   conditionally compiled and never linked when disabled.
