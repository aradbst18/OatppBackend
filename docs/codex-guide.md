# Codex Development Guide

Intended for future Codex tasks adding APIs to this project. Follow it
literally; ambiguity resolves toward the stricter reading.

## Where files belong

| Artifact   | Header                    | Source                    |
| ---------- | ------------------------- | ------------------------- |
| DTO        | `include/app/dto/`        | header-only (codegen)     |
| Controller | `include/app/controllers/`| `src/controllers/`        |
| Service    | `include/app/services/`   | `src/services/`           |
| Repository | `include/app/repositories/`| `src/repositories/`      |
| Middleware | `include/app/middlewares/`| `src/middlewares/`        |
| Validation | `include/app/validation/` | `src/validation/`         |
| Domain     | `include/app/domain/`     | `src/domain/`             |
| Config     | `include/app/config/`     | `src/config/`             |

Do not create new top-level layers without updating `docs/architecture.md`.

## Controller rules

- Inherit `ApiController`, constructor-inject services, no `OATPP_COMPONENT`.
- `ENDPOINT` declared in header, defined in `.cpp`. `ENDPOINT_INFO` inline.
- Authenticated endpoints: both `info->authorization = "bearer_auth";` and
  `addSecurityRequirement("bearer_auth")`.
- Handler: null-check body -> service call -> `createDtoResponse`; catch
  `AppException` -> `Status + text/plain` via `AppErrorHandler`; catch-all -> 500 generic.

## DTO rules

- `DTO_INIT`/`DTO_FIELD`, wire-only. Narrow ints only for real range
  contracts (+ serialization test). Enum-ish values as canonical strings.
- Document every producible status with the real DTO in `ENDPOINT_INFO`.

## Service rules

- Plain C++ in/out, one method per use-case, throws `AppException` only.
- No oatpp includes in service headers. No SQL in services.

## Repository rules

- `BaseRepository` subclass, idempotent `ensureSchema`, `?` params only,
  `optional<T>` lookups. Tests via `tests::createTestDatabase()`.

## Middleware / auth rules

- Public paths go in `AuthInterceptor::publicPaths_` + classification test.
- Never log tokens/secrets. Verify via `JwtService`, stash
  `subject|role|issuer` under `kClaimsBundleKey`.

## Validation rules

- Input shape -> `ValidationError` (422). Ranges are automatic (mapper).
- Unknown enum strings rejected. No silent defaults.

## Swagger rules

- Only producible statuses, real DTOs, auth scheme documented. Regenerate
  check: new path present in `/api-docs/oas-3.0.0.json`.

## Test requirements

- New endpoint: live-HTTP success + each error status + overflow case for
  narrow fields. New persistence: CRUD + injection test. Run full
  `ctest --output-on-failure`; all suites must pass.

## Configuration / CMake rules

- New setting in all four stores + `validate()` + example configs + tests.
- New dependency: `find_package` in `cmake/dependencies.cmake` inside the
  feature `if()`, link in `link_app_dependencies()`, define in
  `apply_app_definitions()`. No global commands.

## Dependency rules

- Allowed: oatpp*, jwt-cpp, jsoncpp, toml11, pugixml, OpenSSL, sqlite/sqlcipher,
  Threads, zlib. New libraries need justification in the PR/issue.

## Prohibited shortcuts

- No SQL string interpolation. No crypto outside `JwtService`. No plaintext
  error bodies. No `OATPP_COMPONENT` globals. No detached threads. No
  `TODO` for core behavior. No weakening validation to make tests pass.
- No C++20 (`std::jthread`, concepts, ranges without approval): C++17 only.

## Correct API skeleton

Follow `docs/adding-api.md` steps 1–12 and its conformance checklist. Reference
implementation: `AuthController` + `AuthService` + `UserRepository`.
