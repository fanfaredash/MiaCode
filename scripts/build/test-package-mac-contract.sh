#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PACKAGE_SCRIPT="$ROOT_DIR/scripts/build/package-mac.sh"
PROVISION_SCRIPT="$ROOT_DIR/scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh"

require_text() {
  local file_path="$1"
  local expected="$2"
  if ! /usr/bin/grep -Fq -- "$expected" "$file_path"; then
    echo "Missing required packaging contract text in $file_path: $expected" >&2
    exit 1
  fi
}

reject_text() {
  local file_path="$1"
  local rejected="$2"
  if /usr/bin/grep -Fq -- "$rejected" "$file_path"; then
    echo "Forbidden packaging contract text remains in $file_path: $rejected" >&2
    exit 1
  fi
}

if [[ ! -f "$PACKAGE_SCRIPT" ]]; then
  echo "Missing macOS package script: $PACKAGE_SCRIPT" >&2
  exit 1
fi

# The package must not discover or recursively copy the build machine's
# Homebrew closure. It stages the fixed six-library local SDK instead.
reject_text "$PACKAGE_SCRIPT" 'brew --prefix'
reject_text "$PACKAGE_SCRIPT" 'bundle_homebrew_loader_path_dependencies'
require_text "$PACKAGE_SCRIPT" 'stage_macos_ffmpeg_runtime'
require_text "$PACKAGE_SCRIPT" 'strip_absolute_build_rpaths'
require_text "$PACKAGE_SCRIPT" 'libavcodec.60.dylib'
require_text "$PACKAGE_SCRIPT" 'libavfilter.9.dylib'
require_text "$PACKAGE_SCRIPT" 'libavformat.60.dylib'
require_text "$PACKAGE_SCRIPT" 'libavutil.58.dylib'
require_text "$PACKAGE_SCRIPT" 'libswresample.4.dylib'
require_text "$PACKAGE_SCRIPT" 'libswscale.7.dylib'
require_text "$PACKAGE_SCRIPT" '@executable_path/../Frameworks'
require_text "$PACKAGE_SCRIPT" 'libffmpegmediaplugin.dylib'
require_text "$PACKAGE_SCRIPT" 'libavcodec.61.dylib'
require_text "$PACKAGE_SCRIPT" 'libavformat.61.dylib'
require_text "$PACKAGE_SCRIPT" 'libavutil.59.dylib'
require_text "$PACKAGE_SCRIPT" 'libswresample.5.dylib'
require_text "$PACKAGE_SCRIPT" 'libswscale.8.dylib'
require_text "$PACKAGE_SCRIPT" 'libavfilter.10.dylib'
require_text "$PACKAGE_SCRIPT" 'ZIP-extracted app'

if [[ ! -f "$PROVISION_SCRIPT" ]]; then
  echo "Missing macOS FFmpeg SDK provisioner: $PROVISION_SCRIPT" >&2
  exit 1
fi
require_text "$PROVISION_SCRIPT" 'FFMPEG_VERSION="6.1.2"'
require_text "$PROVISION_SCRIPT" 'https://ffmpeg.org/releases/'
require_text "$PROVISION_SCRIPT" '3b624649725ecdc565c903ca6643d41f33bd49239922e45c9b1442c63dca4e38'
require_text "$PROVISION_SCRIPT" 'third_party/ffmpeg/macos/dev'
require_text "$PROVISION_SCRIPT" 'MIACODE_FFMPEG_BUILD_JOBS'
require_text "$PROVISION_SCRIPT" '--disable-avdevice'
require_text "$PROVISION_SCRIPT" '--disable-postproc'

echo 'macOS package contract: passed'
