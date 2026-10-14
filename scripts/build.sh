#!/usr/bin/env bash

# Configure + build + test one CMake preset (see docs/cmake.md).
# Usage: scripts/build.sh [preset]   (default: debug-ninja)
# Any preset cache value can be overridden: scripts/build.sh debug -D... is
# not supported here; use cmake --preset debug -DKEY=VALUE directly instead.

set -euo pipefail
cd "$(dirname "$0")/.."
PRESET="${1:-debug-ninja}"
cmake --workflow --preset "$PRESET"
