# SQLCipher: the file database backend

The project's file database is **SQLCipher** — never standard SQLite. Every
file backend (`SqlcipherDatabase`, `SqlcipherDocument`) is compiled against
and linked with the SQLCipher library, and the standard SQLite library is
never searched, linked, or compiled in any configuration. This document
records the taxonomy, the key semantics, exactly how the prebuilt
`oatpp-sqlite` adapter is pointed at SQLCipher instead of `sqlite3`, and how
to verify a build.

## Taxonomy

| Concept | Name |
| ------- | ---- |
| CMake option (file support) | `APP_ENABLE_SQLCIPHER` (`APP_WITH_SQLCIPHER`) |
| CMake option (user database) | `APP_USERS_DATABASE_TYPE=SQLCIPHER` (`APP_DATABASE_SQLCIPHER`) |
| Backend class | `SqlcipherDatabase` (`backendName() == "SQLCIPHER"`) |
| Inspector class | `SqlcipherDocument` |
| Dependency | SQLCipher via `pkg-config` (`sqlcipher.pc`, `PkgConfig::SQLCIPHER`) |
| Encryption flag | `APP_ENCRYPTION_USER_DATABASE` (`APP_USER_DATABASE_ENCRYPTED`) |

The inspector functions use the backend name consistently: `openSqlcipherDatabase`, `listSqlcipherTables`, `sqlcipherTableSchema`, `dumpSqlcipherTable`, and `dumpSqlcipherDatabase`.

The removed `SQLITE` value of `APP_USERS_DATABASE_TYPE` fails configure with a
migration message. The old `APP_ENABLE_SQLITE` option no longer exists: a
stale `-DAPP_ENABLE_SQLITE=ON` in an existing build directory is simply
ignored, so pass `-DAPP_ENABLE_SQLCIPHER=ON` (or reconfigure from a fresh
build directory) if a cached value ever mattered to you.

## The key is optional

`SqlcipherDatabase(path, key)` takes the key with an empty default and the
behavior is:

- **No key** (empty): SQLCipher opens the file exactly like standard SQLite —
  plaintext, `sqlite3_key()` is never called.
- **With a key**: every connection is keyed before any other statement, the
  key is held in memory only (never logged, no accessor), and it is validated
  at `open()` with a real `sqlite_master` read — a wrong key (or an encrypted
  file behind a configured key) fails `open()` with
  `errors::database::ConnectionFailed` instead of failing at the first query.

Runtime configuration (`database.key`, `AppSettings::databaseKey`):

- absent -> completed from defaults like every other parameter (the built-in
  default encrypts fresh files);
- present -> used as-is, including an explicitly **empty** key, which means
  plaintext and logs a startup warning;
- builds without `APP_ENCRYPTION_USER_DATABASE` have no key concept at all
  (field and overloads compiled out; `database.key` is not written to their
  config files).

So `APP_ENCRYPTION_USER_DATABASE=ON` only decides whether the key machinery
exists in the build; the key itself is optional. PostgreSQL is different: its
`database.key` is the scram password and must be non-empty (see [`docs/postgresql.md`](postgresql.md)).

## Why the code still says `sqlite3`

SQLCipher is a fork of SQLite and keeps the SQLite C API. These are library
names fixed by SQLCipher and the oatpp adapter — not project taxonomy, and
they must not be renamed:

- `#include <sqlite3.h>` — SQLCipher ships its own `sqlite3.h`; with
  `pkg_check_modules(SQLCIPHER)`, `-I/usr/include/sqlcipher` comes first on
  the command line, so the include resolves to SQLCipher's header;
- `sqlite3_*` functions (`sqlite3_open`, `sqlite3_key`, ...) — SQLCipher's
  exported C API;
- `SQLITE_HAS_CODEC` — SQLCipher's build macro that makes its `sqlite3.h`
  declare `sqlite3_key()` / `sqlite3_rekey()`;
- `sqlite_master` — the catalog table name inside the file format;
- `oatpp-sqlite` — the adapter package/namespace (`oatpp::sqlite`).

`sqlite3_key()` does not exist in standard SQLite at all, so a build that
accidentally linked the standard library cannot even link.

## How `oatpp-sqlite` is wired to SQLCipher (no library patch)

The installed adapter never hardcodes a sqlite library. Its exported target
(`/usr/local/lib/cmake/oatpp-sqlite-1.3.0/oatpp-sqliteTargets.cmake`) carries
the abstract target name in its interface:

```cmake
set_target_properties(oatpp::oatpp-sqlite PROPERTIES
  INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:oatpp::oatpp>;$<LINK_ONLY:oatpp::oatpp-test>;SQLite::SQLite3"
  ...)
```

The package config does not resolve `SQLite::SQLite3` itself (no
`find_dependency`), so **the consumer defines it**. `cmake/dependencies.cmake`
does exactly that:

```cmake
pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher)
find_package(oatpp-sqlite 1.3.0 EXACT REQUIRED)

if(NOT TARGET SQLite::SQLite3)
    add_library(SQLite::SQLite3 INTERFACE IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
        INTERFACE_LINK_LIBRARIES PkgConfig::SQLCIPHER
        INTERFACE_COMPILE_DEFINITIONS "SQLITE_HAS_CODEC=1")
endif()
```

Consequences:

- the adapter's compiled object code only references `sqlite3_*` symbols
  (declared by whichever `sqlite3.h` it saw at build time); they resolve at
  final link against libsqlcipher;
- `PkgConfig::SQLCIPHER` also supplies the SQLCipher include directory to our
  translation units, and `SQLITE_HAS_CODEC=1` travels through
  `oatpp::oatpp-sqlite` -> `SQLite::SQLite3` to every consumer that calls
  `sqlite3_key()`;
- `find_package(SQLite3)` is **never** called in this project — CMake's stock
  `FindSQLite3` would define `SQLite::SQLite3` from the standard library, and
  the `if(NOT TARGET ...)` guard would then keep the wrong library.

For a completely fresh machine: install SQLCipher (dev files + `sqlcipher.pc`,
e.g. distro `libsqlcipher-dev`, or a source build with
`--with-crypto-lib=openssl --enable-tempstore=yes`), install `oatpp-sqlite`
as-is, and configure this project — the two steps above do the rest.

### Rebuilding `oatpp-sqlite` from source (only if you must)

The stock source (`external/libs/oatpp-sqlite-1.3.0-latest.zip`) calls
`find_package(SQLite3 REQUIRED)` and links `SQLite::SQLite3`. Two unpatched
options:

1. Put a `FindSQLite3.cmake` earlier in `CMAKE_MODULE_PATH` that defines
   `SQLite::SQLite3` from SQLCipher (pkg-config) and reports
   `SQLite3_FOUND`/`SQLite3_INCLUDE_DIRS`/`SQLite3_LIBRARIES` — the stock
   build then compiles and links against SQLCipher.
2. Configure the adapter with `OATPP_SQLITE_AMALGAMATION=ON` after replacing
   its bundled `sqlite/sqlite3.c` / `sqlite3.h` with SQLCipher's amalgamation
   (produced by a SQLCipher source build): the codec is compiled into the
   adapter's `sqlite` static library. Heavier, and it forks the extension
   tree.

Patching `src/CMakeLists.txt` to use pkg-config directly also works but
creates a maintained fork — not needed with the shim above.

## Verify a build really uses SQLCipher

```bash
ldd build/debug/oatpp-backend | grep -iE "sql|cipher"   # no shared sqlite/cipher: SQLCipher links statically (.a preferred)
nm -C build/debug/oatpp-backend | grep sqlite3_key      # present (T sqlite3_key): it is SQLCipher's codec
pkg-config --cflags --libs sqlcipher                    # -I/usr/include/sqlcipher -lsqlcipher
```

Tests that pin the behavior: `database: empty key opens plaintext`,
`database: wrong SQLCipher key fails at open`, `database: backend name`
(`SQLCIPHER`). No-file builds (`APP_ENABLE_SQLCIPHER=OFF`) compile zero file
symbols and link no SQLCipher (see `docs/cmake.md` leak checks).

## Sharp edges

- Include resolution decides which `sqlite3.h` the compiler sees: without the
  pkg-config include directory, a stock header on the default path would be
  used, `sqlite3_key` would not be declared, and the build fails loudly
  (compile error) — rather than silently losing the codec.
- `SQLITE_HAS_CODEC=1` must reach every translation unit that calls
  `sqlite3_key`; it comes through the `SQLite::SQLite3` shim, so keep the
  shim (or add the definition directly) if the adapter's exported interface
  ever changes.
- `sqlite_master` in SQL text and `sqlite3_*` identifiers in C++ are
  SQLCipher/SQLite API names — leave them; only the project taxonomy is
  SQLCipher.
