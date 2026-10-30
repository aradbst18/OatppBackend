# CMake

C++17 is enforced (`CMAKE_CXX_STANDARD 17 ... REQUIRED`, extensions off).
Target-based only: `target_link_libraries`, `target_include_directories`,
`target_compile_definitions` with scope. No global `include_directories` or
`add_definitions`.

## Options

| Option               | Values                    | Default  | Effect                                                        |
| -------------------- | ------------------------- | -------- | ------------------------------------------------------------- |
| `APP_ENABLE_ZLIB`    | `ON/OFF`                  | `OFF`    | `find_package(oatpp-zlib)`, `APP_WITH_ZLIB`, gzip handlers (see `docs/zlib.md`) |
| `APP_ENABLE_SWAGGER` | `ON/OFF`                  | `OFF`    | `find_package(oatpp-swagger)`, `APP_WITH_SWAGGER`, UI routes (see `docs/swagger.md`) |
| `APP_ENABLE_POSTGRESQL` | `ON/OFF`               | `OFF`    | `find_package(oatpp-postgresql)`, `APP_WITH_POSTGRESQL`, pg impl |
| `APP_ENABLE_SQLCIPHER` | `ON/OFF`               | `ON`     | SQLCipher-backed file support over oatpp-sqlite, `APP_WITH_SQLCIPHER` (OFF = no file lib) |
| `APP_USERS_DATABASE_TYPE` | `SQLCIPHER`/`POSTGRESQL` | `SQLCIPHER` | user db impl, `APP_DATABASE_*` |
| `APP_ENCRYPTION_USER_DATABASE` | `ON/OFF` | `OFF` | user db encryption, `APP_USER_DATABASE_ENCRYPTED` (SQLCIPHER only; PG+ON is a configure error, see `docs/roadmap/postgresql_encryption.md`) |
| `APP_PASSWORD_HASH_ALGO` | `SHA256`/`SHA384`/`SHA512` | `SHA256` | password hash, `APP_PASSWORD_HASH_SHA*` (OpenSSL EVP)     |
| `APP_CONFIG_FORMAT`  | `JSON`/`TOML`/`XML`/`YAML` | `JSON`  | active store, `APP_CONFIG_*`                                  |
| `APP_VERSION`          | `x.y.z`               | `0.0.1`  | app version, sets `PROJECT_VERSION`                           |
| `APP_RELEASE_DATE`     | `yyyy-mm-dd` (empty = configure date) | auto | release/build date stamp                        |
| `APP_VERSION_FILE_FORMAT` | `JSON`/`TOML`/`XML`/`YAML` | `JSON` | version file syntax, `APP_VERSION_FILE_*`                |
| `APP_ENABLE_JSON_PARSER` | `ON/OFF`              | `ON`     | `find_package(jsoncpp)`, `APP_WITH_JSON_PARSER`               |
| `APP_ENABLE_TOML_PARSER` | `ON/OFF`              | `OFF`    | `find_package(toml11)`, `APP_WITH_TOML_PARSER`                |
| `APP_ENABLE_XML_PARSER`  | `ON/OFF`              | `OFF`    | `find_package(pugixml)`, `APP_WITH_XML_PARSER`                |
| `APP_ENABLE_YAML_PARSER` | `ON/OFF`              | `OFF`    | `find_package(yaml-cpp)`, `APP_WITH_YAML_PARSER`              |
| `APP_BUILD_TESTS`      | `ON/OFF`              | `ON`     | build `tests/` (`app_tests`); `OFF` skips CTest entirely     |
| `ENABLE_SANITIZERS`    | `ON/OFF`              | `OFF`    | master sanitizer switch; the build type selects the set (see below) |
| `VERBOSE_BUILD_SUMMARY`| `ON/OFF`              | `OFF`    | per-dependency link-type/path/version table in the summary  |
| `CMAKE_BUILD_TYPE`   | `Debug/Release/...`       | —        | standard optimization/debug flags                             |

Invalid `APP_USERS_DATABASE_TYPE`/`APP_PASSWORD_HASH_ALGO`/`APP_CONFIG_FORMAT` values are a configure-time
`FATAL_ERROR` (comparison is case-insensitive: `"JSOn"`, `"SqlCipher"`, … are
normalized to uppercase first), as is selecting a format whose parser is disabled (e.g.
`APP_CONFIG_FORMAT=JSON` with `APP_ENABLE_JSON_PARSER=OFF`). Disabled features
are never searched (`find_package` is inside `if()`), never compiled
(`target_sources` conditionals in the root `CMakeLists.txt`), and never
linked. Boolean flags accept `ON`/`OFF` (any case), `TRUE`/`FALSE`,
`YES`/`NO`, `Y`/`N` and `1`/`0` (`false = OFF = 0`, `true = ON = 1`) and are
canonicalized to `ON`/`OFF` in the cache; anything else fails configure. The parser flags live in the `base` preset's `cacheVariables` (rarely
changed) and are inherited by every configuration.

## Static linking

`.a` sorts before `.so` in `CMAKE_FIND_LIBRARY_SUFFIXES`, so every dependency
links statically wherever a static archive exists (oatpp modules, jsoncpp,
pugixml, yaml-cpp, SQLCipher, zlib; toml11/jwt-cpp are header-only). One
library stays shared: OpenSSL (Ubuntu's static `libcrypto.a` needs the
uninstalled `libjitterentropy`). PostgreSQL builds also link the shared
`libpq` the `oatpp-postgresql` package exports, and the static SQLCipher
archive additionally needs `zstd` at runtime. Only system runtimes (`libc`,
`libstdc++`, …) plus those and `zstd` remain dynamic — confirm with `ldd`.

## Verbose build summary

`VERBOSE_BUILD_SUMMARY` (default `OFF`, in `base` `cacheVariables`) adds a
per-dependency table to the configure summary: CMake target, static/shared/
interface link type, resolved path, and version:

```bash
cmake --preset debug -DVERBOSE_BUILD_SUMMARY=ON
```

## Modules (`cmake/`)

- `options.cmake` — flags, validation, `apply_app_definitions()`.
- `dependencies.cmake` — all `find_package` calls + `link_app_dependencies()`.
  File support discovers SQLCipher via `pkg_check_modules` (`sqlcipher.pc`,
  `PkgConfig::SQLCIPHER`) plus `oatpp-sqlite`; the adapter's exported
  `SQLite::SQLite3` reference is satisfied by SQLCipher (an INTERFACE shim
  that also defines `SQLITE_HAS_CODEC=1`, so SQLCipher's `sqlite3.h` declares
  `sqlite3_key()` — the adapter itself is not rebuilt), so the standard SQLite
  library is never searched or linked. PostgreSQL support discovers
  `oatpp-postgresql` (libpq underneath).
- `compiler.cmake` — `apply_app_warnings()` (strict set, our targets only),
  `apply_app_build_type_flags()` (per-config optimization/debug/size tuning for
  `Debug`, `RelWithDebInfo`, `Release`, `MinSizeRel` plus `DebugTSan`/
  `DebugMSan`/`RelWithDebInfoTSan`, which mirror their base type) and
  `apply_app_hardening()` (production hardening for `Release`/`MinSizeRel` on
  Linux GNU/Clang: `_FORTIFY_SOURCE=3`, `-fstack-protector-strong`, PIE,
  RELRO+immediate binding; RelWithDebInfo excluded so its sanitizer variants
  never meet FORTIFY).
- `sanitizers.cmake` — single sanitizer switch `ENABLE_SANITIZERS` plus the
  single source of truth (`app_effective_sanitizers()`,
  `app_apply_sanitizers()`): `Debug`/`RelWithDebInfo` → ASan+UBSan,
  `DebugTSan`/`RelWithDebInfoTSan` → TSan only, `DebugMSan` → MSan (Clang
  only), `Release`/`MinSizeRel` → never (policy). Custom types get explicit
  base flags here because CMake supplies none.
- `build_summary.cmake` — configure-time feature echo, incl. the *effective*
  `Sanitizers:` set (never just the raw switch; `None (disabled by policy …)`
  for `Release`/`MinSizeRel`).

## Presets

`CMakePresets.json` holds `base` (hidden: generator, output dirs, and every
`APP_*` default — everything except `CMAKE_BUILD_TYPE`; it contains no
sanitizer toggle), plus one leaf preset per configuration, each with its own
`build/<preset-name>/` dir (no cache contamination) and its own explicit
`ENABLE_SANITIZERS` value: `debug` and `relwithdebinfo` are the standard,
sanitizer-free configurations (`false`); `debug-asan-ubsan`, `debug-tsan`,
`debug-msan` (Clang only), `relwithdebinfo-asan-ubsan`,
`relwithdebinfo-tsan` and `relwithdebinfo-msan` (Clang only) set `true`;
`release` and `minsizerel` set `false`. `ENABLE_SANITIZERS` is the single sanitizer toggle
(no per-sanitizer options): `-DENABLE_SANITIZERS=OFF` disables instrumentation
for every configuration, and `Release`/`MinSizeRel` stay sanitizer-free even
with `ON` (enforced in `cmake/sanitizers.cmake`, not just by preset values).
Any value is overridden per configure with `-D` (command-line `-D` wins over
preset `cacheVariables`):

```bash
cmake --preset debug && cmake --build --preset debug \
  && ctest --preset debug
cmake --preset debug-asan-ubsan && cmake --build --preset debug-asan-ubsan
cmake --preset debug -DAPP_CONFIG_FORMAT=YAML -DAPP_ENCRYPTION_USER_DATABASE=ON
cmake --preset debug-tsan && cmake --build --preset debug-tsan
cmake --preset relwithdebinfo-msan   # Clang only; fails configure on GCC
cmake --preset debug-asan-ubsan -DENABLE_SANITIZERS=OFF   # any preset, sanitizers off
bash scripts/check_sanitizers.sh               # configure-only matrix check
```

`CMakeUserPresets.json` (local overlay, Ninja + `-ninja` names, `jobs: 6`)
mirrors all ten configurations for day-to-day development — `debug-ninja` is
the `scripts/build.sh` default, `debug-asan-ubsan-ninja` the instrumented
counterpart:

```bash
cmake --preset debug-ninja && cmake --build --preset debug-ninja
```

Each preset builds into `build/<preset-name>/`, which is git-ignored.

## Full test matrix (`scripts/test_matrix.sh`)

`scripts/test_matrix.sh` accepts any checked-in configure preset and overrides
individual options into isolated `build/matrix/<case>/` directories. Case names
include the password hash and version format to prevent cache collisions.
Release and MinSizeRel are build-only; other presets run CTest. Missing tests
are errors. MSan presets require a suitable Clang environment.

```bash
scripts/test_matrix.sh debug        # 96 main cases plus parser/validation checks
MATRIX_HASHES=SHA512 MATRIX_FORMATS=TOML MATRIX_FEATURES=minimal \
  scripts/test_matrix.sh relwithdebinfo
MATRIX_CONFIGURE_ONLY=1 MATRIX_VERSION_FORMATS="JSON TOML XML YAML" \
  scripts/test_matrix.sh debug
MATRIX_PG=1 scripts/test_matrix.sh debug  # require PostgreSQL cases; no auto-skip
```

The main matrix varies backend, SQLCipher encryption, config format, feature
bundle, password hash, and version-file format. Subset variables documented in
the script bound the cross-product. Parser-exclusion cases and invalid-option
checks are also included. PostgreSQL variants run automatically only when a
server is reachable unless `MATRIX_PG=1` forces them.

### GitHub Actions coverage

- `ci.yml`: pushes/PRs to main/master and manual runs. Debug, ASan/UBSan,
  Release, MinSizeRel, and two targeted backend/parser/hash variants.
- The Debug job also runs the matrix runner's orchestration tests, sanitizer
  configuration checks, and a configure-only cross-product of both backends,
  three hashes, four config formats, and four version formats, plus negative
  checks. PostgreSQL is started explicitly.
- `extended.yml`: Mondays at 03:17 UTC or manually. Adds RelWithDebInfo,
  RelWithDebInfo ASan/UBSan, both TSan presets, and Clang configuration checks
  for both MSan presets. MSan runtime tests remain deferred until the C++
  standard library and dependencies can be instrumented too.
- The extended feature matrix runs all 16 config/version format pairs. Across
  these cases it rotates plaintext/encrypted SQLCipher and PostgreSQL user
  databases, SHA256/384/512, Swagger/Zlib, and parser exclusions. This is
  selected interaction coverage, not every possible feature cross-product.
- `.github/actions/setup/action.yml` shares dependency setup and caches the
  complete installation, keyed by archive/setup/pin hashes, OS/architecture,
  compiler, CMake, and system-library versions. There are no fallback restore
  keys. Dependency versions in `cmake/dependencies.cmake` remain authoritative.

Workflows use Ubuntu 24.04, read-only repository permissions, bounded
parallelism, per-job timeouts, and cancellation of superseded runs. No build
artifacts or releases are published. Dependabot proposes GitHub Actions updates;
it does not update bundled library ZIPs or project version pins.

## Staged output (`out/`)

Every preset sets `CMAKE_INSTALL_PREFIX` to `build/<preset-name>/out`, so
`cmake --install` stages a runnable tree per configuration:

```bash
cmake --install build/release
./build/release/out/bin/oatpp-backend --config configs/app.conf
```

Layout: `out/bin/oatpp-backend` plus, only when `APP_ENABLE_SWAGGER=ON`,
`out/share/swagger-res/` (Swagger UI assets, resolved from the `swagger.resPath`
setting with exe-relative `../share/swagger-res` and `OATPP_SWAGGER_RES_PATH` env
fallbacks; dev runs fall back to the configure-time source dir). No config or version files are
installed: the app creates `configs/app.conf` from defaults on first startup
when missing and rewrites `configs/version.conf` on every startup, both
relative to the working directory — pass `--config <path>` to place them
elsewhere. System shared libraries (OpenSSL, `libzstd`, `libpq` in
PostgreSQL builds, libstdc++) are expected on the target machine and are
not bundled.

## Targets

- `app_core` (static): everything except `main()`.
- `oatpp-backend` (executable): `src/main.cpp` + `app_core`.
- `app_tests` (CTest): `tests/` + `app_core`, registered via `add_test`.

## Workflows

Every configure preset has a matching workflow preset of the same name:
test-carrying presets chain configure+build+test, `release`/`minsizerel`
(which register no tests) chain configure+build only:

```bash
cmake --workflow --preset debug
cmake --workflow --preset debug-tsan
cmake --workflow --preset relwithdebinfo
cmake --workflow --preset minsizerel   # configure+build, no test step
cmake --workflow --preset debug-ninja   # Ninja overlay
cmake --workflow --list-presets
```

## Examples

```bash
# Default: SQLCipher + JSON + Swagger + Zlib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Minimal: SQLCipher + TOML, no Swagger/Zlib
cmake -S . -B build-min -DAPP_ENABLE_SWAGGER=OFF -DAPP_ENABLE_ZLIB=OFF \
  -DAPP_USERS_DATABASE_TYPE=SQLCIPHER -DAPP_CONFIG_FORMAT=TOML
cmake --build build-min --parallel

# Encrypted: SQLCIPHER + encryption flag + JSON
cmake -S . -B build-cipher -DAPP_USERS_DATABASE_TYPE=SQLCIPHER \
  -DAPP_ENCRYPTION_USER_DATABASE=ON -DAPP_CONFIG_FORMAT=JSON
cmake --build build-cipher --parallel
```

## Build matrix (verified via `scripts/test_matrix.sh`)

All 35 cases pass: 16 file-backend combos (`SQLCIPHER` plain/encrypted ×
JSON/TOML/XML/YAML × `full`/`minimal`), 4 parser-exclusion builds, the
version-file case, 3 PostgreSQL combos, and 11 fail-fast negatives.
Representative rows:

| Backend       | Format | Swagger | Zlib | Result  |
| ------------- | ------ | ------- | ---- | ------- |
| SQLCIPHER     | JSON   | ON      | ON   | pass    |
| SQLCIPHER     | YAML   | ON      | ON   | pass    |
| SQLCIPHER     | XML    | OFF     | OFF  | pass    |
| SQLCIPHER     | TOML   | OFF     | OFF  | pass    |
| SQLCIPHER+enc | JSON   | ON      | ON   | pass    |
| SQLCIPHER+enc | YAML   | ON      | ON   | pass    |
| SQLCIPHER+enc | TOML   | OFF     | OFF  | pass    |

Fail-fast negatives verified in the same run: `BOGUS` user database, the
removed `SQLITE` value (migration message), `BOGUS` boolean,
`POSTGRESQL` without `APP_ENABLE_POSTGRESQL`, `SQLCIPHER` without
`APP_ENABLE_SQLCIPHER`, PostgreSQL + encryption, and each config format with
its parser disabled.

Leak checks: a no-file build (`APP_ENABLE_SQLCIPHER=OFF`) contains zero
file-database symbols and links no SQLCipher (nor any other sqlite
implementation); `SqlcipherDatabase.hpp` `#error`s when included without
`APP_WITH_SQLCIPHER`. The standard SQLite library is never linked in any
configuration (file support is always SQLCipher-backed): the linked codec is
SQLCipher — the binary calls `sqlite3_key` (a symbol standard SQLite does not
provide, so a plain-SQLite link cannot even succeed) and validates the key
against `sqlite_master` at open. Swagger
OFF serves 404 on `/swagger/ui` while APIs keep working (verified with curl).

## Build configurations

| Preset | `CMAKE_BUILD_TYPE` | Optimize | Debug info | Sanitizers (`ON`) | Production-ready |
|---|---|---|---|---|---|
| `debug` | `Debug` | `-O0` | `-g3`, frame pointers, `DEBUG` | None (switch off) | No |
| `debug-asan-ubsan` | `Debug` | `-O0` | `-g3`, frame pointers, `DEBUG` | ASan+UBSan | No (diagnostics) |
| `debug-tsan` | `DebugTSan` | `-O0` | `-g3`, frame pointers, `DEBUG` | TSan only | No |
| `debug-msan` | `DebugMSan` | `-O0` | `-g3`, frame pointers, `DEBUG` | MSan only (Clang) | No |
| `relwithdebinfo` | `RelWithDebInfo` | `-O2` | `-g`, kept (never stripped) | None (switch off) | Yes, if debuggable prod needed |
| `relwithdebinfo-asan-ubsan` | `RelWithDebInfo` | `-O2` | `-g`, kept (never stripped) | ASan+UBSan | No (diagnostics) |
| `relwithdebinfo-tsan` | `RelWithDebInfoTSan` | `-O2` | `-g`, kept | TSan only | No (diagnostics) |
| `relwithdebinfo-msan` | `RelWithDebInfoMSan` | `-O2` | `-g`, kept | MSan only (Clang) | No |
| `release` | `Release` | `-O3`+LTO | none (`-s` strips) | Never | Yes (default deploy) |
| `minsizerel` | `MinSizeRel` | `-Os`+LTO | none (`-s` strips) | Never | Yes (smallest) |

`Release`/`MinSizeRel` additionally harden on Linux GNU/Clang
(`_FORTIFY_SOURCE=3`, `-fstack-protector-strong`, PIE, RELRO+immediate
binding). Measured sizes, SQLCipher+TOML build, `release`/`minsizerel` without
Swagger (preset defaults): `minsizerel` 3.7M < `release` 4.1M < `debug` 40.7M <
`debug-tsan` 41.2M < `relwithdebinfo` 43.7M < `relwithdebinfo-tsan` 45.3M <
`debug-asan-ubsan` 74.7M < `relwithdebinfo-asan-ubsan` 85.2M.

Note the ordering: `relwithdebinfo-asan-ubsan` is *larger* than
`debug-asan-ubsan`, and that is correct — not a misconfiguration. Section
analysis (`size -A`) shows why: `relwithdebinfo-asan-ubsan` carries 22.0M of
`.debug_info` vs 12.4M for `debug-asan-ubsan`, plus a 4.0M `.debug_loclists`
section, while `.text` grows 8.4M → 9.9M. `-O2` inlining duplicates
ASan-instrumented code at every inline site and each copy gets full debug
records; the static archives show the identical pattern (`libapp_core.a`
159.9M vs 178.9M), proving a compile-stage effect. The sanitizer-free presets
show the same ladder (`debug` 40.7M vs `relwithdebinfo` 43.7M). So with debug
info retained, `-O2` binaries routinely outweigh `-O0` ones; stripping is what
makes `Release`/`MinSizeRel` small.

## Sanitizer compatibility (verified)

| Toolchain | ASan+UBSan | TSan | MSan |
|---|---|---|---|
| GCC 15 (Linux x86_64) | ✅ built+tested | ✅ built+tested | ❌ configure error (Clang-only) |
| Clang (Linux) | ✅ expected | ✅ expected | ⚠️ needs fully instrumented stdlib/deps (`debug-msan` and `relwithdebinfo-msan`) |
| MSVC | ASan-only degrade | ❌ fatal | ❌ fatal |

Rules enforced in `cmake/sanitizers.cmake`: TSan is never combined with ASan;
a requested-but-unsupported sanitizer fails configure instead of silently
dropping flags; `Release`/`MinSizeRel` ignore the switch by policy. LSan is
not listed separately (integrated into ASan on Linux). TSan binaries exit
nonzero on races (exit code 66), so `ctest` reports them as failures; run
TSan test presets when hunting races.

## Deployment workflow

1. Deploy `Release` (default) or `MinSizeRel` (smallest). Both are PIE,
   RELRO-hardened, stripped, and link no sanitizer runtimes (verify with
   `ldd build/release/oatpp-backend`).
2. On a production issue, reproduce with `relwithdebinfo` (same `-O2`
   optimization, sanitizer-free, symbols retained, deployable where deps
   exist); reach for `relwithdebinfo-asan-ubsan` when sanitizer coverage at
   `-O2` is needed.
3. If that is insufficient, use `debug` (unoptimized, `-g3`, `DEBUG`
   assertions) under `gdb` (`.vscode/launch.json` has `debug` and
   `relwithdebinfo` server configs, plus their `-asan-ubsan` variants).
4. For suspected memory corruption/UB/races/uninitialized memory, use
   `debug-asan-ubsan`, `debug-tsan`, or `debug-msan` (Clang) respectively.

Cross-machine limits: the binary needs its dynamic deps on the target
(OpenSSL, `libzstd`, `libpq` in PostgreSQL builds, `libstdc++`, matching
glibc ABI — build glibc 2.43 here; older targets need a rebuild). Keep the exact shipped binary plus
its `relwithdebinfo` counterpart (same commit/flags) for symbolization;
`MinSizeRel`/`Release` carry no usable symbols (`-s`). `ENABLE_SANITIZERS`
binaries additionally need the matching sanitizer runtime and must never be
deployed.
