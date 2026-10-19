# OatppBackend — Master Agent Guidelines & Repository Coding Standards

This document is the authoritative, machine-enforceable rulebook for all AI agents, pair programmers, and automated coding assistants working on the OatppBackend repository. All code created or modified in `include/`, `src/`, and `tests/` MUST strictly comply with these rules.

---

## 0. Fundamental Separation of Concerns

- **For Agents (`AGENTS.md` and `.agents/`):**
  - All machine-oriented instructions, strict invariants, coding guidelines, checklists, and guardrails belong in `AGENTS.md` and the `.agents/` directory.
  - Agents must treat these rules as absolute constraints.
- **For Humans (`docs/` and `README.md`):**
  - All human-oriented documentation (architectural explanations, getting started guides, design rationales, tutorials, examples) belongs in `docs/` and `README.md`.
  - Machine-specific guides or agent-targeted files must **never** be placed in `docs/`.

---

## 1. Bracing and Layout (Allman Style Everywhere)

- **Mandatory Allman Bracing:** Opening braces MUST be placed on their own line for all:
  - Namespaces
  - Classes and structs
  - Functions, constructors, destructors, and methods
  - Control flow blocks (`if`, `else`, `for`, `while`, `do`)
  - `try` and `catch` blocks
  - `switch` and scoped `case` blocks
  - Lambdas with block bodies
- **Class Layout & Access Specifiers:**
  - Access specifiers (`public:`, `protected:`, `private:`) are placed at column 0 (`AccessModifierOffset: -4`).
  - Exactly one blank line before each access specifier.
  - Class members and methods are indented 4 spaces (never tabs).
- **Braced `case` Blocks:** In `switch` statements, wrap complex `case` branches in `{ ... }` blocks with Allman braces.
- **Never Omit Braces:** Always use braces for control flow statements (`if`, `for`, `while`), even for single-line bodies.

---

## 2. Vertical Spacing & Paragraphing (Readability Rules)

Code must have clean "visual breathing room" separating logical phases:

1. **Before Returns & Throws:**
   - Always place an empty line before a `return` or `throw` statement (unless it is the only statement in a small 1-3 line function or early-return guard).
2. **After Control-Flow Blocks:**
   - Always place an empty line after the closing brace of an `if`, `else`, `for`, `while`, or `try-catch` block if further statements follow.
3. **Variable Declarations vs Logic:**
   - Place an empty line between variable declarations/initializations and the statements that use them.
4. **Member and Method Separation in Classes/Structs:**
   - Place an empty line between distinct member declarations or groups.
   - Place an empty line between constructors, destructors, and method declarations/definitions.
5. **Preprocessor Delimiters:**
   - Place an empty line before and after `#if`, `#ifdef`, `#elif`, `#else`, and `#endif` blocks that delimit sections or branches.
6. **Test Case Structure (AAA Pattern):**
   - In `TEST_CASE` bodies, visually separate the **Arrange**, **Act**, **Assert**, and **Cleanup** phases with blank lines.
   - Group long setup sequences logically with empty lines between subsystems.

---

## 3. Comments and Preprocessor Directives

- **Namespace Closing Comments:** Every closing namespace brace MUST have a comment identifying the namespace:
  ```cpp
  }  // namespace app::infrastructure::database
  }  // namespace
  ```
- **Preprocessor Closing Comments:** Every `#endif` MUST have a comment naming its opening condition or alternative set:
  ```cpp
  #endif  // APP_WITH_POSTGRESQL
  #endif  // APP_DATABASE backend selection (SQLCIPHER / POSTGRESQL)
  ```
- **Preprocessor Directives:**
  - Prefer `#ifdef FOO` or `#ifndef FOO` over `#if defined(FOO)` when checking a single macro definition.
  - Use `#if defined(...)` only for compound boolean expressions (e.g., `#if defined(FOO) && defined(BAR)`).
- **Doxygen Documentation:** Use `///` for public headers and interfaces.

---

## 4. Types, Pointers, and Idioms

- **Pointer & Reference Alignment (Left):**
  - Star/ampersand is attached to the type: `const std::string&`, `char*`, `const oatpp::Object<T>&`.
- **Guard Clauses & Early Returns:**
  - Prefer early returns (`if (!ptr) { return; }`) over deeply nesting the function body in an `if` block.
- **No Redundant `.c_str()` Roundtrips:**
  - Do NOT call `.c_str()` when constructing an `oatpp::String` from `std::string` or when passing to an API accepting `const std::string&` / `std::string_view`.
- **Move Semantics & `noexcept`:**
  - Explicitly declare move constructors and move assignment operators as `noexcept` for data-carrying classes and structs (`AppSettings`, `TokenClaims`, `LoginResult`).
  - Mark getters and predicates that cannot throw as `noexcept`.
- **Const Correctness:** Mark local variables and parameters that do not change as `const`.

---

## 5. Architectural Boundaries & Layering Invariants

Strict one-way dependency pipeline:
`Controller` ➔ `Service` ➔ `Repository` ➔ `DatabaseExecutor` / `IDatabase`

1. **Controllers (`include/app/controllers/`, `src/controllers/`):**
   - Wire format DTOs in/out only. Null-check incoming DTO bodies immediately, then call `dto->validate()`.
   - Never write inline validation logic (e.g., regex loops, field length checks) inside endpoint handlers.
   - Call exactly one service method, map results via `createDtoResponse`.
   - Never contain SQL, crypto, or direct filesystem access.
2. **DTO Self-Validation Pattern (`include/app/dto/`, `src/dto/`):**
   - Request DTOs declare `void validate() const` in headers and define them in `src/dto/`.
   - Throws `app::errors::request::Missing` if any mandatory field is null or empty.
   - Throws `app::errors::validation::InvalidValue` if format, charset, or policy constraints fail.
   - For partial update DTOs (`Patch*Dto`), validate that at least one update field is provided.
3. **REST Verb Semantics (PUT vs PATCH):**
   - `PUT /resource/{id}`: Full resource replacement. The path parameter specifies the current identifier; the request body contains all required fields and may specify a new identifier for renaming.
   - `PATCH /resource/{id}`: Partial update. The path parameter specifies the current identifier; the request body contains optional fields. At least one field must be supplied; empty bodies are rejected with HTTP 400.
4. **Protected Entity Immutability:**
   - Seeded/system accounts (such as the default `admin` account) must have immutability guardrails.
   - Attempting to delete a protected entity via `DELETE` or rename it via `PUT`/`PATCH` must be rejected with `403 Forbidden` (`app::errors::authorization::ResourceAccessDenied`).
5. **Services (`include/app/services/`, `src/services/`):**
   - Pure C++ business logic. Take and return plain C++ primitives or domain structs.
   - No Oat++ headers (`oatpp::String`, `oatpp::Object`) in service headers.
   - Throw fine-grained `app::errors::AppException` derivatives only. No HTTP status codes in services.
6. **Repositories (`include/app/repositories/`, `src/repositories/`):**
   - Subclass `BaseRepository`. Own entity SQL using `QUERY` templates and `PARAM` macros.
   - Idempotent `ensureSchema()` creating tables with `CREATE TABLE IF NOT EXISTS`.
   - Program against `IDatabase`; contain zero `#ifdef` backend branches.
7. **No `OATPP_COMPONENT`:**
   - Global component registries and service locators are strictly forbidden.
   - Constructor-inject all dependencies via `std::shared_ptr<T>`.
   - The only permitted singleton in the application is `app::logging::Logger::instance()`.

---

## 6. Security & Secret Management Invariants

1. **Zero-Trust SQL (Anti-Injection):**
   - Every dynamic parameter passed to SQL MUST be bound (`:param` in `QUERY` templates, `?` in `IDatabase::execute()`).
   - String concatenation or interpolation of values into SQL queries is strictly forbidden.
2. **Never-Log-Secrets Policy:**
   - Passwords, password hashes, JWT secrets, bearer tokens, database keys (`databaseKey`), and connection passwords must NEVER be printed to logs, console, exception messages, DSN strings, or response bodies.
3. **Cryptographic Isolation:**
   - Only `app::auth::JwtService` may handle JWT verification, signing, and tokens.
   - Only `app::auth::PasswordHash` may handle password hashing and validation.
4. **Constant-Shape Authentication Failures:**
   - Failed logins must return a generic 401 `Invalid credentials` without revealing whether the user existed or which credential was invalid.

---

## 7. Database & ORM Rules

1. **Dual-Backend Support:**
   - Support both `SQLCIPHER` and `POSTGRESQL` transparently.
   - Connection handling, mutex locking, and execution flow through `DatabaseExecutor` (retrieved via `IDatabase::ormExecutor()`).
2. **Portable SQL:**
   - Write standard ANSI SQL: explicit column lists (`SELECT id, username FROM ...`), standard types (`VARCHAR`, `INTEGER`).
   - Avoid engine-specific extensions (`AUTOINCREMENT`, `WITHOUT ROWID`, `x::text`).
3. **Schema Migrations:**
   - Schema creation must be idempotent: `ensureSchema()` can be called repeatedly without failure.
4. **SQLite Connection Scoping:**
   - When executing multiple queries within a single operation (e.g. counting before deleting), always scope read statement results in an inner `{ ... }` block so SQLite frees the connection before the subsequent query executes.

---

## 8. Configuration & 4-Store Parity (The 4-Store Rule)

Whenever a field is added to `app::config::AppSettings`:
1. Add the field to `AppSettings.hpp`, default it in `AppSettings::defaults()`, and validate it in `AppSettings::validate()`.
2. Update all 4 config stores in lockstep:
   - `src/config/JsonConfigStore.cpp`
   - `src/config/TomlConfigStore.cpp`
   - `src/config/XmlConfigStore.cpp`
   - `src/config/YamlConfigStore.cpp`
3. Update sample configs: `configs/app.json`, `configs/app.toml`, `configs/app.xml`, `configs/app.yaml`.
4. Add roundtrip tests in `tests/test_config.cpp`.
5. Maintain fallback semantics: missing files auto-populate from defaults; missing optional leaves are completed and saved; legacy keys are rejected.

---

## 9. Threading & Concurrency Lifecycle

- **No Detached Threads:** Calling `std::thread::detach()` is prohibited.
- **Managed Threads:** Use `app::application::ManagedThread` with atomic stop flags (`std::atomic<bool>& stop`).
- **Idempotent Teardown:** `Application::stop()`, `ManagedThread::stop()`, and `IDatabase::close()` must be safe to call multiple times without crashing or hanging.

---

## 10. Autonomous Self-Verification & Double-Checking Protocol

Agents must perform an exhaustive, multi-point verification before concluding any task. Never stop after writing code without executing the full protocol:

1. **Pre-Flight Test Harness Verification:**
   - Inspect `tests/support/TestRunner.hpp` before writing unit tests.
   - Use only supported macros: `TEST_CASE`, `EXPECT_TRUE`, `EXPECT_FALSE`, `EXPECT_EQ`, `EXPECT_THROW_AS`.
   - Never assume GoogleTest/Catch2 macros exist (e.g., `EXPECT_NO_THROW` is not defined; invoke the method directly and assert expected side-effects).
2. **Domain Rule Cross-Referencing:**
   - Cross-reference test payloads with the actual domain validation functions (`isValidUsername`, `isValidPassword`) before testing. Ensure test data adheres to exact character whitelists and lengths.
3. **Build System Registration:**
   - Verify that any newly created `.cpp` files are added to `CMakeLists.txt` under the appropriate library target (`app_core`, `app_main`, etc.).
4. **Clang-Format Gate:**
   - Run clang-format on all modified and new files:
     ```bash
     /home/arad/.local/bin/clang-format -i <modified_files>
     ```
5. **Full CTest Suite Execution:**
   - Build and run the entire CTest suite:
     ```bash
     ctest --test-dir build/debug-ninja --output-on-failure
     ```
   - Ensure all 14 test suites pass with a 100% success rate.
6. **Workspace Cleanliness Audit:**
   - Inspect `git status` and `git diff` to verify that no stray debug lines, unintended modifications, or untracked temporary files were left behind.
