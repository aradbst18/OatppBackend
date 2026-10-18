# Adding a New API — Step by Step

Follow these steps in order. Each has a conformance checklist at the end.

## 1. Define domain requirements

- Add or reuse a type in `include/app/domain/`. Use `enum class`, never magic
  numbers. Provide `toString`/`tryParse`/`isValid` helpers (see `UserRole`).
- Decide the error cases up front; every failure maps to an existing
  `ErrorCode` (`BadRequest`, `Unauthorized`, `Forbidden`, `NotFound`,
  `Conflict`, `ValidationFailed`, `InternalError`, ...). Do not invent ad-hoc
  string errors.

## 2. Create DTOs (`include/app/dto/`)

- One oatpp DTO per request/response with `DTO_INIT`/`DTO_FIELD`.
- Field types: prefer `String`, `Int32/Int64`, `Boolean`. Use narrow types
  (`Int8`, `UInt16`, ...) only when the range is a real contract — the object
  mapper enforces it and overflow becomes HTTP 400.
- Add Swagger metadata at the endpoint (`ENDPOINT_INFO`), not in the DTO.

## 3. Validation

- Shape checks (required, format) go in `validation/` helpers or the service;
  throw `ValidationError` (HTTP 422).
- Range checks for integers are automatic via `createAppObjectMapper()` — do
  not re-check ranges manually. Add a case to `test_serialization.cpp` when
  you introduce a narrow field.
- Enum/role strings: convert with `tryParse*`, reject unknown values (no
  silent defaults).

## 4. Service (`include/app/services/`, `src/services/`)

- Exactly one public method per use-case. Takes plain C++ values, returns
  plain C++ structs. No oatpp types, no HTTP codes.
- Throws `AppException` subtypes only.

## 5. Repository (`include/app/repositories/`) — only if persistence is needed

- Extend `BaseRepository` or add a new table to an existing one. Schema setup
  must be idempotent (`CREATE TABLE IF NOT EXISTS`).
- All values as bound parameters (`QUERY` templates with `PARAM`, see
  `adding-repository.md`). Add an injection regression test.

## 6. Controller (`include/app/controllers/`, `src/controllers/`)

- Constructor-inject the service (and `JwtService` if it needs fallback
  verification). No `OATPP_COMPONENT`.
- `ENDPOINT_INFO`: summary, description, `addConsumes`, every response code
  the endpoint can actually produce with the real DTO type, `addTag`.
  Authenticated endpoints: `info->authorization = "bearer_auth"` **and**
  `addSecurityRequirement("bearer_auth")` (both — the generator asserts).
- `ENDPOINT`: validate DTO presence (`400` if body/fields missing), call the
  service, `createDtoResponse`. Catch `AppException` -> `Status + text/plain` via `AppErrorHandler`;
  catch-all -> 500 without internals.

## 7. Middleware / authentication

- New public paths must be added to `AuthInterceptor::publicPaths_` **and**
  covered by the `interceptor public/private classification` test.
- Authenticated endpoints rely on the interceptor bundle but re-verify the
  header directly as a fail-closed fallback (see `AuthController::whoAmI`).

## 8. Routing (`src/application/Application.cpp`)

- Instantiate the controller in `initRouting()` with the shared
  `objectMapper`, `router_->addController(...)`, and append its endpoints to
  the `merged` list for Swagger.

## 9. Swagger

- If `APP_WITH_SWAGGER`, the new endpoints appear automatically via the merged
  list. Verify at runtime: `GET /api-docs/oas-3.0.0.json` contains the path.

## 10. Tests

- Unit: service logic, validation, DTO round-trips.
- Integration (`test_api.cpp` pattern): real HTTP over 127.0.0.1 — success,
  unauthorized, validation-failure, and (for narrow fields) overflow cases.
- Run: `cmake --build build && ctest --test-dir build --output-on-failure`.

## 11. Configuration

- New settings go in `AppSettings` with identical leaf names in the
  `server`/`database`/`jwt`/`logging` groups of every format
  stores, defaults in `defaults()`, rules in `validate()`. Update
  `configs/app.{json,toml,xml}` and add a round-trip case in
  `test_config.cpp`.

## 12. Error handling

- Every documented status must be producible by the code. Do not document 403
  if the endpoint cannot return it.

## Conformance checklist

- [ ] No SQL/crypto/filesystem in the controller
- [ ] No oatpp types in the service signature
- [ ] All SQL values bound, none interpolated
- [ ] ENDPOINT_INFO lists only producible statuses with real DTOs
- [ ] Authenticated endpoint sets both `authorization` and security requirement
- [ ] Tests for success + each error path, all passing
