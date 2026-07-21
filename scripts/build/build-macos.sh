#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"
QT_VERSION="${QT_VERSION:-6.10.2}"
QT_OUTPUT_DIR="${QT_OUTPUT_DIR:-$ROOT_DIR/.qt}"
# aqt's macOS module list does not expose every framework that ships in the
# base desktop package. Keep the explicit add-on set minimal so the install
# command stays valid across current mirrors.
QT_MODULES="${QT_MODULES:-qtmultimedia qtshadertools}"
QT_DESKTOP_ARCH="${QT_DESKTOP_ARCH:-clang_64}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-macos}"
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-13.0}"
MIACODE_BUILD_DEV_TOOLS="${MIACODE_BUILD_DEV_TOOLS:-OFF}"
RUNNER_ARCH="$(uname -m)"

if [[ "$RUNNER_ARCH" != "arm64" ]]; then
  echo "MiaCode for macOS is arm64-only; an Apple-Silicon build host is required (got: $RUNNER_ARCH)." >&2
  exit 1
fi
CMAKE_ARCH="arm64"

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
export BUILD_DIR

chmod +x "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"
bash "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"

chmod +x "$ROOT_DIR/scripts/build/package-mac.sh"
bash "$ROOT_DIR/scripts/build/package-mac.sh"
