# Security & Secret Management Rules for Agents

This document defines the mandatory security rules, cryptographic boundaries, and secret redaction policies enforced across the OatppBackend repository.

---

## 1. Zero-Trust SQL Invariant (Anti-Injection)

- **Parameter Binding Mandatory:** Every dynamic value passed into a SQL query MUST be bound.
  - In `QUERY` templates: Use named parameters `:param` and declare each with `PARAM(type, name)`.
  - In `IDatabase::execute()`: Use positional `?` placeholders and pass parameters via `std::vector<std::string>`.
- **String Concatenation Forbidden:** Concatenating user inputs, query parameters, or variables into SQL strings with `+` or stringstream is strictly forbidden.
- **The Only Exception:** The internal table name (`tableName()`) controlled directly by repository C++ code is permitted in `QUERY` templates.

---

## 2. Never-Log-Secrets Policy

Sensitive credentials and secrets must **NEVER** appear in:
- Log files (console or file sinks via `app::logging::Logger`)
- Exception messages or HTTP response bodies
- Database connection strings / DSN logs
- Crash dumps or error reports

### Specific Redacted Values:
- User passwords (raw or plaintext)
- Password hashes (SHA/Argon2 digests)
- JWT secrets (`AppSettings::jwtSecret`)
- Raw JWT access or refresh tokens
- Database encryption keys (`AppSettings::databaseKey` / SQLCipher key)
- Connection passwords (`AppSettings::databaseKey` for PostgreSQL)

---

## 3. Cryptographic Ownership & Isolation

- **Centralized Crypto Only:** All cryptographic primitives are strictly encapsulated in `app::auth`:
  - **Token Handling:** Handled exclusively by `app::auth::JwtService` (HS256 signing, issuer check, expiration, 30s leeway).
  - **Password Hashing:** Handled exclusively by `app::auth::PasswordHash` using OpenSSL EVP interfaces.
- **Prohibited:** No service, controller, repository, or utility may invoke OpenSSL or crypto APIs directly.

---

## 4. Authentication & Authorization Invariants

- **Constant-Shape Failures:** When authentication fails (bad password, non-existent user, expired account), always return a generic `errors::authentication::InvalidCredentials` (HTTP 401). Never reveal whether the username was found in the database (prevents user enumeration).
- **Fail-Closed Principle:** If token validation fails, interceptors and controllers must reject immediately and never proceed with an unauthenticated state.
- **Claims Stash:** When `AuthInterceptor` verifies a bearer token, it must stash claims in the request bundle using `app::auth::kClaimsBundleKey`.

---

## 5. Database Encryption Invariants

- **SQLCipher Key Application:**
  - The database key must be applied using `sqlite3_key()` inside the connection provider immediately upon opening.
  - The key must **never** be injected via `PRAGMA key = '...'` SQL text.
  - At database open, always verify the key immediately by reading `sqlite_master`.
- **Postgres DSN:**
  - Connection DSNs printed or logged must have passwords stripped or replaced with `***`.
