# PostgreSQL: the client-server database backend

The project supports **PostgreSQL** as a relational client-server database backend alongside the file-based SQLCipher backend. PostgreSQL integration is powered by the `oatpp-postgresql` adapter communicating with PostgreSQL via `libpq`.

This document records the taxonomy, connection model, configuration parameters, automated local server management, manual cluster administration, and build verification.

---

## Taxonomy

| Concept | Name |
| :--- | :--- |
| **CMake option (PG support)** | `APP_ENABLE_POSTGRESQL` (defines `APP_WITH_POSTGRESQL`) |
| **CMake option (user database)** | `APP_USERS_DATABASE_TYPE=POSTGRESQL` (defines `APP_DATABASE_POSTGRESQL`) |
| **Backend class** | `PostgresqlDatabase` (`backendName() == "POSTGRESQL"`) |
| **Inspector class** | `PostgresqlDocument` |
| **Local server manager** | `PostgresqlLocalServer` |
| **Dependency** | `oatpp-postgresql` 1.3.0 (`libpq` underneath) |
| **Authentication method** | SCRAM-SHA-256 |

The inspector functions use the backend name consistently: `openPostgresqlDatabase`, `listPostgresqlTables`, `postgresqlTableSchema`, `dumpPostgresqlTable`, and `dumpPostgresqlDatabase`.

---

## Connection Model & Parameters

PostgreSQL connectivity is configured in `AppSettings` under the `database` group. Unlike file-based databases that only use a path and an optional key, PostgreSQL requires standard network/socket credentials:

```json
{
  "database": {
    "path": "data/postgresql",
    "port": 5433,
    "name": "oatpp_backend",
    "user": "oatpp_tester",
    "key": "REPLACE_ME"
  }
}
```

The `REPLACE_ME` placeholder is rejected. New configurations receive a unique
password automatically; existing clusters require their matching private password.

### Parameter Semantics

- **`database.path` (`databasePath`)**: 
  - Represents the **Unix domain socket directory** of the local server (e.g., `data/postgresql`).
  - Connecting via Unix domain sockets avoids network stack overhead and eliminates open TCP ports to the external network.
- **`database.port` (`databasePort`)**:
  - The port number assigned to the server (default: `5433`, chosen to avoid conflicts with standard system PostgreSQL instances running on `5432`).
- **`database.name` (`databaseName`)**:
  - The target database name (default: `oatpp_backend`).
- **`database.user` (`databaseUser`)**:
  - The login role name (default: `oatpp_tester`).
- **`database.key` (`databaseKey`)**:
  - The database password.
  - **Mandatory**: For PostgreSQL builds, `database.key` must be a non-empty string enforcing SCRAM-SHA-256 authentication.
  - **Security Invariant**: The password is persisted in the owner-only configuration file and held in memory at runtime. It is never included in configuration validation errors or logs.

---

## Local Single-User Server Management (`PostgresqlLocalServer`)

When running in PostgreSQL mode (`APP_USERS_DATABASE_TYPE=POSTGRESQL`), the application includes an automated local server lifecycle manager (`app::infrastructure::database::PostgresqlLocalServer`):

1. **Bootstrap Phase (`Application::initDatabase`)**:
   - Checks if a PostgreSQL cluster exists at `<database.path>/pgdata/PG_VERSION`.
   - **If cluster is missing**:
     - Verifies the configured TCP port (`5433`) is not occupied by an alien process.
     - Runs `initdb` with `--auth=scram-sha-256` using an atomically created, uniquely named temporary password file (`0600` permissions) so credentials are never visible in process lists. The file is closed before `initdb` runs and removed on completion or C++ exception; cleanup errors are reported.
     - Starts the server via `pg_ctl` with socket directory set to `database.path` and logs written to `<database.path>/pg.log`.
     - Creates the configured user role (`oatpp_tester`) with password and provisions the application database (`oatpp_backend`).
   - **If cluster exists but is stopped**:
     - Starts the server via `pg_ctl` and verifies connectivity.
   - **If cluster is already active**:
     - Reuses the running instance and proceeds directly to schema validation.
2. **Shutdown Phase (`Application::stop`)**:
   - Gracefully stops the managed server with `pg_ctl stop -m fast` if the instance was spawned by the application process.

Initial provisioning requires `database.path` and its resolved ancestors to be owned by the application account or root. The socket directory must not be writable by group or other users; writable ancestors are accepted only when the sticky bit protects their entries (for example, `/tmp`). Unsafe paths are rejected before writing a password, without changing existing permissions. With a permissive umask, pre-create the socket directory with mode `0700` under trusted parents. These checks apply when creating a cluster; existing clusters keep their current startup behavior.

Cleanup covers normal completion and C++ exception unwinding. Forced termination or an operating-system refusal to delete can leave an owner-only password file behind; cleanup warnings identify the path without logging its contents.

---

## Manual CLI Administration

For development, testing, or maintenance, you can inspect and manage the local cluster manually using standard PostgreSQL client tools:

### Starting the Server
```bash
/usr/lib/postgresql/18/bin/pg_ctl \
  -D "$PWD/data/postgresql/pgdata" \
  -o "-k $PWD/data/postgresql -p 5433 -h 127.0.0.1" \
  -l "$PWD/data/postgresql/pg.log" \
  start
```

### Checking Status
```bash
/usr/lib/postgresql/18/bin/pg_ctl \
  -D "$PWD/data/postgresql/pgdata" \
  status
```

### Stopping the Server
```bash
/usr/lib/postgresql/18/bin/pg_ctl \
  -D "$PWD/data/postgresql/pgdata" \
  stop -m fast
```

### Connecting with `psql` (Interactive Shell)
```bash
psql -W \
  -h "$PWD/data/postgresql" \
  -p 5433 \
  -U oatpp_tester \
  -d oatpp_backend
```

### Authentication Verification (`pg_hba.conf`)
Verify that `pgdata/pg_hba.conf` enforces SCRAM-SHA-256:
```bash
grep -v '^#' "$PWD/data/postgresql/pgdata/pg_hba.conf" | grep -v '^$'
# Expected output:
# local   all   all                 scram-sha-256
# host    all   all   127.0.0.1/32  scram-sha-256
```

---

## Schema & ORM Compatibility

Repositories interact with PostgreSQL through `app::repositories::BaseRepository` and `DatabaseExecutor`:
- **Unified Interface**: Repositories contain zero `#ifdef` backend branches. The exact same repository code works identically against SQLCipher and PostgreSQL.
- **Portable SQL**: Table schemas use standard portable ANSI SQL types (`VARCHAR`, `INTEGER`, `BIGINT`, `BOOLEAN`).
- **Idempotency**: Schema initialization in `ensureSchema()` relies on `CREATE TABLE IF NOT EXISTS` and is safe to execute repeatedly.
- **Bound Parameters**: Statements utilize `:param` placeholders through `QUERY` templates or positional `?` placeholders through `execute()`, ensuring anti-injection safety.

---

## CMake Configuration & Build Flags

To configure and build with PostgreSQL enabled:

```bash
# Configure with PostgreSQL enabled as the primary user database backend:
cmake -S . -B build/debug \
  -DAPP_ENABLE_POSTGRESQL=ON \
  -DAPP_USERS_DATABASE_TYPE=POSTGRESQL

# Build:
cmake --build build/debug --parallel
```

### Encryption Status
PostgreSQL at-rest encryption is currently deferred. If `APP_ENCRYPTION_USER_DATABASE=ON` is combined with `APP_USERS_DATABASE_TYPE=POSTGRESQL`, CMake will reject the configuration at configure-time to prevent silent fallback to unencrypted storage. See [`docs/roadmap/postgresql_encryption.md`](roadmap/postgresql_encryption.md) for the detailed design and roadmap.

---

## Test Isolation

In integration tests (`tests/support/TestDb.hpp`):
- When testing PostgreSQL, a unique test database (`oatpp_test_<pid>_<seq>`) is provisioned dynamically via `CREATE DATABASE`.
- Tests run against this isolated database.
- Upon completion of the test suite, the temporary database is dropped via `DROP DATABASE IF EXISTS`, leaving no stale state.
