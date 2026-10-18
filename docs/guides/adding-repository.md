# Adding a Repository

Location: `include/app/repositories/`, `src/repositories/`.

A repository is an `oatpp::orm::DbClient` subclass that owns one table. It is
the only layer that declares SQL for entities; services call it, and translate
its `app::errors::database` exceptions to HTTP statuses.

## Model

`BaseRepository` (this directory) is the shared base: it holds the
`shared_ptr<IDatabase>` (null -> `ConnectionFailed` at construction) and the
table name, and provides `tableName()`, `database()` and `tableExists()`.
Statements are declared with the oatpp `QUERY` macro:

```cpp
QUERY(findUserImp,
      "SELECT username, password_hash, role FROM " + tableName() + " WHERE username = :username;",
      PARAM(oatpp::String, username))
```

- Named `:param` placeholders, each declared with `PARAM(type, name)`. Values
  are always bound parameters — never build SQL with `+` on values. The only
  interpolation point is `tableName()` (code-controlled).
- `QUERY`-generated methods take one trailing parameter
  (`connection = nullptr`): never pass a connection. The executor is a
  `DatabaseExecutor` around the active backend (`IDatabase::ormExecutor()`),
  so every statement takes the database mutex and reuses the open handle, and
  repositories never see a connection.
- DTO parameter paths use the property name, not the C++ member:
  `:user.username`, `:user.password_hash` (see `UserDto`).
- Fetch typed results with `fetch<Wrapper>(result)` (a `BaseRepository`
  helper). Execute-stage failures already throw `database::QueryFailed`; the
  helper also covers the fetch stage (SQLCipher step errors, mapper/type
  mismatches) with the same exception.

## Rules

1. Inherit `BaseRepository` and pass your table name:
   `BaseRepository(std::move(db), "users")`.
2. Rows travel as DTOs in this directory (one file per entity, e.g.
   `UserDto.hpp`). DTO fields map to result columns by name: select explicit
   columns (never `SELECT *`), and name each field for its column — camelCase
   member plus the column as the second `DTO_FIELD` argument
   (`DTO_FIELD(oatpp::String, passwordHash, "password_hash")`).
3. `ensureSchema()` runs `CREATE TABLE IF NOT EXISTS` and then verifies the
   result through `tableExists()`; it is called from
   `Application::initDatabase()` (or service seeding), never per request.
   DDL/DML statements are portable: `CREATE TABLE IF NOT EXISTS`,
   `INSERT`/`UPDATE ... WHERE ...` — no `ON CONFLICT` shortcut needed, no
   file-backend-only SQL (`AUTOINCREMENT`, `PRAGMA`, `sqlite_master`).
4. Method contracts speak in domain errors, not optionals:
   - missing row -> `errors::database::RecordNotFound` (e.g. `getUser`,
     `updateUser`);
   - duplicate -> `errors::database::Conflict` (e.g. `createUser`);
   - bad input -> `errors::validation::InvalidValue` (validate before SQL);
   - failed SQL -> `errors::database::QueryFailed` (from the seam/`fetch`).
5. Passwords/keys: store hashes via `auth::passwordHashHex` (active
   `APP_PASSWORD_HASH_ALGO`: SHA256/SHA384/SHA512 over OpenSSL EVP — switch
   with `-DAPP_PASSWORD_HASH_ALGO=...`, then re-seed users since stored
   hashes change), never plaintext, never log them. Hashing lives in `auth/`
   (next to JwtService, the crypto owner), never in repositories.

## Database abstraction and testing

- Code against `IDatabase` only. Backend selection is `DatabaseFactory` +
  `APP_DATABASE_SQLCIPHER/POSTGRESQL` — repositories contain zero `#ifdef`s. The
  one backend-aware statement in the repository layer is
  `BaseRepository::tableExists()` (picks `sqlite_master` or
  `information_schema.tables` from `IDatabase::backendName()`).
- The plain-`?` text-row rules of `IDatabase::execute()` are for ad-hoc SQL
  (inspectors, tests); repository SQL uses the adapters' named `:param`
  templates instead. A `:` outside single-quoted literals is rejected by the
  `?`-rewriter, so consumer code uses `cast(x as text)`, not PostgreSQL's
  `x::text` — repository templates follow adapter parsing unchanged.
- Repository results are mapped with the adapters' typed mapping: text and
  integer columns work directly (PostgreSQL text casts are only needed for the
  `execute()` text-row path).
- Tests use `tests::createTestDatabase()` (`:memory:`, correct key per
  backend; fresh provisioned database per call for PostgreSQL) and
  `tests::seedUser(...)` to create users through the same repository API the
  application uses. Cover: schema idempotency, create/get/list/update/delete,
  not-found, conflict, invalid input, and an injection attempt stored
  literally (`test_database.cpp`).
