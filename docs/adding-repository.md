# Adding a Repository

Location: `include/app/repositories/`, `src/repositories/`.

## Rules

1. Inherit `BaseRepository` (holds the `shared_ptr<IDatabase>`, null-checked).
2. `ensureSchema()` with `CREATE TABLE IF NOT EXISTS`, called from
   `Application::initDatabase()` (or service seeding), never per-request DDL
   in hot paths.
3. **All values are `?` bound parameters.** Example:

```cpp
db_->executeVoid(
  "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?) "
  "ON CONFLICT(username) DO UPDATE SET password_hash=excluded.password_hash",
  {record.username, record.passwordHash, record.role});
```

4. Never build SQL with `+`/`format` on values. Table/column names are fixed
   literals in code.
5. Return `std::optional<T>` for single-row lookups; empty = not found (the
   service decides 404 vs other mapping).
6. Passwords/keys: store hashes via `auth::passwordHashHex` (active
   `APP_PASSWORD_HASH_ALGO`: SHA256/SHA384/SHA512 over OpenSSL EVP — switch
   with `-DAPP_PASSWORD_HASH_ALGO=...`, then re-seed users since stored
   hashes change), never plaintext, never log them. Hashing lives in `auth/`
   (next to JwtService, the crypto owner), never in repositories.

## Database abstraction and testing

- Code against `IDatabase` only. Backend selection is `DatabaseFactory` +
  `APP_DATABASE_SQLITE/SQLCIPHER` — repositories contain zero `#ifdef`s.
- Tests use `tests::createTestDatabase()` (`:memory:`, correct key per
  backend) and cover: schema idempotency, CRUD, upsert overwrite, and an
  injection attempt stored literally (`test_database.cpp`).
