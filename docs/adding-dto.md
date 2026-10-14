# Adding a New DTO

Location: `include/app/dto/`. Errors are `Status + text/plain` (no error DTO).

## Field types

| Wire need            | DTO type  | Notes                                            |
| -------------------- | --------- | ------------------------------------------------ |
| text                 | `String`  | null = absent; check presence in controller      |
| general integer      | `Int32/Int64` | no range surprises                            |
| constrained integer  | `Int8…UInt64, Float32/64` | range enforced by mapper; overflow -> 400 |
| true/false           | `Boolean` |                                                  |
| role/kind            | `String`  | convert via domain `tryParse*`, reject unknowns  |

## Rules

1. DTOs carry data only: no validation, no defaults with side effects, no
   business logic.
2. Narrow integer fields are a **contract**: any out-of-range JSON value fails
   deserialization before the controller runs. Add a serialization test
   (`64654646`-style) for each narrow field exposed to clients.
3. Missing vs null: oatpp leaves absent optional fields null. The controller
   must treat null required fields as 400.
4. Malformed JSON never reaches the controller: the framework returns 400
   through `AppErrorHandler`.
5. Enum representation is always the canonical string on the wire
   (`"admin"`, not `2`). Document allowed values in the endpoint description.
6. Every request DTO needs an `addConsumes<Object<...>>("application/json")`
   line in its `Z__ENDPOINT_ADD_INFO_*` body (in the controller `.cpp`);
   every response DTO an `addResponse<...>` entry per producible status.
7. Swagger field docs follow the project convention: `DTO_FIELD_INFO(name);` in the
   header (guarded by `APP_WITH_SWAGGER`), `Z__PROPERTY_ADD_INFO_name`
   bodies in `src/dto/AuthDtos.cpp`
   (only compiled when swagger is on).

## Example

See `app/dto/AuthDtos.hpp` (`LoginRequestDto` with an optional `Int8 pinCode`
exercising range validation end to end).
