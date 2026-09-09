#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_JOBS="${BUILD_JOBS:-8}"

if ! [[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "BUILD_JOBS must be a positive integer (got: $BUILD_JOBS)" >&2
  exit 2
fi

exec env BUILD_JOBS="$BUILD_JOBS" bash "$ROOT_DIR/scripts/build/package-linux.sh"
