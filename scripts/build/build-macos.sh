#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
QT_VERSION="${QT_VERSION:-6.8.3}"
QT_OUTPUT_DIR="${QT_OUTPUT_DIR:-$ROOT_DIR/.qt}"
QT_MODULES="${QT_MODULES:-qtmultimedia qtdeclarative qtshadertools qtsvg}"
QT_DESKTOP_ARCH="${QT_DESKTOP_ARCH:-clang_64}"
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-13.0}"
MIACODE_BUILD_DEV_TOOLS="${MIACODE_BUILD_DEV_TOOLS:-OFF}"
RUNNER_ARCH="$(uname -m)"

case "$RUNNER_ARCH" in
  arm64)
    CMAKE_ARCH="arm64"
    ;;
  x86_64)
    CMAKE_ARCH="x86_64"
    ;;
  *)
    echo "Unsupported macOS runner architecture: $RUNNER_ARCH" >&2
    exit 1
    ;;
esac

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "Python executable not found: $PYTHON_BIN" >&2
  exit 1
fi

"$PYTHON_BIN" -m pip install --user "aqtinstall==3.3.*" "py7zr==1.0.*"
"$PYTHON_BIN" -m aqt install-qt mac desktop "$QT_VERSION" "$QT_DESKTOP_ARCH" \
  --outputdir "$QT_OUTPUT_DIR" \
  --modules $QT_MODULES

QT_MACDEPLOYQT="$(find "$QT_OUTPUT_DIR" -path '*/bin/macdeployqt' -type f -print -quit)"
if [[ -z "$QT_MACDEPLOYQT" ]]; then
  echo "macdeployqt not found under $QT_OUTPUT_DIR" >&2
  exit 1
fi

QT_ROOT_DIR="$(cd "$(dirname "$QT_MACDEPLOYQT")/.." && pwd)"
export QT_ROOT_DIR
export CMAKE_OSX_ARCHITECTURES="$CMAKE_ARCH"
export CMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET"
export MIACODE_BUILD_DEV_TOOLS

chmod +x "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"
bash "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"

chmod +x "$ROOT_DIR/scripts/build/package-mac.sh"
bash "$ROOT_DIR/scripts/build/package-mac.sh"
