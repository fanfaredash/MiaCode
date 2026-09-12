#!/usr/bin/env bash
# macOS CI packaging entry: provisions Qt and the FFmpeg SDKs on a clean runner,
# then hands the Release build and package assembly to package-mac.sh.
# Local work uses build-macos-local.sh, which reuses whatever is already installed.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYTHON_BOOTSTRAP_BIN="${PYTHON_BIN:-python3}"
PYTHON_VENV_DIR="${MIACODE_PYTHON_VENV_DIR:-$ROOT_DIR/.venv/macos-build}"
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
MIACODE_FFMPEG_BUILD_JOBS="${MIACODE_FFMPEG_BUILD_JOBS:-4}"
RUNNER_ARCH="$(uname -m)"

if [[ "$RUNNER_ARCH" != "arm64" ]]; then
  echo "MiaCode for macOS is arm64-only; an Apple-Silicon build host is required (got: $RUNNER_ARCH)." >&2
  exit 1
fi
CMAKE_ARCH="arm64"

if ! command -v "$PYTHON_BOOTSTRAP_BIN" >/dev/null 2>&1; then
  echo "Python executable not found: $PYTHON_BOOTSTRAP_BIN" >&2
  exit 1
fi

if [[ ! -x "$PYTHON_VENV_DIR/bin/python" ]]; then
  echo "Creating Python build environment: $PYTHON_VENV_DIR"
  "$PYTHON_BOOTSTRAP_BIN" -m venv "$PYTHON_VENV_DIR"
fi
PYTHON_BIN="$PYTHON_VENV_DIR/bin/python"
if ! "$PYTHON_BIN" -m pip --version >/dev/null 2>&1; then
  echo "Python build environment has no pip: $PYTHON_VENV_DIR" >&2
  exit 1
fi

export PIP_DISABLE_PIP_VERSION_CHECK=1
export PIP_NO_INPUT=1
"$PYTHON_BIN" -m pip install --upgrade "aqtinstall==3.3.*" "py7zr==1.0.*"
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
export MIACODE_FFMPEG_BUILD_JOBS
export BUILD_DIR

chmod +x "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh"
bash "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh"

chmod +x "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"
bash "$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg.sh"

chmod +x "$ROOT_DIR/scripts/build/package-mac.sh"
bash "$ROOT_DIR/scripts/build/package-mac.sh"
