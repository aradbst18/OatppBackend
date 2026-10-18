# Database & Repository Rules for Agents

This document defines the rules, patterns, and conventions for interacting with databases, writing repositories, and handling schema migrations in OatppBackend.

---

## 1. Supported Backends & Abstraction

The system supports two database backends selectable at compile time:
1. **SQLCIPHER:** Default file-based or in-memory (`:memory:`) SQLite database with optional AES-256 encryption via SQLCipher.
2. **POSTGRESQL:** Client-server relational database connected via `oatpp-postgresql` (libpq) with SCRAM-SHA-256 authentication.

### Core Rules:
- All repository code must program against `app::infrastructure::database::IDatabase` and `BaseRepository`.
- Repositories must contain **ZERO** `#ifdef` backend checks.
- Backend-specific branching is encapsulated inside `DatabaseFactory`, `DatabaseExecutor`, and `BaseRepository::tableExists()`.

---

## 2. Repository Design with `BaseRepository`

- Every repository subclass must inherit `app::repositories::BaseRepository`.
- The constructor must pass the database instance and table name:
  ```cpp
  UserRepository::UserRepository(std::shared_ptr<infrastructure::database::IDatabase> db)
      : BaseRepository(std::move(db), "users")
  {
  }
  ```
- Use `tableName()` in `QUERY` templates to ensure portable, consistent referencing.

---

## 3. Query Declarations & Templates

- Declare queries with the Oat++ `QUERY` macro:
  ```cpp
  QUERY(getUserByUsername,
        "SELECT id, username, password_hash, role FROM " + tableName() + " WHERE username = :username;",
        PARAM(oatpp::String, username))
  ```
- **Named Parameters:** Always use `:param` placeholders in the SQL string, paired with `PARAM(type, name)`.
- **Explicit Projection:** Always specify column names (`SELECT id, name FROM ...`). Never use `SELECT *`.
- **Fetch Results:** Use `fetch<Wrapper>(result)` or `fetchOne<Wrapper>(result)` helpers provided by `BaseRepository`.

---

## 4. Schema Migration & Idempotency

- Every repository must provide an `ensureSchema()` method.
- Use portable DDL with existence checks:
  ```sql
  CREATE TABLE IF NOT EXISTS users (
      id VARCHAR(64) PRIMARY KEY,
      username VARCHAR(64) NOT NULL UNIQUE,
      password_hash VARCHAR(256) NOT NULL,
      role VARCHAR(32) NOT NULL
  );
  ```
- Schema creation must be strictly idempotent — calling `ensureSchema()` multiple times must succeed without error or data alteration.
- Do not use database-specific extensions like `AUTOINCREMENT`, `WITHOUT ROWID`, or non-standard trigger syntax.

---

## 5. Thread Safety & DatabaseExecutor

- Repositories never manage raw database connections.
- Query execution flows through `DatabaseExecutor` (retrieved via `IDatabase::ormExecutor()`).
- `DatabaseExecutor` acquires a mutex on the database instance, ensuring thread-safe access across concurrent worker threads.

---

## 6. Testing Repositories

- Use `tests::createTestDatabase()` from `tests/support/TestDb.hpp`.
  - For SQLCipher: Creates an in-memory (`:memory:`) or scratch database with encryption applied if configured.
  - For PostgreSQL: Provisions a clean, unique test database (`oatpp_test_<pid>_<seq>`) and tears it down after tests.
- Always include test cases for:
  1. Idempotent schema initialization (`ensureSchema` called twice).
  2. Full CRUD cycle (Create, Read, Update, Delete).
  3. `RecordNotFound` on non-existent records.
  4. `Conflict` on unique constraint violation.
  5. SQL injection resistance (verifying characters like `' OR '1'='1` are treated as literal text).
