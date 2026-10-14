#!/usr/bin/env bash

# Full backend x format x feature test matrix WITHOUT checked-in presets.
#
# CMakePresets.json intentionally stays at base/debug/release. This script
# drives plain `cmake -S/-B` with explicit -D flags (command-line -D wins over
# any preset/base defaults) for every combination, then builds and tests it.
# Build trees land in build/matrix/<combo>/, never clobbering build/debug.
#
# Usage: scripts/test_matrix.sh [debug|release]   (default: debug)
#
# Env (space-separated subsets; defaults = everything):
#   MATRIX_BACKENDS="SQLITE SQLCIPHER"
#   MATRIX_FORMATS="JSON TOML XML YAML"
#   MATRIX_FEATURES="full swagger zlib minimal"
#   MATRIX_PARSERS=1   # also run parser-exclusion builds + fail-fast negatives
#   MATRIX_VERSION_FORMATS="JSON"  # version-file formats to exercise
#                                  # (same four options / parser rules as
#                                  # APP_CONFIG_FORMAT; space-separated)
#   MATRIX_TESTS=1     # 0 skips building/running tests (build-only matrix)
#   JOBS=$(nproc)      # build parallelism
#   GENERATOR=""       # e.g. GENERATOR=Ninja to pass -G explicitly

set -u
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-debug}"

case "$BUILD_TYPE" in
    debug)
        CMAKE_BT="Debug"
        ;;
    release)
        CMAKE_BT="Release"
        ;;
    *)
        echo "usage: $0 [debug|release]" >&2
        exit 2
        ;;
esac

DB_BACKENDS=${MATRIX_BACKENDS:-"SQLITE SQLCIPHER"}
DFG_FORMATS=${MATRIX_FORMATS:-"JSON TOML XML YAML"}
FEATURES=${MATRIX_FEATURES:-"full swagger zlib minimal"}
PARSERS=${MATRIX_PARSERS:-1}
VFMTS=${MATRIX_VERSION_FORMATS:-"JSON"}
TESTS=${MATRIX_TESTS:-1}
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

    if ! cmake -S . -B "$dir" "${GEN_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE="$CMAKE_BT" "${args[@]}" >>"$dir.configure.log" 2>&1; then
        echo "  CONFIGURE FAILED (see $dir.configure.log)"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (configure)")
        return 0
    fi

    if ! cmake --build "$dir" -j"$JOBS" >>"$dir.build.log" 2>&1; then
        echo "  BUILD FAILED (see $dir.build.log)"
        FAIL=$((FAIL + 1))
        FAILED_CASES+=("${name} (build)")
        return 0
    fi

    if [[ "$TESTS" == "0" ]]; then
        echo "  PASS (build only, tests skipped)"
        PASS=$((PASS + 1))
        return 0
    fi

    if ! ctest --test-dir "$dir" --output-on-failure >>"$dir.test.log" 2>&1; then
        echo "  TEST FAILED (see $dir.test.log)"
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

    if cmake -S . -B "$dir" "${GEN_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE="$CMAKE_BT" "$@" >>"$dir.configure.log" 2>&1; then
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

# --- main matrix: every backend x format x feature, all parsers on ---
# shellcheck disable=SC2206
for be in $DB_BACKENDS; do
    for fmt in $DFG_FORMATS; do
        for feat in $FEATURES; do
            # shellcheck disable=SC2086
            run_case "$(lower "$BUILD_TYPE")-$(lower "$be")-$(lower "$fmt")-$feat" \
                -DAPP_DATABASE_BACKEND="$be" -DAPP_CONFIG_FORMAT="$fmt" \
                $(feature_flags "$feat") \
                -DAPP_ENABLE_JSON_PARSER=ON -DAPP_ENABLE_TOML_PARSER=ON \
                -DAPP_ENABLE_XML_PARSER=ON -DAPP_ENABLE_YAML_PARSER=ON
        done
    done
done

if [[ "$PARSERS" == "1" ]]; then
    # --- parser exclusion: each format with ONLY its own parser, minimal ---
    for fmt in $DFG_FORMATS; do
        # shellcheck disable=SC2086
        run_case "$(lower "$BUILD_TYPE")-excl-$(lower "$fmt")" \
            -DAPP_DATABASE_BACKEND=SQLITE -DAPP_CONFIG_FORMAT="$fmt" \
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
            -DAPP_DATABASE_BACKEND=SQLITE -DAPP_CONFIG_FORMAT=JSON \
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

echo "---- matrix summary: $PASS passed, $FAIL failed ----"

if ((FAIL > 0)); then
    printf '  FAILED: %s\n' "${FAILED_CASES[@]}"
    exit 1
fi
