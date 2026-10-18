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
from defaults on first startup when missing (see below). A missing explicit
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
  "server": {"host": "127.0.0.1", "port": 8080},
  "database": {"path": "data/postgresql", "key": "oatpp_test_pw", "port": 5433, "name": "oatpp_backend",
               "user": "oatpp_tester"},
  "jwt": {"secret": "change-me-in-production", "issuer": "oatpp-backend", "ttlSeconds": 3600},
  "logging": {"level": "INFO", "path": "logs"},
  "swagger": {"resPath": "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res"}
}
```

Leaf mapping: `host`/`port`, `database.path` (`databasePath`: file path
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
(delete `configs/app.conf` to regenerate it from defaults).

`database.key` rule: the SQLCipher key is optional — a missing key is
completed from defaults like every other parameter (recorded in the
completion log and written back), a present key of any string value is used
as-is, and an explicitly empty key opens the file plaintext (startup logs a
warning). A wrong key fails `open()` (validated with a schema read) — the
operator's to fix. Builds without user-database encryption have no `key`
concept at all (compiled out); POSTGRESQL always requires it (non-empty
scram password).
Per-type files: SQLCIPHER writes/reads only `path` (and `key` in encryption
builds);
POSTGRESQL only `path`/`key`/`port`/`name`/`user`. Existing files are left
as-is (each backend reads only its own fields); missing files are created
with only their backend's fields. For POSTGRESQL `path` (socket dir),
`port` (non-zero), `name`/`user` (non-empty) and `key` (non-empty scram
password) are validated. POSTGRESQL defaults match
[`docs/postgresql.md`](postgresql.md) exactly (`data/postgresql`, `5433`,
`oatpp_backend`, `oatpp_tester`, `oatpp_test_pw`).

Build exclusion: `APP_USERS_DATABASE_TYPE` selects exactly one backend
(`SQLCIPHER` or `POSTGRESQL`); `APP_ENABLE_SQLCIPHER` toggles file support and
`APP_ENCRYPTION_USER_DATABASE` toggles user-database encryption
(SQLCIPHER only). File builds compile the single SQLCipher-backed
`SqlcipherDatabase` — the standard SQLite library is never searched, linked,
or compiled in any configuration. No-file builds
(`USERS=POSTGRESQL`, `ENABLE_SQLCIPHER=OFF`) compile only the PostgreSQL
backend — no file symbols, no `SqlcipherDocument`.

Startup order: 1) create `configs/app.conf` from per-type defaults when
missing (skip when present); 2) load + validate it — failures in 1)-2) append
one `HH:MM:SS.mmm [CRITICAL] [config] …` line to
`<exe-dir>/logs/yyyy-mm-dd.log` (a `logs` directory is created next to the
executable when needed; same daily format as the main log) and exit 1; 3) logging; 4) database (below); 5)
REST/Oatpp components; 6) REST server; 7) serve.

Startup provisioning: SQLCIPHER boots check `database.path`: a path
elsewhere is treated as non-existent (a new database is created at the
configured path); missing file is created on open, existing files are
verified for schema + `admin` user (`AuthService::ensureSeeded`, which
creates `admin`/`Admin1234` when absent). With user-database encryption on,
the file is SQLCipher-encrypted via `database.key` (a missing key is
completed from defaults; an explicitly empty key opens the file plaintext
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

## Settings completeness

The application guarantees a complete configuration: a missing file is
created from per-type defaults; an existing file with absent parameters
keeps its values and gets the missing ones added from defaults — every
parameter follows this rule with no exceptions, including `database.key`
(a missing key is completed from defaults; a present key of any string
value is left untouched — an explicitly empty key means plaintext — and an
open failure from a wrong key is the operator's to fix). The completed names
are logged (`group.leaf` list) and
the file is rewritten. Wrong data types and invalid values are never
auto-fixed — loading aborts with a critical error for the operator to
correct.

`logging.path` was renamed from `logging.filePath`: files still using the
old name fail with a migration error (rename it or delete the file to
regenerate).

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

If the config file does not exist the server creates it from
`AppSettings::defaults()` (marked `(created)`) and boots with those values,
which is also what tests use.

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
