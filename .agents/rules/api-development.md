# API Development Rules & Conformance Checklist for Agents

This document defines the strict, machine-enforceable rules for adding new endpoints, DTOs, services, and repositories to the OatppBackend repository.

---

## 1. File Placement & Artifact Map

| Artifact | Header Path | Source Path | Codegen / Header Only |
| -------- | ----------- | ----------- | --------------------- |
| **DTO (wire)** | `include/app/dto/` | N/A | Header-only (Oat++ DTO codegen) |
| **Controller** | `include/app/controllers/` | `src/controllers/` | Declared in header, defined in `.cpp` |
| **Service** | `include/app/services/` | `src/services/` | Standard C++ class |
| **Repository** | `include/app/repositories/` | `src/repositories/` | Subclass of `BaseRepository` |
| **Row DTO** | `include/app/repositories/` | N/A | Header-only (ORM mapping) |
| **Domain Type** | `include/app/domain/` | `src/domain/` | Plain C++ `enum class` + helpers |
| **Validation** | `include/app/validation/` | `src/validation/` | Free functions / predicates |
| **Middleware** | `include/app/middlewares/` | `src/middlewares/` | Oat++ `RequestInterceptor` subclass |

---

## 2. Controller Rules

1. **Header Declaration:**
   - Inherit `oatpp::web::server::api::ApiController` enclosed by `OATPP_CODEGEN_BEGIN(ApiController)` and `OATPP_CODEGEN_END(ApiController)`.
   - Endpoints are declared with `ENDPOINT(METHOD, PATH, name, ...)`; the function body MUST be in the `.cpp` file.
   - Guarded Swagger metadata in header: `#ifdef APP_WITH_SWAGGER` ... `ENDPOINT_INFO(name);` ... `#endif  // APP_WITH_SWAGGER`.
2. **Implementation (`.cpp`):**
   - Define endpoint methods returning `std::shared_ptr<OutgoingResponse>`.
   - Validate DTO presence immediately: throw `errors::request::Missing("...")` if required body or properties are null.
   - Invoke exactly one service method.
   - Return success using `createDtoResponse(Status::CODE_200, outDto)`.
   - Let `AppException` derivatives bubble up to `AppErrorHandler`.
3. **Authenticated Endpoints:**
   - Must set **BOTH** requirements in `ENDPOINT_INFO`:
     ```cpp
     info->authorization = "bearer_auth";
     info->addSecurityRequirement("bearer_auth");
     ```
   - Must verify the `Authorization` header directly as a fail-closed fallback if the claim bundle is absent.
4. **Registration:**
   - Instantiate and register in `Application::initRouting()`.
   - Append endpoint list to the `merged` list for Swagger documentation.

---

## 3. DTO Rules

1. **Wire Shapes Only:** DTOs define serializable data transfer objects; they must not contain business logic or database methods.
2. **Field Types:**
   - Text: `oatpp::String` (null when omitted in JSON).
   - Standard Integers: `oatpp::Int32` or `oatpp::Int64`.
   - Constrained Integers: `oatpp::Int8`, `oatpp::UInt8`, `oatpp::Int16`, `oatpp::UInt16`. The `RangeValidatingDeserializer` rejects out-of-range values with HTTP 400.
   - Booleans: `oatpp::Boolean`.
   - Enums: Represented as canonical strings (`"admin"`, `"operator"`), never raw integers.
3. **Swagger Properties:**
   - Guarded `DTO_FIELD_INFO(name);` in the header under `#ifdef APP_WITH_SWAGGER`.
   - `Z__PROPERTY_ADD_INFO_name` definitions placed in the matching `.cpp` under `src/dto/`.

---

## 4. Service Rules

1. **Signatures:** Clean, idiomatic C++ taking primitives, `std::string`, `std::vector`, or domain types. No Oat++ types (`oatpp::String`, `oatpp::Object`) in service headers.
2. **Orchestration:** Performs input validation, calls repositories, interacts with `JwtService` or `PasswordHash`, and returns plain structs (e.g., `LoginResult`).
3. **Exceptions:** Throws only domain exceptions derived from `app::errors::AppException`. Never return HTTP status codes from services.
4. **Seeding:** Data initialization must happen in an explicit `ensureSeeded()` method called during startup (`Application::initServices()`), never lazily on request threads.

---

## 5. Repository Rules

1. **Inheritance & Construction:**
   - Inherit `BaseRepository`.
   - Pass the database handle and table name: `BaseRepository(std::move(db), "table_name")`.
2. **SQL Declarations:**
   - Use Oat++ `QUERY` macros with named `:param` placeholders and `PARAM(type, name)` declarations.
   - String concatenation of values is strictly forbidden.
   - Explicit column lists only: `SELECT col1, col2 FROM ...` (never `SELECT *`).
3. **Row Mapping:**
   - Map query results to row DTOs using `fetch<Wrapper>(result)`.
   - Row DTOs live in `include/app/repositories/` (e.g., `UserDto.hpp`).
4. **Schema Setup:**
   - Provide an idempotent `ensureSchema()` executing `CREATE TABLE IF NOT EXISTS` and verifying with `tableExists()`.

---

## 6. Prohibited Shortcuts & Hard Constraints

- ❌ **No SQL string concatenation** for values.
- ❌ **No crypto logic** outside of `JwtService` and `PasswordHash`.
- ❌ **No `OATPP_COMPONENT`** dependency lookups.
- ❌ **No C++20 features** (`std::jthread`, concepts, ranges). C++17 only.
- ❌ **No unhandled exceptions** or leaking internal stack traces / secrets.
- ❌ **No detached threads** (`std::thread::detach()` is forbidden).

---

## 7. Pre-Merge Conformance Checklist

Before finishing any API addition, verify each item:
- [ ] Controller constructor receives all services via `std::shared_ptr`.
- [ ] No Oat++ headers included in service header files.
- [ ] All request body fields are validated for presence before calling the service.
- [ ] Authenticated endpoints specify both `authorization = "bearer_auth"` and `addSecurityRequirement("bearer_auth")`.
- [ ] New public routes are explicitly added to `AuthInterceptor::publicPaths_`.
- [ ] Repository queries use `:param` bindings with `PARAM` macros.
- [ ] Allman bracing and vertical spacing rules are strictly followed.
- [ ] Full CTest suite passes with 0 failures (`ctest --test-dir build/debug --output-on-failure`).
