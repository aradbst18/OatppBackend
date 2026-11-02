# Coding Standards

Enforced by `cmake/compiler.cmake` (`-Wall -Wextra -Wpedantic -Wconversion
-Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast`): the build
must be warning-free. Third-party headers are `SYSTEM` includes; warnings
fire only for our code.

## Formatting and naming

- 4-space indent, 100-column soft limit, `snake_case` files, `CamelCase`
  types, `lowerCamel` methods, `snake_case` variables.
- Namespaces: `app::<layer>` (`logging`, `errors`, `config`, `database`,
  `repositories`, `auth`, `services`, `validation`, `serialization`,
  `domain`, `dto`, `controllers`, `middlewares`, `websocket`,
  `infrastructure`, `application`).
- Compile defines: `APP_*`. CMake options: `APP_*`.

## Headers and includes

- `#pragma once`. Include order: own header, C/C++ stdlib, third-party
  (oatpp, jwt-cpp, jsoncpp, toml11, pugixml, openssl, sqlcipher), then `app/`.
- Forward-declare oatpp server types in headers; include full oatpp headers
  only in `.cpp` files where possible.
- No `using namespace` in headers.
- Every `#if`, `#ifdef`, and `#ifndef` block closes with an `#endif` comment
  identifying its full condition or selection; preserve negation and name
  all alternatives in multi-branch selections (see `docs/code-style.md`).

## Classes and ownership

- Explicit ownership: constructor injection with `shared_ptr`, null-checked.
- `std::thread` only inside `ManagedThread`. No `detach`, no raw owning
  `new`, no singletons except `Logger::instance()`.
- `const` correctness, move semantics for settings/handlers, deleted copies
  for RAII owners. `noexcept` for predicates and simple getters.

## Comments and docs (English only)

- Explain *why*, invariants, and security considerations. No
  `// create object` noise. Doxygen (`///`, `/** */`) for reusable public
  APIs. Every `pragma`/suppression has a `NOTE:` with justification.

## Error handling and logging

- Services throw fine-grained `AppException` subtypes (`request::*`, `authentication::*`, `validation::*`, `database::*`, ...); `AppErrorHandler` preserves their HTTP status and selects fixed public `text/plain` messages. All 5xx bodies are generic. Success uses `createDtoResponse` JSON; errors have no numeric envelope.
- Exception `what()` text is diagnostic, never a public message. The HTTP handler redacts free-form diagnostics in full and logs the status, error category, and matching `X-Error-Id`. Never put secrets in exceptions, since other internal consumers may log them. Framework errors use the same response policy; only `WWW-Authenticate`, `Allow`, and `Retry-After` are forwarded from framework error headers.
- Log with ordered levels (`TRACE..CRITICAL`, plus `OFF`), tags per subsystem, no passwords /
  JWTs / keys / raw tokens. Oatpp internal logs flow through `OatppLoggerBridge`.
  - `CRITICAL`: Fatal process-terminating errors (startup failure, port bound, schema corrupt).
  - `ERROR`: Operations that failed completely (5xx server errors, thread unhandled crashes).
  - `WARN`: Actionable anomalies (rate limits exceeded, failed logins, unencrypted database fallback).
  - `INFO`: Operational milestones (server running, migrations, user login success, user created).
  - `DEBUG`: High-level diagnostics and HTTP access logs (`method`, `path`, `status`, `duration_ms`).
  - `TRACE`: Step-by-step diagnostic execution flow and branch decisions (validation, DB locks, token minting).
  - `OFF`: Completely silences all logging output for benchmarking or silent test runs.
- Key application lifecycle milestones (version info, initialization checkpoints, routing address, Swagger UI URL, and server running) use `app::logging::logMilestone()` to guarantee console visibility for operators even when general logging is set to `OFF` or `WARN`, while strictly keeping file output suppressed when logging is disabled.
- HTTP requests are correlated with a unique `X-Request-Id` header (forwarded or 32-char hex generated)
  and bound to thread-local `app::logging::RequestContext` so all subsystem logs automatically carry `[req=<id>]`.

## Security

- Bound SQL parameters only; the SQLCipher key goes through `sqlite3_key()`
  in the connection provider (never into SQL text) and is validated with a
  real schema read at open.
- JWT: HS256, issuer + expiry enforced, 30s leeway. Constant-shape login
  failures. Minimum secret length 8 (tests), production must use a strong
  random secret.
