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

- Services throw fine-grained `AppException` subtypes (`request::*`, `authentication::*`, `validation::*`, `database::*`, ...); `AppErrorHandler` maps them to `Status + text/plain`. Success via `createDtoResponse` JSON. No numeric envelope, no exception text with
  secrets, no status codes outside the documented set.
- Log with levels (`TRACE..CRITICAL`), tags per subsystem, no passwords /
  JWTs / keys / raw tokens. Oatpp internal logs flow through
  `OatppLoggerBridge`.

## Security

- Bound SQL parameters only; the SQLCipher key goes through `sqlite3_key()`
  in the connection provider (never into SQL text) and is validated with a
  real schema read at open.
- JWT: HS256, issuer + expiry enforced, 30s leeway. Constant-shape login
  failures. Minimum secret length 8 (tests), production must use a strong
  random secret.
