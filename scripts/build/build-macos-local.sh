#!/usr/bin/env bash
# macOS local packaging entry: reuses the Qt install, the FFmpeg SDK, and the
# export ffmpeg binary already present on this machine, then hands the Release
# build and package assembly to package-mac.sh.
# CI runs use build-macos-ci.sh, which installs Qt and the FFmpeg SDKs first.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
QT_VERSION="${QT_VERSION:-6.10.2}"
QT_ROOT="${QT_ROOT:-${QT_ROOT_DIR:-}}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-macos}"
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-13.0}"
MIACODE_BUILD_DEV_TOOLS="${MIACODE_BUILD_DEV_TOOLS:-OFF}"
FFMPEG_DEV_DIR="${MIACODE_FFMPEG_DEV_DIR:-$ROOT_DIR/third_party/ffmpeg/macos/dev}"
FFMPEG_EXPORT_BINARY="$ROOT_DIR/third_party/ffmpeg/macos/ffmpeg"
RUNNER_ARCH="$(uname -m)"

if [[ "$RUNNER_ARCH" != "arm64" ]]; then
  echo "MiaCode for macOS is arm64-only; an Apple-Silicon build host is required (got: $RUNNER_ARCH)." >&2
  exit 1
fi

if [[ -z "$QT_ROOT" ]]; then
  for candidate in "$ROOT_DIR/.qt/$QT_VERSION/macos" "$HOME/Qt/$QT_VERSION/macos"; do
    if [[ -x "$candidate/bin/macdeployqt" ]]; then
      QT_ROOT="$candidate"
      break
    fi
  done
fi
if [[ -z "$QT_ROOT" || ! -x "$QT_ROOT/bin/macdeployqt" ]]; then
  echo "No Qt $QT_VERSION found for the macOS build." >&2
  echo "Set QT_ROOT to a Qt $QT_VERSION macos directory, or install one with:" >&2
  echo "  python3 -m aqt install-qt mac desktop $QT_VERSION clang_64 --outputdir .qt --modules qtmultimedia qtshadertools" >&2
  exit 1
fi

if [[ ! -f "$FFMPEG_DEV_DIR/include/libavcodec/avcodec.h" || ! -d "$FFMPEG_DEV_DIR/lib" ]]; then
  echo "Missing macOS FFmpeg development SDK: $FFMPEG_DEV_DIR" >&2
  echo "Run: bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh" >&2
  exit 1
fi

if [[ ! -s "$FFMPEG_EXPORT_BINARY" ]]; then
  echo "Missing macOS export ffmpeg binary: $FFMPEG_EXPORT_BINARY" >&2
  echo "Run: bash scripts/ffmpeg/ensure-macos-ffmpeg.sh" >&2
  exit 1
fi

export QT_ROOT_DIR="$QT_ROOT"
export BUILD_DIR
export CMAKE_OSX_ARCHITECTURES="arm64"
export CMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET"
export MIACODE_BUILD_DEV_TOOLS
export MIACODE_FFMPEG_DEV_DIR="$FFMPEG_DEV_DIR"

chmod +x "$ROOT_DIR/scripts/build/package-mac.sh"
bash "$ROOT_DIR/scripts/build/package-mac.sh"
