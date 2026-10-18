# PostgreSQL Encryption: Status, Behavior, and Implementation Guide

This file is the single reference for encryption of the PostgreSQL user
database: what is covered today, what the encryption flag does per backend,
which errors/warnings to expect, and — step by step — how to implement the
missing pieces (TLS in transit, at-rest TDE), including the challenges.

## 1. What is covered today

| Layer | Status |
| ----- | ------ |
| Authentication | ✅ scram-sha-256 (`password_encryption`, enforced `pg_hba.conf`, auto-upgraded from `trust` at startup) |
| File-database encryption | ✅ SQLCipher via `database.key` (`sqlite3_key()` at open; see §2) |
| PostgreSQL traffic encryption (TLS) | ❌ not implemented (see §4) |
| PostgreSQL at-rest encryption (TDE) | ❌ not implemented (see §5) |

Consequence: with `APP_USERS_DATABASE_TYPE=POSTGRESQL`, traffic over TCP
(`127.0.0.1:<port>`) and all files under `data/postgresql/pgdata` are
unencrypted. Passwords never cross the wire in cleartext (scram), and the
Unix socket is filesystem-permission protected — for a localhost-only
single-user server that is the effective security boundary today.

## 2. What happens when the encryption flag is enabled

`APP_ENCRYPTION_USER_DATABASE=ON` means "the user database must be
encrypted at rest", enforced fail-fast at configure time when it cannot be
honored. Per backend:

### SQLCIPHER — honored

- The generated `database` section contains `path` + `key`.
- Every connection the adapter opens is keyed through SQLCipher's
  `sqlite3_key()` before any other statement (key material never enters SQL
  text), and the key is validated at open with a real `sqlite_master` read.
  A config file without `key` receives the built-in default like any other
  missing parameter (recorded as `database.key` and written back); a present
  key of any string value is used as-is, while an explicitly empty key opens
  the file plaintext and logs a startup warning (the key is optional by
  design).
- **Wrong key:** `open()` fails with `errors::database::ConnectionFailed`
  (`invalid SQLCipher key: file is not a database`). The admin seeding in
  `AuthService::ensureSeeded()` therefore never runs and startup aborts with
  a non-zero exit — by design (fail closed, never boot on an unreadable
  database).
- **Existing plaintext database + newly enabled flag:** opening a plaintext
  file *with* a key does NOT convert it — the open fails exactly like a
  wrong key. Migration requires an explicit export step (challenge §6.1):
  1. Open the plaintext file with an empty key.
  2. `ATTACH DATABASE '<new>' AS encrypted KEY '<key>';`
  3. `SELECT sqlcipher_export('encrypted');`
  4. Swap the files, then boot with the key.
- **Disabling the flag afterwards:** symmetric problem — an encrypted file
  opened with an empty key fails the same way. Export back to plaintext
  first (same steps in reverse), or keep the flag on.

### POSTGRESQL — refused (configure-time error)

```
APP_ENCRYPTION_USER_DATABASE=ON is not supported with
APP_USERS_DATABASE_TYPE=POSTGRESQL (see docs/roadmap/postgresql_encryption.md)

```

Rationale: silently booting an unencrypted cluster while the operator asked
for encryption would violate the flag's contract. A warning would be
missed; a hard error cannot be. The `database.key` in PostgreSQL builds
remains the scram role password (authentication, unchanged).

## 3. Error / warning catalog (encryption-related)

| Situation | Signal |
| --------- | ------ |
| `POSTGRESQL` + encryption `ON` at configure time | `FATAL_ERROR` (see §2), configure aborts |
| SQLCipher config file without `database.key` | completed from defaults (`database.key` recorded and written back); a present key of any value is used as-is; an explicitly empty key means plaintext (startup warning) |
| SQLCipher opened with a wrong key (or an encrypted file opened with an empty key) | `errors::database::ConnectionFailed` at `open()` (`invalid SQLCipher key: file is not a database`); startup aborts |
| Legacy `trust` auth found in `pg_hba.conf` | `logWarn` + automatic upgrade to `scram-sha-256` + restart (not an error) |

## 4. Future work I: TLS in transit — implementation guide

Goal: encrypt TCP traffic (`sslmode=require` minimum, `verify-ca` ideally).
Prerequisite (verified locally): our PostgreSQL 18 is built `--with-openssl`
and the `openssl` CLI is available, so no new system packages are needed.

### Steps

1. **Server certificate at cluster creation** (`PostgresqlLocalServer::runInitdb`,
   after `initdb`):
   `openssl req -newkey rsa:2048 -nodes -keyout <pgdata>/server.key -x509 -days 825 -out <pgdata>/server.crt -subj "/CN=localhost"`,
   then `chmod 0600 server.key` (the server refuses to start otherwise).
2. **Enable SSL**: `ssl = on` (+ explicit `ssl_cert_file`/`ssl_key_file`
   if the files live outside `pgdata`) via `postgresql.conf`/`ALTER SYSTEM`,
   and add `hostssl` lines to `pg_hba.conf` (keep scram as the auth method).
   Restart (cert/key changes require restart; `reload` re-reads them but a
   first-time `ssl=on` flip is safest with restart).
3. **Client side** (`PostgresqlDatabase::open` conninfo): connect over TCP
   (`host=127.0.0.1`, not the socket dir) with `sslmode=require` (encryption
   only) or `verify-ca` + `sslrootcert=<pgdata>/server.crt` (also defeats
   MITM). Never log the conninfo.
4. **Provisioning flow** (`ensureProvisioned`): after `startServer`, probe
   with the TLS conninfo; treat "server running but TLS handshake fails" as
   a distinct error (cert missing vs. wrong key). Existing clusters migrate
   by steps 1–2 + restart (same trust→scram style upgrade path as today).
5. **Rotation**: regenerate cert/key (same step 1), `pg_ctl restart`, replace
   every distributed `server.crt` root copy. With a self-signed CA there is
   no revocation story — document the restart window.
6. **Tests/docs**: `test_matrix.sh` PG cases gain TLS variants
   (`PGSSLMODE=require`), `docs/postgresql.md` gains a TLS section,
   `testing.md` setup uses `hostssl`.

### Challenges (why this was deferred)

1. **TLS does not cover Unix sockets.** Per the PostgreSQL docs and upstream
   discussion, SSL/TLS applies to TCP/IP only; socket connections never use
   it. Our app traffic goes over the socket dir today, so enabling TLS alone
   encrypts *nothing we send* — the app must switch its own connections to
   `127.0.0.1` first, giving up the socket's filesystem-permission boundary
   and adding TCP setup to every maintenance probe and test helper.
2. **Threat model on localhost is thin.** With `-h 127.0.0.1` + socket dir,
   an attacker who can sniff loopback or read `pgdata` can usually also read
   the socket, the config `database.key`, or process memory — TLS moves
   little. It matters once clients are remote, which we have none of.
3. **Self-signed certificate management.** No CA infrastructure exists in the
   project: issuance, distribution of the root cert to every client, expiry
   tracking (825-day cap for browser-trusted certs; self-signed can be
   longer but rotation is still manual), and restart coordination all become
   new operational surface, including in the auto-provision/restart paths
   and their tests.
4. **Failure-mode ambiguity.** TLS handshake failures, expired certs, and
   wrong-password scram failures all surface as `ConnectionFailed`; the
   startup state machine needs a new branch to tell "cert broken, fix certs"
   apart from "key wrong" and "foreign server on my port".

## 5. Future work II: at-rest encryption (TDE)

- Community PostgreSQL 18 has **no built-in transparent data encryption**.
  The moved-target option is `pg_tde`, which requires **Percona Server for
  PostgreSQL** (or the limited `tde_heap_basic` community variant on PG 16/17
  without index encryption or logical replication) — i.e. replacing the
  distro PostgreSQL packages, plus a key provider (Vault/KMIP, or a local
  keyfile that is dev-only). Our `initdb`/`pg_ctl`-driven local-cluster flow
  would need to drive Percona packaging, `shared_preload_libraries`, and
  per-database `CREATE EXTENSION` + key setup.
- The realistic near-term at-rest story remains **disk-level encryption**
  (LUKS) on the host holding `data/`, which is outside the application's
  scope to configure (documented here so the gap is explicit, not hidden).

## 6. Operational notes

1. **Plaintext↔encrypted file migration is manual** (see §2 export steps).
   Never flip the encryption flag on an existing database file and expect
   the app to convert it — startup will fail closed instead.
2. **Backups inherit the format**: encrypted files back up encrypted (good);
   keep the `database.key` in the same secret store as any backup.
3. **Logs never carry secrets**: conninfo strings, SQLCipher keys (they never
   enter SQL text at all), and passwords are never logged — only
   "key applied, not logged" style confirmations.
