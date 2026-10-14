#!/usr/bin/env bash

# Run the server from a preset build dir (see docs/cmake.md).
# Usage: [PRESET=<preset>] scripts/run.sh [args...]
#   (default preset: debug-ninja)
# The default --config is configs/app.conf (created from defaults when
# missing); pass --config PATH explicitly to override. Extra args go to
# the server.

set -euo pipefail
cd "$(dirname "$0")/.."

PRESET="${PRESET:-debug-ninja}"
BIN="build/${PRESET}/oatpp-backend"

if [[ ! -x "$BIN" ]]; then
    echo "error: '${BIN}' not found; build the preset first: scripts/build.sh ${PRESET}" >&2
    exit 1
fi

HAS_CONFIG=0

for a in "$@"; do
    [[ "$a" == "--config" ]] && HAS_CONFIG=1
done

if ((!HAS_CONFIG)); then
    set -- --config "configs/app.conf" "$@"
fi

exec "$BIN" "$@"
