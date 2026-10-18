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
   - Wire format DTOs in/out only. Null-check incoming DTO bodies immediately.
   - Call exactly one service method, map results via `createDtoResponse`.
   - Never contain SQL, crypto, or direct filesystem access.
2. **Services (`include/app/services/`, `src/services/`):**
   - Pure C++ business logic. Take and return plain C++ primitives or domain structs.
   - No Oat++ headers (`oatpp::String`, `oatpp::Object`) in service headers.
   - Throw fine-grained `app::errors::AppException` derivatives only. No HTTP status codes in services.
3. **Repositories (`include/app/repositories/`, `src/repositories/`):**
   - Subclass `BaseRepository`. Own entity SQL using `QUERY` templates and `PARAM` macros.
   - Idempotent `ensureSchema()` creating tables with `CREATE TABLE IF NOT EXISTS`.
   - Program against `IDatabase`; contain zero `#ifdef` backend branches.
4. **No `OATPP_COMPONENT`:**
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

## 10. Formatter & Verification Gates

Before completing any turn:
1. Run clang-format on all modified files:
   ```bash
   /home/arad/.local/bin/clang-format -i <modified_files>
   ```
2. Build and run the entire CTest suite:
   ```bash
   ctest --test-dir build/debug --output-on-failure
   ```
3. Ensure all 14 test suites pass with 100% success rate.
