# Architecture & Layering Rules for Agents

This document defines the mandatory architectural boundaries, layering rules, and component responsibilities that all AI agents must follow when creating or modifying code in this repository.

---

## 1. Dependency Flow & Layering Invariants

The application follows a strict one-way dependency pipeline:

```
Controllers  (wire format, HTTP endpoints)
    │
    ▼
Services     (business logic, domain orchestration)
    │
    ▼
Repositories (entity queries, table ownership)
    │
    ▼
Database     (DatabaseExecutor, IDatabase, raw backends)
```

### Strict Boundaries:
- **No Layer Skipping:** Controllers must never call repositories or database objects directly.
- **No Reverse Dependencies:** Lower layers (Repositories, Services) must never know about or include higher layers (Controllers, DTOs, HTTP status codes).
- **No Oat++ in Services:** Services take and return standard C++ types or domain types (`app::domain`). Do not include Oat++ headers in service interfaces.
- **DTOs are Wire Only:** DTOs live in `include/app/dto/` and are only used by Controllers and serialization layers. Repositories define their own row DTOs in `include/app/repositories/` (e.g., `UserDto.hpp`).

---

## 2. Dependency Injection & Ownership

- **Constructor Injection Everywhere:** Every class must receive its dependencies via `std::shared_ptr<T>` in its constructor.
- **Null Verification:** Constructors must validate incoming dependency pointers and throw `errors::request::Missing` or `errors::AppException` if a required collaborator is null.
- **Prohibited: `OATPP_COMPONENT`:** The global component registry macro `OATPP_COMPONENT` is strictly forbidden. Never use global service locators or component lookups.
- **Singletons:** The only allowed singleton in the entire application is `app::logging::Logger::instance()`.

---

## 3. Component Responsibility Matrix

| Component Layer | Location | Responsibilities | Prohibited Actions |
| --------------- | -------- | ---------------- | ------------------ |
| **Controllers** | `include/app/controllers/`<br>`src/controllers/` | Validate DTO presence, invoke 1 service method, return `createDtoResponse`. Declare `ENDPOINT_INFO`. | Direct SQL, crypto, filesystem I/O, business logic. |
| **Services** | `include/app/services/`<br>`src/services/` | Business validation, orchestrating repositories, invoking `JwtService`/`PasswordHash`. | Oat++ types, HTTP status codes, raw SQL queries. |
| **Repositories** | `include/app/repositories/`<br>`src/repositories/` | Declaring `QUERY` templates, executing queries via `DatabaseExecutor`, `ensureSchema()`. | Business validation, password hashing, JWT operations. |
| **Domain** | `include/app/domain/`<br>`src/domain/` | Pure C++ domain types (`enum class`), string conversions (`toString`, `tryParse`). | External dependencies, Oat++ macros. |
| **DTOs** | `include/app/dto/` | Oat++ DTO definitions for wire serialization. | Validation logic, methods with side effects. |
| **Middlewares** | `include/app/middlewares/`<br>`src/middlewares/` | Cross-cutting request inspection (`AuthInterceptor`, `CorsInterceptor`, `RateLimitInterceptor`). | Business state mutation, database transactions. |

---

## 4. Lifecycle & Concurrency Management

- **Lifecycle Phases in `Application`:**
  1. `initLogging()`: Setup logging directory and levels.
  2. `initDatabase()`: Instantiate `IDatabase` via `DatabaseFactory`, run schema migrations (`ensureSchema()`).
  3. `initServices()`: Instantiate domain services, seed initial data (`ensureSeeded()`).
  4. `initRouting()`: Add controllers to router, merge endpoint lists for Swagger.
  5. `start()` / `run()`: Start listener and blocking I/O loop.
  6. `stop()`: Graceful, idempotent shutdown.
- **Thread Management:**
  - Raw `std::thread` is forbidden.
  - All background threads must use `app::application::ManagedThread`.
  - Calling `thread.detach()` is strictly prohibited.
  - Always use atomic cancellation tokens (`std::atomic<bool>& stop`) to signal termination.
