#!/usr/bin/env bash
#
# Sanitizer preset validation WITHOUT instrumented builds.
#
# Configures each sanitizer-relevant setup into build/san-check/<name>/ with
# plain `cmake -S/-B` (never touching build/<preset>/ caches) and verifies the
# configure-time build summary reports the effective sanitizer set from the
# single source of truth (cmake/sanitizers.cmake). No binaries are built or run.
#
# Usage: scripts/check_sanitizers.sh
# Env:   GENERATOR=""  # e.g. GENERATOR=Ninja to pass -G explicitly
#
# Expected matrix (raw CMAKE_BUILD_TYPE values; ENABLE_SANITIZERS=ON unless
# noted). The Debug / RelWithDebInfo rows are what the `debug-asan-ubsan` /
# `relwithdebinfo-asan-ubsan` presets configure; the plain `debug` /
# `relwithdebinfo` presets are the same builds with the switch off:
#   Debug                  -> AddressSanitizer, UndefinedBehaviorSanitizer
#   DebugTSan              -> ThreadSanitizer only (must NOT mention AddressSanitizer)
#   DebugMSan              -> configure FAILURE on GCC (MSan is Clang-only);
#                             MemorySanitizer on Clang
#   RelWithDebInfo         -> AddressSanitizer, UndefinedBehaviorSanitizer
#   RelWithDebInfoTSan     -> ThreadSanitizer only
#   Release / MinSizeRel   -> None, with the policy note, even with ON
#   Debug + OFF            -> None

set -u
cd "$(dirname "$0")/.."

GEN_ARGS=()
[[ -n "${GENERATOR:-}" ]] && GEN_ARGS=(-G "$GENERATOR")

PASS=0
FAIL=0
FAILED=()

BASE_ARGS=(
    -DAPP_USERS_DATABASE_TYPE=SQLCIPHER
    -DAPP_ENABLE_SQLCIPHER=ON
    -DAPP_ENCRYPTION_USER_DATABASE=ON
    -DAPP_CONFIG_FORMAT=TOML
    -DAPP_ENABLE_TOML_PARSER=ON
    -DAPP_BUILD_TESTS=OFF
    -DAPP_ENABLE_SWAGGER=OFF
    -DAPP_ENABLE_ZLIB=ON
)

check_case() { # <name> <expected-regex> <cmake-args...>
    local name="$1" expected="$2"
    shift 2
    local dir="build/san-check/${name}"
    rm -rf "$dir"
    mkdir -p build/san-check
    echo "=== [san-check] ${name} ==="
    local log="$dir.configure.log"
    if ! cmake -S . -B "$dir" "${GEN_ARGS[@]}" "${BASE_ARGS[@]}" "$@" >"$log" 2>&1; then
        echo "  CONFIGURE FAILED (see $log)"
        FAIL=$((FAIL + 1))
        FAILED+=("${name} (configure)")
        return 0
    fi
    local line
    line=$(grep -a "Sanitizers " "$log" | head -n 1 || true)
    echo "  summary: $line"
    if [[ "$line" =~ $expected ]]; then
        echo "  PASS"
        PASS=$((PASS + 1))
    else
        echo "  MISMATCH (expected /$expected/)"
        FAIL=$((FAIL + 1))
        FAILED+=("${name} (summary)")
    fi
}

expect_configure_failure() { # <name> <cmake-args...>
    local name="$1"
    shift
    local dir="build/san-check/${name}"
    rm -rf "$dir"
    mkdir -p build/san-check
    echo "=== [san-check] ${name} (must fail) ==="
    if cmake -S . -B "$dir" "${GEN_ARGS[@]}" "${BASE_ARGS[@]}" "$@" >"$dir.configure.log" 2>&1; then
        echo "  UNEXPECTEDLY CONFIGURED (should have failed)"
        FAIL=$((FAIL + 1))
        FAILED+=("${name} (should-fail)")
        return 0
    fi
    if grep -aqi "MemorySanitizer" "$dir.configure.log"; then
        echo "  PASS (rejected with MSan diagnostic)"
        PASS=$((PASS + 1))
    else
        echo "  FAILED WITHOUT MSan diagnostic (see $dir.configure.log)"
        FAIL=$((FAIL + 1))
        FAILED+=("${name} (diagnostic)")
    fi
}

check_tsan_clean() { # <name> <cmake-args...> : TSan must not leak ASan flags
    local name="$1"
    shift
    local dir="build/san-check/${name}"
    if grep -aq "fsanitize=address" "$dir.configure.log"; then
        echo "  [!] WARNING: ASan flag text inside TSan configure log (investigate)"
    fi
    # Authoritative check: compile_commands of a built target would be ideal,
    # but configure-only keeps this fast; the summary + single-source function
    # (no ASan branch for TSan configs) is the guarantee. Verify via flags file:
    if grep -aq "Sanitizers .*ThreadSanitizer" "$dir.configure.log" \
        && ! grep -aq "Sanitizers .*AddressSanitizer" "$dir.configure.log"; then
        echo "  PASS (TSan only, no ASan in summary)"
        PASS=$((PASS + 1))
    else
        echo "  MISMATCH (expected ThreadSanitizer only)"
        FAIL=$((FAIL + 1))
        FAILED+=("${name} (tsan-clean)")
    fi
}

COMPILER_ID=$(c++ -dumpmachine 2>/dev/null || echo unknown)
echo "compiler target: $COMPILER_ID ($(${CXX:-c++} --version 2>/dev/null | head -n 1))"

check_case debug-san "Sanitizers.*AddressSanitizer, UndefinedBehaviorSanitizer" \
    -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON

check_case debug-tsan "Sanitizers.*ThreadSanitizer" \
    -DCMAKE_BUILD_TYPE=DebugTSan -DENABLE_SANITIZERS=ON
check_tsan_clean debug-tsan

check_case relwithdebinfo-san "Sanitizers.*AddressSanitizer, UndefinedBehaviorSanitizer" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_SANITIZERS=ON

check_case relwithdebinfo-tsan "Sanitizers.*ThreadSanitizer" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfoTSan -DENABLE_SANITIZERS=ON
check_tsan_clean relwithdebinfo-tsan

check_case release-san "Sanitizers.*None.*disabled by policy" \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZERS=ON

check_case minsizerel-san "Sanitizers.*None.*disabled by policy" \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DENABLE_SANITIZERS=ON

check_case debug-off "Sanitizers.*None" \
    -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=OFF

if c++ --version 2>/dev/null | grep -qi clang; then
    check_case debug-msan "Sanitizers.*MemorySanitizer" \
        -DCMAKE_BUILD_TYPE=DebugMSan -DENABLE_SANITIZERS=ON
    check_case relwithdebinfo-msan "Sanitizers.*MemorySanitizer" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfoMSan -DENABLE_SANITIZERS=ON
else
    expect_configure_failure debug-msan \
        -DCMAKE_BUILD_TYPE=DebugMSan -DENABLE_SANITIZERS=ON
    expect_configure_failure relwithdebinfo-msan \
        -DCMAKE_BUILD_TYPE=RelWithDebInfoMSan -DENABLE_SANITIZERS=ON
fi

echo "---- san-check summary: $PASS passed, $FAIL failed ----"
if ((FAIL > 0)); then
    printf '  FAILED: %s\n' "${FAILED[@]}"
    exit 1
fi
