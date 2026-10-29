# Configuration

## Formats and selection

Four formats, one active at compile time:

| CMake `APP_CONFIG_FORMAT` | Define           | Parser  | Parser flag              |
| ------------------------- | ---------------- | ------- | ------------------------ |
| `JSON` (default)          | `APP_CONFIG_JSON` | jsoncpp | `APP_ENABLE_JSON_PARSER` |
| `TOML`                    | `APP_CONFIG_TOML` | toml11  | `APP_ENABLE_TOML_PARSER` |
| `XML`                     | `APP_CONFIG_XML`  | pugixml | `APP_ENABLE_XML_PARSER`  |
| `YAML`                    | `APP_CONFIG_YAML` | yaml-cpp | `APP_ENABLE_YAML_PARSER` |

Exactly one format define is set (see `cmake/options.cmake`; invalid values
fail at configure time). `createActiveStore()` / `activeFormatName()` /
`defaultConfigPath()` resolve without runtime branching.

The runtime file is always `configs/app.conf`, whatever the format — only the
on-disk syntax follows the active format. `configs/app.{json,toml,xml,yaml}`
are checked-in samples; the app itself reads/writes `app.conf`, creating it
from non-secret defaults and unique random secrets on first startup when missing
(see below). Generated and saved configurations have owner-only (`0600`)
permissions. A missing explicit
`--config <path>` is created the same way.

Each parser is independently toggleable (`APP_ENABLE_*_PARSER`, all `ON` by
default, set once in the `base` preset's `cacheVariables`). A disabled
parser's dependency is never searched, its store is never compiled, and it is
never linked. Selecting a format whose parser is disabled is a configure-time
`FATAL_ERROR`, re-asserted as a `#error` in `ConfigFactory.cpp`.

## Settings

`AppSettings` fields are identical across formats, grouped under five parents
(`server`, `database`, `jwt`, `logging`, `swagger`) with short leaf keys:

```json
{
  "server": {"host": "127.0.0.1", "port": 8080, "trustedProxies": "", "corsAllowedOrigins": "*"},
  "database": {"path": "data/postgresql", "key": "REPLACE_ME", "port": 5433, "name": "oatpp_backend",
               "user": "oatpp_tester"},
  "jwt": {"secret": "REPLACE_ME", "issuer": "oatpp-backend", "ttlSeconds": 3600},
  "logging": {"level": "INFO", "path": "logs"},
  "swagger": {"resPath": "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res"}
}
```

The `REPLACE_ME` values above are placeholders and are rejected at startup.
Use generated secrets for a new installation, or the matching private database
key/password and a unique JWT secret for an existing installation.

Leaf mapping: `host`/`port`/`trustedProxies`/`corsAllowedOrigins`, `database.path` (`databasePath`: file path
for SQLCIPHER, e.g. `data/app.db`, or Unix socket dir for
POSTGRESQL, e.g. `data/postgresql` per [`docs/postgresql.md`](postgresql.md)),
`database.key` (`databaseKey`: SQLCipher encryption key or PostgreSQL
scram-sha-256 password, never logged), `database.port`/`name`/`user`
(`databasePort`, …, PostgreSQL connection), `jwt.secret`/`issuer`/
`ttlSeconds`, `logging.level`/`path` (the log directory; see below), `swagger.resPath`
(`swaggerResPath`, Swagger UI assets dir; default is the oatpp-swagger
library's own res dir via `OATPP_SWAGGER_RES_PATH`, only used when built with
`APP_WITH_SWAGGER`). The `swagger` section is only written to generated
`app.conf` files when built with Swagger enabled (it stays in the checked-in
samples either way); a `swagger` section present while disabled is loaded but
ignored. `validate()` returns `""` when valid, otherwise a
message used in exceptions. A file that does not match the grouped schema
fails to load with a descriptive error instead of silently loading defaults
(correct the schema while preserving the existing database key).

`database.key` rule: encryption-enabled SQLCipher builds require the key field
to be present. An explicitly empty string selects plaintext storage (startup
logs a warning); an omitted key is rejected. Nonempty keys must not match the
published sample values. A wrong key fails `open()`; changing the configuration
alone does not re-encrypt an existing database. Builds without user-database
encryption have no `key` field (compiled out). PostgreSQL always requires an
explicit, nonempty, private SCRAM password.

For C++ callers, `databaseKey` is an `std::optional<std::string>`: its default
`std::nullopt` is rejected. Assign a private key for encryption, or assign `""`
to deliberately select SQLCipher plaintext. PostgreSQL rejects both omission
and an empty string.

Per-type files: SQLCIPHER writes/reads only `path` (and `key` in encryption
builds);
POSTGRESQL only `path`/`key`/`port`/`name`/`user`. Existing files are left
as-is (each backend reads only its own fields); missing files are created
with only their backend's fields. For POSTGRESQL `path` (socket dir),
`port` (non-zero), `name`/`user` (non-empty) and `key` (non-empty scram
password) are validated. PostgreSQL location defaults are `data/postgresql`,
`5433`, `oatpp_backend`, and `oatpp_tester`; the password is generated for a new
configuration and must match the existing cluster for an existing installation.

Build exclusion: `APP_USERS_DATABASE_TYPE` selects exactly one backend
(`SQLCIPHER` or `POSTGRESQL`); `APP_ENABLE_SQLCIPHER` toggles file support and
`APP_ENCRYPTION_USER_DATABASE` toggles user-database encryption
(SQLCIPHER only). File builds compile the single SQLCipher-backed
`SqlcipherDatabase` — the standard SQLite library is never searched, linked,
or compiled in any configuration. No-file builds
(`USERS=POSTGRESQL`, `ENABLE_SQLCIPHER=OFF`) compile only the PostgreSQL
backend — no file symbols, no `SqlcipherDocument`.

Startup order: 1) create `configs/app.conf` from per-type defaults and fresh secrets when
missing (skip when present); 2) load + validate it — failures in 1)-2) append
one `HH:MM:SS.mmm [CRITICAL] [config] …` line to
`<exe-dir>/logs/yyyy-mm-dd.log` (a `logs` directory is created next to the
executable when needed; same daily format as the main log) and exit 1; 3) logging; 4) database (below); 5)
REST/Oatpp components; 6) REST server; 7) serve.

Startup provisioning: SQLCIPHER boots check `database.path`: a path
elsewhere is treated as non-existent (a new database is created at the
configured path); missing file is created on open, existing files are
verified for schema + the selected bootstrap administrator
(`AuthService::ensureSeeded`). Creating that account requires an explicit
`APP_SEED_ADMIN_PASSWORD`; only the username defaults to `admin`.
`APP_SEED_ADMIN_USERNAME` selects another username and must stay configured on
later starts. Existing administrator passwords are preserved; the published
sample password aborts startup. See [administrator provisioning](authentication.md#administrator-provisioning)
for credential validation and migration. This policy applies to both backends.
With user-database encryption on,
the file is SQLCipher-encrypted via `database.key` (a missing key is
rejected; an explicitly empty key opens the file plaintext
with a startup warning; a wrong key fails `open()` with
`errors::database::ConnectionFailed`); with it off, the same SQLCipher-backed
backend opens the file as plaintext.
POSTGRESQL boots run `PostgresqlLocalServer::ensureProvisioned()` first (socket + TCP on
127.0.0.1): missing cluster -> `initdb --auth=scram-sha-256` + `pg_ctl
start` + role/db creation; stopped cluster -> start; running cluster ->
ensure role/db exist and upgrade `trust` to `scram-sha-256` automatically.
If our cluster is missing/stopped but TCP `127.0.0.1:<port>` already answers
(a foreign server created elsewhere with the same port but a different
path), startup fails with `Port <port> is already in use by another
PostgreSQL server; cannot initialize/start database at '<pgdata>' there`
and terminates; a server matching our path/port proceeds normally.

## Rate limiting and reverse proxies

`POST /auth/login` and `POST /auth/refresh` each allow 10 requests per client IP
in a sliding 60-second window, per application process. The accepted connection's
peer IP identifies direct clients; source ports and forwarding headers do not
create new quotas. IPv4-mapped IPv6 addresses normalize to IPv4.

Endpoint matching follows the HTTP router's rules. Query strings and accepted
slash variants share the registered endpoint's quota, and login/refresh remain
public for those variants. Changing a query parameter cannot reset the quota.

`server.trustedProxies` is an optional comma-separated string of **literal IP
addresses**, for example `"192.0.2.10, 2001:db8::10"`. Its default is empty:
no proxy is trusted, including loopback. Hostnames, wildcards and CIDRs are
rejected. Existing configurations without the setting keep this default.

Only a connection from an allowlisted peer may supply `X-Forwarded-For`.
The chain is parsed from right to left, skipping allowlisted proxies until
the first untrusted address. Caller-supplied prefixes cannot override that
address. Configure each trusted proxy to overwrite incoming forwarding headers
or correctly append the actual peer IP; list only proxies under your control.
`X-Real-IP` and `Forwarded` are ignored. Without `X-Forwarded-For`, the proxy's
own IP is used, so its clients share a quota.

Trusted forwarding chains are limited to 4096 bytes and 32 IPs. Empty,
duplicate, malformed or oversized headers from trusted proxies, and requests
without usable transport peer metadata, receive HTTP 429 on protected routes.
Untrusted peers' forwarding data is ignored.

The limiter stores at most 10,000 endpoint/IP buckets. At capacity, new buckets
receive HTTP 429 while existing buckets retain their quotas. Each request
expires at most 16 old buckets, ordered by their last accepted request; active
buckets are never evicted to admit a new identity. Each bucket retains at most
10 timestamps. These bounds are constructor options for embedded C++ callers;
they are not runtime file settings. Limits are local to each server process.

## CORS origins

`server.corsAllowedOrigins` is a string in all four formats. Set it to `"*"`
to allow every origin, or to comma-separated HTTP(S) origins, for example
`"https://app.example.com, http://localhost:3000"`. Spaces and tabs around
entries are ignored. Use the exact origin sent by the browser: scheme,
host (including brackets for IPv6), and port when present, without a path,
query, fragment, or user credentials. Matching is exact and case-sensitive;
there is no URL normalization, subdomain matching, or partial wildcard syntax.

Explicit empty strings, blank entries, malformed origins, embedded control characters,
and wildcards mixed with explicit origins are rejected on load, save, and
application construction. A missing setting keeps the existing `"*"` default
and is recorded for configuration completion. Generated files and all four
sample files include the setting; quote `"*"` in YAML.

Allowed origins receive `Access-Control-Allow-Origin` with that origin and
`Access-Control-Allow-Credentials: true`. Restricted policies also set
`Vary: Origin` for allowed requests. Disallowed origins receive no CORS
permission headers, including on preflight OPTIONS responses (which remain
HTTP 204). Requests without an Origin receive no CORS permission headers
under a restricted policy. CORS does not replace authentication or authorization.

The compatibility default remains permissive; configure an explicit allowlist
to restrict browser access, especially because credentialed CORS is enabled.

## Settings completeness

A missing configuration file is created with independent cryptographically
random JWT and database secrets (32 random bytes each, Base64-encoded). These
values are published only if the file is still absent, so concurrent starts
load the same persisted secrets. Subsequent starts reuse those values. Secrets are never
generated to repair an existing configuration.

Existing files must explicitly provide `jwt.secret` (at least 32 characters)
and `database.key` when that field is enabled. Missing secrets, published sample
values, wrong types, and invalid values abort startup with a descriptive error
that identifies the setting without including its value. This policy also
applies to settings passed directly to `Application`.

Missing non-secret parameters are still filled from defaults, logged as a
`group.leaf` list, and saved back. Configuration saves use an owner-only temporary
file and atomic replacement, so partially written secrets are not published.
Before database initialization can proceed, saves sync the file and its containing
directory. Established directories are the durable boundary supplied by deployment;
unchanged ancestors need only search permission, including mode-0111 directories.
New parent directories are tracked with temporary `.oatpp-dir-*.pending` sibling
files, persisted before directory creation and removed after the new entry is
synced. Parents are completed before any children or config files are created.
Concurrent startups can complete a pending directory, including after an interrupted
creator, without opening unrelated ancestors. Startups finding an existing config
still sync its containing directory to cover concurrent file publication.
Sync failures abort startup; an already published configuration is preserved for
a later retry. Pending directory markers are completed and removed on retry.
Only empty regular files with mode `0600`, owned by the effective application
UID, qualify as recovery markers. Foreign-owned files, symlinks and other invalid
entries are left untouched and cannot block an established application directory.
Creating a missing directory still requires a valid private pending record;
an unrelated entry cannot substitute for that record.

For an installation using an old sample JWT secret, configure a new private
random secret; previously issued JWTs will no longer verify. For an existing
SQLCipher database or PostgreSQL cluster using a sample key/password, back up
the data and explicitly migrate the database credentials before updating
`database.key`. Changing the setting alone cannot rotate the database key, and
regenerating a missing configuration cannot recover an existing database key.

`logging.path` was renamed from `logging.filePath`: files still using the
old name fail with a migration error (rename it while preserving existing
secrets).

## File logging

`logPath` is a **directory**, not a file — just set the path:

```json
{ "logging": {"path": "logs"} }
```

- The active file is `<dir>/yyyy-mm-dd.log` (local date), resolved on every
  write, so a new file starts automatically at midnight.
- Every line starts with `HH:MM:SS.milliseconds`, then level, tag, message:
  `14:03:22.041 [INFO] [auth] Login succeeded for user 'admin'`.
  Console/stderr output uses the same line format.
- Empty `path` (`""`) disables file logging (console only).
- The directory is created on demand; unwritable locations fall back to a
  stderr notice without crashing. `logs/` is git-ignored.

## Adding or modifying a setting

1. Add the field to `AppSettings` (+ default + `validate()` rule).
2. Read/write it in **all four** stores (`Json/Toml/Xml/YamlConfigStore`,
   each guarded by its `APP_WITH_*_PARSER` define).
3. Update `configs/app.{json,toml,xml,yaml}` samples.
4. Add round-trip + invalid-value cases to `tests/test_config.cpp`.

## Architecture

- `TextFile` (read/write/exists) never parses; stores never touch the
  filesystem except through `TextFile` (testable, mockable). The generic
  document tree (`infrastructure::documents::DocumentValue`) and the
  per-format `*Document` parsers live in `infrastructure/documents/` and are
  usable for any file, not just application config.
- `IConfigStore` is the extension point: a new format = one new class, no
  caller changes. To promote it to primary, add the CMake value + define
  branch in `ConfigFactory.cpp`.
- Refusing to save invalid settings is a feature: `save()` validates first.

## Switching formats

```bash
cmake -S . -B build -DAPP_CONFIG_FORMAT=XML
cmake --build build
./build/oatpp-backend --config configs/app.conf
```

If the config file does not exist, the server creates it from non-secret
`AppSettings::defaults()` plus newly generated secrets (marked `(created)`).
`AppSettings::defaults()` by itself is incomplete: programmatic callers must
explicitly supply secrets before constructing `Application`.

## Version file

Every startup writes `configs/version.conf` (fixed name, like `app.conf`)
with three values in the `APP_VERSION_FILE_FORMAT` syntax (default JSON,
independent parser toggle with the same fail-fast rule):

- `version`: `APP_VERSION` (`0.0.1` default).
- `releaseDate`: `APP_RELEASE_DATE` — empty means "stamp the configure date"
  (`yyyy-mm-dd`); a provided value must already match that shape.
- `startupTime`: local time of the write, `HH:MM:SS`.

The file is runtime-generated, so it is git-ignored.

The same completeness contract as the main config applies: a missing file
is created, absent fields (`version`, `releaseDate`, `startupTime`) are
completed from the current build info, and wrong types or invalid values
abort startup with a critical error for the operator to fix
(`version` must be `x.y.z`, `releaseDate` `yyyy-mm-dd`, `startupTime`
`HH:MM:SS`).
