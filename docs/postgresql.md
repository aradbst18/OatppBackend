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
    "path": "_data/postgresql",
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
  - Represents the **Unix domain socket directory** of the local server (e.g., `_data/postgresql`).
  - The local manager also enables TCP on loopback (`127.0.0.1`), not external interfaces.
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
   - Closing the application closes its connections; the local PostgreSQL server remains running. Stop it manually when required (see below).

Initial provisioning requires `database.path` and its resolved ancestors to be owned by the application account or root. The socket directory must not be writable by group or other users; writable ancestors are accepted only when the sticky bit protects their entries (for example, `/tmp`). Unsafe paths are rejected before writing a password, without changing existing permissions. With a permissive umask, pre-create the socket directory with mode `0700` under trusted parents. These checks apply when creating a cluster; existing clusters keep their current startup behavior.

> [!TIP]
> **Troubleshooting: "Unsafe initdb password directory"**
>
> On Linux distributions using User Private Groups (default `umask 0002`), directories created in your workspace typically receive `0775` permissions (`drwxrwxr-x`, group-writable). If startup fails with:
> ```text
> Startup failed [DatabaseConnectionFailed]: Unsafe initdb password directory '...': require application or root ownership and no untrusted write access
> ```
> You can resolve this with a single command from the project root to restrict the socket directory and remove group-write permissions on its ancestors:
> ```bash
> chmod 700 _data/postgresql && chmod go-w _data . .. ../.. ../../..
> ```
> Alternatively, you can configure `database.path` in your config to an owner-only directory under `/tmp` (e.g. `path = "/tmp/oatpp_postgresql"` created via `mkdir -m 700 -p /tmp/oatpp_postgresql`), which passes automatically because `/tmp` has the sticky bit set.

Cleanup covers normal completion and C++ exception unwinding. Forced termination or an operating-system refusal to delete can leave an owner-only password file behind; cleanup warnings identify the path without logging its contents.

---

## Manual CLI Administration

This English runbook incorporates `archives/helper.txt` and `archives/remove.txt`.
It uses the renamed `_data` runtime directory and replaces their published sample
password with an interactive private password. A PostgreSQL **cluster** contains
multiple databases and roles: stopping the server does not delete a database, and
dropping one database does not remove the cluster.

### 1. Prepare and initialize a cluster

Run from the project root as the ordinary account that will run the application,
not as root. Use binaries matching the cluster's PostgreSQL major version. These
examples use PostgreSQL 18; adjust `PG_BIN` for your installation (check
`pg_config --bindir`). Do not initialize an existing cluster again.

```bash
cd /path/to/OatppBackend
PG_BIN=/usr/lib/postgresql/18/bin
PGROOT="$PWD/_data/postgresql"
PGDATA="$PGROOT/pgdata"
PGPORT=5433
mkdir -p "$PGROOT"
chmod 700 "$PGROOT"
chmod go-w _data . .. ../.. ../../..
```

If a root-owned empty directory was created beforehand, have its owner assign it
to your application account before proceeding. Never change ownership on an
unrelated cluster. Check that port 5433 is free before starting a new cluster.

Read a unique private password; it must match `database.key` in the runtime
application config. The local manager uses that password for both `postgres` and
the application role. Keep it out of command arguments and shell history:

```bash
(
    umask 077
    read -r -s -p 'Private local PostgreSQL password: ' database_password
    printf '\n'
    password_file=$(mktemp "$PGROOT/.initpw.XXXXXX")
    trap 'rm -f -- "$password_file"' EXIT
    printf '%s' "$database_password" > "$password_file"
    "$PG_BIN/initdb" -D "$PGDATA" -U postgres \
        --auth=scram-sha-256 --pwfile="$password_file"
)
```

`initdb` creates the `pgdata` directory and initial system databases. It refuses
nonempty data directories. See the [official initdb reference](https://www.postgresql.org/docs/current/app-initdb.html).

### 2. Activate, provision, and verify

```bash
"$PG_BIN/pg_ctl" -D "$PGDATA" \
    -o "-k $PGROOT -p $PGPORT -h 127.0.0.1" -l "$PGROOT/pg.log" -w start
"$PG_BIN/pg_ctl" -D "$PGDATA" status
"$PG_BIN/pg_isready" -h "$PGROOT" -p "$PGPORT"
"$PG_BIN/createuser" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --login --createdb --pwprompt oatpp_tester
"$PG_BIN/createdb" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --owner=oatpp_tester oatpp_backend
"$PG_BIN/psql" -h "$PGROOT" -p "$PGPORT" -U oatpp_tester -W \
    -d oatpp_backend -c 'SELECT current_database(), current_user, version();'
```

For `createuser`, enter the same private password for the new role and the
`postgres` connection. `CREATEDB` is useful for local tests/inspection; grant it
only where needed. Create the role and database once, not on every start. Set
`database.path`, `port`, `name`, `user`, and `key` to the corresponding values in
`_configs/app.<format>`. Interactive `psql` exits with `\q`.

Check both socket and TCP authentication:

```bash
sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' "$PGDATA/pg_hba.conf"
# Relevant local and host entries must use scram-sha-256, never trust.
env -u PGPASSWORD -u PGSERVICE PGPASSFILE=/dev/null \
    "$PG_BIN/psql" -w -h "$PGROOT" -p "$PGPORT" -U oatpp_tester \
    -d oatpp_backend -c 'SELECT 1;'
# Expected: failure because no password was supplied.
"$PG_BIN/psql" -W -h 127.0.0.1 -p "$PGPORT" -U oatpp_tester \
    -d oatpp_backend -c 'SELECT 1;'
# Expected: succeeds with the configured password.
```

For a cluster previously initialized with `trust`, connect as `postgres` and set
`SET password_encryption = 'scram-sha-256';`, then use `\password postgres` and
`\password oatpp_tester` in `psql`. Set both passwords before replacing relevant
`trust` methods with `scram-sha-256` in `pg_hba.conf`. Reload the server and repeat
the authentication checks. Do not replace unrelated authentication rules blindly.

### 3. Deactivate, reactivate, or reload

Stop the application first so it does not reconnect or automatically restart the
cluster. Keep the shell variables from step 1 in each administration session.

```bash
"$PG_BIN/pg_ctl" -D "$PGDATA" -m fast -w stop
"$PG_BIN/pg_ctl" -D "$PGDATA" status
# Expected after shutdown: no server running (nonzero exit status).
# Reactivate using the start command in step 2.
# Reload edited authentication/settings while running:
"$PG_BIN/pg_ctl" -D "$PGDATA" reload
# Restart when a setting requires it:
"$PG_BIN/pg_ctl" -D "$PGDATA" -m fast -w restart
```

Fast shutdown disconnects clients and rolls back active transactions. Use
`pg_ctl`, not removal of `postmaster.pid` or a forced process kill. See the
[official pg_ctl reference](https://www.postgresql.org/docs/current/app-pg-ctl.html).

### 4. Back up and test restoration

Keep backups outside `PGROOT` so cluster removal cannot erase them. With the
server running:

```bash
BACKUP_DIR="$HOME/oatpp-backups"
mkdir -p "$BACKUP_DIR"
chmod 700 "$BACKUP_DIR"
(umask 077; "$PG_BIN/pg_dump" -h "$PGROOT" -p "$PGPORT" -U oatpp_tester -W \
    -Fc -f "$BACKUP_DIR/oatpp_backend.dump" oatpp_backend)
"$PG_BIN/createdb" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --owner=oatpp_tester oatpp_restore_check
"$PG_BIN/pg_restore" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --exit-on-error -d oatpp_restore_check "$BACKUP_DIR/oatpp_backend.dump"
```

Inspect the restored database before relying on the backup. A database dump does
not include cluster-wide roles: record/recreate them, or use `pg_dumpall --globals-only`
with owner-only output for a full-cluster retirement. See the
[official pg_dump reference](https://www.postgresql.org/docs/current/app-pgdump.html).

### 5. Delete a database or remove the entire cluster

These actions permanently delete data. Stop the application, verify a restorable
backup, and identify the exact target first. To remove only one database while
the server is running:

```bash
"$PG_BIN/dropdb" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --interactive oatpp_backend
# Optional: remove the application role only after all its databases/objects
# (including any restore-check database) have been removed or reassigned.
"$PG_BIN/dropuser" -h "$PGROOT" -p "$PGPORT" -U postgres -W \
    --interactive oatpp_tester
```

Active connections can prevent removal; close them rather than forcing a drop
without review. See the [official dropdb reference](https://www.postgresql.org/docs/current/app-dropdb.html).

To retire the **whole cluster**, including every database and role, first stop it
with `pg_ctl -D "$PGDATA" -m fast -w stop` and confirm shutdown. Inspect the absolute
paths below, ensure no other application uses this cluster, then remove it:

```bash
printf 'Cluster to remove: %s\nSocket/log directory: %s\n' "$PGDATA" "$PGROOT"
# Run only after successful shutdown and reviewing these paths:
rm -ri -- "$PGDATA"
# Remove the remaining socket/log directory only after confirming it contains
# nothing else you need; interactive removal asks before deleting entries.
rm -ri -- "$PGROOT"
```

The archives' deletion command omitted the leading slash in `/usr/lib/.../pg_ctl`;
the commands above use `PG_BIN` consistently. Starting the application again can
recreate a missing cluster/database, so keep it stopped during retirement.


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

## Manual database lifecycle checklist

- [ ] Select the correct PostgreSQL binaries, project directory, port, account, and private password.
- [ ] Prepare an owned, private socket directory and initialize the cluster once.
- [ ] Activate the server; verify status and readiness.
- [ ] Create the application role and database, and align `_configs/app.<format>` settings.
- [ ] Verify socket/TCP connectivity and SCRAM authentication, including passwordless rejection.
- [ ] Start the application and verify schema initialization and administrator provisioning.
- [ ] Monitor logs and disk space; back up the database and required roles outside the cluster directory.
- [ ] Test restoration into a separate database.
- [ ] For maintenance, stop the application, deactivate PostgreSQL cleanly, and confirm shutdown.
- [ ] Reactivate or reload/restart as needed, then verify connectivity before restarting the application.
- [ ] For removal, stop clients, confirm a usable backup, and drop only the intended database.
- [ ] Remove unused restore-check databases/roles only after checking ownership and dependencies.
- [ ] If retiring the whole cluster, stop it and confirm shutdown before removing its data, socket, and log directories.
- [ ] Keep the application stopped or reconfigure it so automatic provisioning does not recreate retired resources.
