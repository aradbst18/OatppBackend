#!/usr/bin/env bash

# Backend x encryption x format x feature x hash x version-format matrix.
#
# Uses the selected checked-in preset, with explicit -D overrides for each case.
# Subset environment variables keep CI coverage bounded.
# Build trees land in build/matrix/<combo>/, never clobbering build/debug.
#
# Usage: scripts/test_matrix.sh [preset]   (default: debug)
#
# Env (space-separated subsets; defaults = everything):
#   MATRIX_BACKENDS="SQLCIPHER"   # file user-database encryption variants under test (ENCRYPTION ON/OFF x formats/features)
#   MATRIX_FORMATS="JSON TOML XML YAML"
#   MATRIX_FEATURES="full swagger zlib minimal"
#   MATRIX_PARSERS=1   # also run parser-exclusion builds + fail-fast negatives
#   MATRIX_HASHES="SHA256 SHA384 SHA512"
#   MATRIX_CONFIGURE_ONLY=0  # 1 validates configurations without building
#   MATRIX_VERSION_FORMATS="JSON"  # version-file formats to exercise
#                                  # (same four options / parser rules as
#                                  # APP_CONFIG_FORMAT; space-separated)
#   MATRIX_PG="auto"   # PostgreSQL users-database combos: "auto" runs them only
#                      # when a server answers at $PGHOST:$PGPORT (else skipped
#                      # with a note), "1" runs unconditionally, "0" skips.
#                      # Reachable-server tests use PGHOST/PGPORT/PGDATABASE/
#                      # PGUSER/PGPASSWORD (see docs/testing.md).
#   MATRIX_TESTS=1     # 0 skips building/running tests (build-only matrix)
#   JOBS=$(nproc)      # build parallelism
#   GENERATOR=""       # e.g. GENERATOR=Ninja to pass -G explicitly

set -u
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-debug}"

case "$BUILD_TYPE" in
    debug|debug-asan-ubsan|debug-tsan|debug-msan|relwithdebinfo|relwithdebinfo-asan-ubsan|relwithdebinfo-tsan|relwithdebinfo-msan|release|minsizerel) ;;
    *) echo "Unknown preset: $BUILD_TYPE" >&2; exit 2 ;;
esac

DB_BACKENDS=${MATRIX_BACKENDS:-"SQLCIPHER"}
PG_MODE=${MATRIX_PG:-"auto"}
DFG_FORMATS=${MATRIX_FORMATS:-"JSON TOML XML YAML"}
FEATURES=${MATRIX_FEATURES:-"full swagger zlib minimal"}
PARSERS=${MATRIX_PARSERS:-1}
VFMTS=${MATRIX_VERSION_FORMATS:-"JSON"}
HASHES=${MATRIX_HASHES:-"SHA256 SHA384 SHA512"}
CONFIGURE_ONLY=${MATRIX_CONFIGURE_ONLY:-0}
TESTS=${MATRIX_TESTS:-1}
case "$BUILD_TYPE" in release|minsizerel) TESTS=0 ;; esac
JOBS=${JOBS:-$(nproc)}
GEN_ARGS=()
[[ -n "${GENERATOR:-}" ]] && GEN_ARGS=(-G "$GENERATOR")

lower() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

PASS=0
FAIL=0
FAILED_CASES=()

run_case() { # <name> <cmake-args...>
    local name="$1"
    shift
    local dir="build/matrix/${name}"
    local args=("$@")

    if [[ "$TESTS" == "0" ]]; then
        args+=(-DAPP_BUILD_TESTS=OFF)
    fi

    mkdir -p "build/matrix"
    echo "=== [matrix] ${name} ==="

    if ! cmake --preset "$BUILD_TYPE" -S . -B "$dir" "${GEN_ARGS[@]}" \
        "${args[@]}" >>"$dir.configure.log" 2>&1; then
        echo "  CONFIGURE FAILED (see $dir.configure.log)"
        tail -n 60 "$dir.configure.log"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (configure)")
        return 0
    fi

    if [[ "$CONFIGURE_ONLY" == "1" ]]; then
        echo "  PASS (configure only)"
        PASS=$((PASS + 1))
        return 0
    fi

    if ! cmake --build "$dir" -j"$JOBS" >>"$dir.build.log" 2>&1; then
        echo "  BUILD FAILED (see $dir.build.log)"
        tail -n 60 "$dir.build.log"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (build)")
        return 0
    fi

    if [[ "$TESTS" == "0" ]]; then
        echo "  PASS (build only, tests skipped)"
        PASS=$((PASS + 1))
        return 0
    fi

    if ! ctest --test-dir "$dir" --no-tests=error --output-on-failure >>"$dir.test.log" 2>&1; then
        echo "  TEST FAILED (see $dir.test.log)"
        tail -n 100 "$dir.test.log"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (test)")
        return 0
    fi

    echo "  PASS"
    PASS=$((PASS + 1))
}

expect_configure_failure() { # <name> <cmake-args...>
    local name="$1"
    shift
    local dir="build/matrix/${name}"
    mkdir -p "build/matrix"
    echo "=== [matrix] ${name} (must fail) ==="

    if cmake --preset "$BUILD_TYPE" -S . -B "$dir" "${GEN_ARGS[@]}" \
        "$@" >>"$dir.configure.log" 2>&1; then
        echo "  UNEXPECTEDLY CONFIGURED (should have failed)"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (should-fail)")
        return 0
    fi

    echo "  PASS (rejected as expected)"
    PASS=$((PASS + 1))
}

feature_flags() { # <feat> -> prints "-D... -D..."
    case "$1" in
        full)
            echo "-DAPP_ENABLE_ZLIB=ON -DAPP_ENABLE_SWAGGER=ON"
            ;;
        swagger)
            echo "-DAPP_ENABLE_ZLIB=OFF -DAPP_ENABLE_SWAGGER=ON"
            ;;
        zlib)
            echo "-DAPP_ENABLE_ZLIB=ON -DAPP_ENABLE_SWAGGER=OFF"
            ;;
        minimal)
            echo "-DAPP_ENABLE_ZLIB=OFF -DAPP_ENABLE_SWAGGER=OFF"
            ;;
        *)
            echo "unknown feature: $1" >&2
            exit 2
            ;;
    esac
}

# --- main matrix: every backend x encryption x format x feature, all parsers on ---
# (SQLCIPHER runs encrypted and plaintext; POSTGRESQL only runs unencrypted —
# PG encryption is not implemented, see docs/roadmap/postgresql_encryption.md.)

# shellcheck disable=SC2206
for be in $DB_BACKENDS; do
    case "$be" in
        POSTGRESQL)
            encs="OFF"
            ;;
        *)
            encs="ON OFF"
            ;;
    esac
    for enc in $encs; do
        for fmt in $DFG_FORMATS; do
            for feat in $FEATURES; do
                for hash in $HASHES; do
                    for vfmt in $VFMTS; do
                        # shellcheck disable=SC2086
                        run_case "${BUILD_TYPE}-$(lower "$be")-enc$(lower "$enc")-$(lower "$fmt")-$feat-$(lower "$hash")-ver$(lower "$vfmt")" \
                            -DAPP_USERS_DATABASE_TYPE="$be" -DAPP_ENCRYPTION_USER_DATABASE="$enc" \
                            -DAPP_ENABLE_SQLCIPHER=ON -DAPP_ENABLE_POSTGRESQL=ON \
                            -DAPP_PASSWORD_HASH_ALGO="$hash" -DAPP_CONFIG_FORMAT="$fmt" \
                            -DAPP_VERSION_FILE_FORMAT="$vfmt" $(feature_flags "$feat") \
                            -DAPP_ENABLE_JSON_PARSER=ON -DAPP_ENABLE_TOML_PARSER=ON \
                            -DAPP_ENABLE_XML_PARSER=ON -DAPP_ENABLE_YAML_PARSER=ON
                    done
                done
            done
        done
    done
done

if [[ "$PARSERS" == "1" ]]; then
    # --- parser exclusion: each format with ONLY its own parser, minimal ---
    # (the version file format follows the config format, so APP_VERSION_FILE_FORMAT
    # never requires a parser that this case has disabled)
    for fmt in $DFG_FORMATS; do
        # shellcheck disable=SC2086
        run_case "$(lower "$BUILD_TYPE")-excl-$(lower "$fmt")" \
            -DAPP_USERS_DATABASE_TYPE=SQLCIPHER -DAPP_ENABLE_SQLCIPHER=ON -DAPP_ENCRYPTION_USER_DATABASE=OFF -DAPP_CONFIG_FORMAT="$fmt" \
            -DAPP_VERSION_FILE_FORMAT="$fmt" \
            $(feature_flags minimal) \
            -DAPP_ENABLE_JSON_PARSER=$([[ "$fmt" == "JSON" ]] && echo ON || echo OFF) \
            -DAPP_ENABLE_XML_PARSER=$([[ "$fmt" == "XML" ]] && echo ON || echo OFF) \
            -DAPP_ENABLE_TOML_PARSER=$([[ "$fmt" == "TOML" ]] && echo ON || echo OFF) \
            -DAPP_ENABLE_YAML_PARSER=$([[ "$fmt" == "YAML" ]] && echo ON || echo OFF)
    done

    # --- fail-fast negatives: format whose parser is off must not configure ---
    for fmt in $DFG_FORMATS; do
        expect_configure_failure "$(lower "$BUILD_TYPE")-neg-$(lower "$fmt")" \
            -DAPP_CONFIG_FORMAT="$fmt" \
            -DAPP_ENABLE_JSON_PARSER=$([[ "$fmt" == "JSON" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_TOML_PARSER=$([[ "$fmt" == "TOML" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_XML_PARSER=$([[ "$fmt" == "XML" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_YAML_PARSER=$([[ "$fmt" == "YAML" ]] && echo OFF || echo ON)
    done

    # --- version file formats: same four options, same parser conditions ---
    # Build+test one combo per version format (config stays JSON, all parsers
    # on), then prove a disabled version parser is rejected. The rejection case
    # uses a *different* config format so only the version-file rule can fire.
    for vfmt in $VFMTS; do
        # shellcheck disable=SC2086
        run_case "$(lower "$BUILD_TYPE")-ver-$(lower "$vfmt")" \
            -DAPP_USERS_DATABASE_TYPE=SQLCIPHER -DAPP_ENCRYPTION_USER_DATABASE=OFF -DAPP_CONFIG_FORMAT=JSON \
            $(feature_flags minimal) \
            -DAPP_ENABLE_JSON_PARSER=ON -DAPP_ENABLE_TOML_PARSER=ON \
            -DAPP_ENABLE_XML_PARSER=ON -DAPP_ENABLE_YAML_PARSER=ON \
            -DAPP_VERSION_FILE_FORMAT="$vfmt"
    done

    for vfmt in $VFMTS; do
        case "$vfmt" in
            JSON)
                cfgfmt="TOML"
                ;;
            TOML)
                cfgfmt="XML"
                ;;
            XML)
                cfgfmt="YAML"
                ;;
            YAML)
                cfgfmt="JSON"
                ;;
            *)
                echo "unknown version format: $vfmt" >&2
                exit 2
                ;;
        esac
        expect_configure_failure "$(lower "$BUILD_TYPE")-verneg-$(lower "$vfmt")" \
            -DAPP_CONFIG_FORMAT="$cfgfmt" -DAPP_VERSION_FILE_FORMAT="$vfmt" \
            -DAPP_ENABLE_JSON_PARSER=$([[ "$vfmt" == "JSON" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_TOML_PARSER=$([[ "$vfmt" == "TOML" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_XML_PARSER=$([[ "$vfmt" == "XML" ]] && echo OFF || echo ON) \
            -DAPP_ENABLE_YAML_PARSER=$([[ "$vfmt" == "YAML" ]] && echo OFF || echo ON)
    done
fi

# --- value negatives: unknown enum values must not configure ---
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-bad-users" -DAPP_USERS_DATABASE_TYPE=BOGUS
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-removed-sqlite-value" -DAPP_USERS_DATABASE_TYPE=SQLITE
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-bad-bool" -DAPP_ENCRYPTION_USER_DATABASE=BOGUS
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-pg-without-support" -DAPP_USERS_DATABASE_TYPE=POSTGRESQL -DAPP_ENABLE_POSTGRESQL=OFF
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-sqlcipher-without-support" -DAPP_USERS_DATABASE_TYPE=SQLCIPHER -DAPP_ENABLE_SQLCIPHER=OFF
expect_configure_failure "$(lower "$BUILD_TYPE")-neg-pg-encryption" -DAPP_USERS_DATABASE_TYPE=POSTGRESQL -DAPP_ENABLE_POSTGRESQL=ON \
    -DAPP_ENCRYPTION_USER_DATABASE=ON

# --- PostgreSQL combos: need a live server ---
# Gated: "auto" probes with pg_isready, "1" runs unconditionally, anything else skips.
PG_RUN=0
if [[ "$PG_MODE" == "1" ]]; then
    PG_RUN=1
elif [[ "$PG_MODE" == "auto" ]] && command -v pg_isready >/dev/null 2>&1 &&
    pg_isready -q -h "${PGHOST:-127.0.0.1}" -p "${PGPORT:-5432}"; then
    PG_RUN=1
fi

if [[ "$PG_RUN" == "1" ]]; then
    # Users database on PostgreSQL, with and without file support alongside it.
    for file in ON OFF; do
        # shellcheck disable=SC2086
        run_case "$(lower "$BUILD_TYPE")-pg-users-sqlcipher-$(lower "$file")" \
            -DAPP_ENABLE_POSTGRESQL=ON -DAPP_USERS_DATABASE_TYPE=POSTGRESQL -DAPP_ENABLE_SQLCIPHER="$file" \
            -DAPP_ENCRYPTION_USER_DATABASE=OFF \
            -DAPP_CONFIG_FORMAT=TOML $(feature_flags minimal) \
            -DAPP_ENABLE_JSON_PARSER=ON -DAPP_ENABLE_TOML_PARSER=ON \
            -DAPP_ENABLE_XML_PARSER=ON -DAPP_ENABLE_YAML_PARSER=ON
    done

    # PostgreSQL support alongside file user databases ("retrieve specific data elsewhere").
    # shellcheck disable=SC2086
    run_case "$(lower "$BUILD_TYPE")-pg-alongside-sqlcipher" \
        -DAPP_ENABLE_POSTGRESQL=ON -DAPP_USERS_DATABASE_TYPE=SQLCIPHER -DAPP_ENABLE_SQLCIPHER=ON \
        -DAPP_ENCRYPTION_USER_DATABASE=OFF \
        -DAPP_CONFIG_FORMAT=JSON $(feature_flags minimal) \
        -DAPP_ENABLE_JSON_PARSER=ON -DAPP_ENABLE_TOML_PARSER=ON \
        -DAPP_ENABLE_XML_PARSER=ON -DAPP_ENABLE_YAML_PARSER=ON
else
    echo "--- [matrix] postgres combos skipped (no server at ${PGHOST:-127.0.0.1}:${PGPORT:-5432}; set MATRIX_PG=1 to force) ---"
fi

echo "---- matrix summary: $PASS passed, $FAIL failed ----"

if ((FAIL > 0)); then
    printf '  FAILED: %s\n' "${FAILED_CASES[@]}"
    exit 1
fi
