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
  "database": {"path": "data/app.db", "key": "dev-sqlcipher-key-change-me"},
  "jwt": {"secret": "change-me-in-production", "issuer": "oatpp-backend", "ttlSeconds": 3600},
  "logging": {"level": "INFO", "filePath": "logs"},
  "swagger": {"resPath": "/usr/local/include/oatpp-1.3.0/bin/oatpp-swagger/res"}
}
```

Leaf mapping: `host`/`port`, `database.path` (`databasePath`),
`database.key` (`databaseKey`, SQLCipher only), `jwt.secret`/`issuer`/
`ttlSeconds`, `logging.level`/`filePath`, `swagger.resPath`
(`swaggerResPath`, Swagger UI assets dir; default is the oatpp-swagger
library's own res dir via `OATPP_SWAGGER_RES_PATH`, only used when built with
`APP_WITH_SWAGGER`). The `swagger` section is only written to generated
`app.conf` files when built with Swagger enabled (it stays in the checked-in
samples either way); a `swagger` section present while disabled is loaded but
ignored. `validate()` returns `""` when valid, otherwise a
message used in exceptions. Pre-grouping flat files
(`host`, `databasePath`, … at the top level) are rejected with a migration
error instead of silently loading defaults.

`database.key` rule: SQLCipher builds require it explicitly present — a file
without it fails to load (no silent fallback to the built-in default, which
could leave a deployment accidentally unencrypted). SQLite builds ignore it
when present and never write it.

## File logging

`logFilePath` is a **directory**, not a file — just set the path:

```json
{ "logging": {"filePath": "logs"} }
```

- The active file is `<dir>/yyyy-mm-dd.log` (local date), resolved on every
  write, so a new file starts automatically at midnight.
- Every line starts with `HH:MM:SS.milliseconds`, then level, tag, message:
  `14:03:22.041 [INFO] [auth] Login succeeded for user 'admin'`.
  Console/stderr output uses the same line format.
- Empty `logFilePath` (`""`) disables file logging (console only).
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

A write failure only logs a warning; startup continues. The file is
runtime-generated, so it is git-ignored.
