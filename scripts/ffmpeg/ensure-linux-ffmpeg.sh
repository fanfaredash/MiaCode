#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FFMPEG_DIR="${MIACODE_LINUX_FFMPEG_DIR:-$ROOT_DIR/third_party/ffmpeg/linux}"
FFMPEG_PATH="$FFMPEG_DIR/ffmpeg"
CACHE_DIR="${MIACODE_LINUX_FFMPEG_CACHE_DIR:-$ROOT_DIR/build/ffmpeg}"

readonly FFMPEG_ARCHIVE_NAME="ffmpeg-n7.1.5-9-gb9a218bc1e-linux64-gpl-7.1.tar.xz"
readonly FFMPEG_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-07-22-13-36/$FFMPEG_ARCHIVE_NAME"
readonly EXPECTED_ARCHIVE_SHA256="1aa3246a617f1a1d17d2e9027685d19c633c10cd6e3d1e0589dfe1ed1db5f3fb"
readonly EXPECTED_BINARY_SHA256="a253d9e7a94c9376d67d2df144f16671c8b3e2875a6311188146bdce2889e85a"
readonly EXPECTED_VERSION_PATTERN='^ffmpeg version n?7\.1\.5([.-]|$)'

require_command() {
  local command_name="$1"
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Required command not found: $command_name" >&2
    exit 1
  fi
}

compute_sha256() {
  sha256sum "$1" | awk '{print tolower($1)}'
}

require_output_token() {
  local output="$1"
  local token="$2"
  local category="$3"
  if ! grep -Eq "(^|[[:space:]])${token}([[:space:]]|$)" <<<"$output"; then
    echo "FFmpeg is missing required ${category}: ${token}" >&2
    return 1
  fi
}

validate_binary() {
  local binary_path="$1"
  if [[ ! -f "$binary_path" || ! -s "$binary_path" || ! -x "$binary_path" ]]; then
    return 1
  fi

  local actual_sha256
  actual_sha256="$(compute_sha256 "$binary_path")"
  if [[ "$actual_sha256" != "$EXPECTED_BINARY_SHA256" ]]; then
    echo "FFmpeg binary hash mismatch at $binary_path" >&2
    echo "Expected: $EXPECTED_BINARY_SHA256" >&2
    echo "Actual:   $actual_sha256" >&2
    return 1
  fi

  local file_description
  file_description="$(file -L "$binary_path")"
  if ! grep -q "ELF 64-bit" <<<"$file_description" || ! grep -q "x86-64" <<<"$file_description"; then
    echo "FFmpeg is not a Linux x86-64 ELF binary: $file_description" >&2
    return 1
  fi
  local runtime_dependencies
  runtime_dependencies="$(ldd "$binary_path" 2>&1)"
  if grep -q "not found" <<<"$runtime_dependencies"; then
    echo "FFmpeg has unresolved runtime dependencies:" >&2
    grep "not found" <<<"$runtime_dependencies" >&2
    return 1
  fi
  if grep -Eq 'lib(avcodec|avdevice|avfilter|avformat|avutil|postproc|swresample|swscale)\.so' \
    <<<"$runtime_dependencies"; then
    echo "FFmpeg unexpectedly depends on system FFmpeg shared libraries:" >&2
    echo "$runtime_dependencies" >&2
    return 1
  fi

  local version_line
  version_line="$("$binary_path" -hide_banner -version 2>/dev/null | head -n 1)"
  if ! grep -Eq "$EXPECTED_VERSION_PATTERN" <<<"$version_line"; then
    echo "Unexpected FFmpeg version: ${version_line:-<empty>}" >&2
    return 1
  fi

  local encoders filters protocols formats
  encoders="$("$binary_path" -hide_banner -encoders 2>/dev/null)"
  filters="$("$binary_path" -hide_banner -filters 2>/dev/null)"
  protocols="$("$binary_path" -hide_banner -protocols 2>/dev/null)"
  formats="$("$binary_path" -hide_banner -formats 2>/dev/null)"

  require_output_token "$encoders" "libx264" "encoder"
  require_output_token "$encoders" "aac" "encoder"
  for filter_name in \
    scale crop pad fps format overlay fade color setpts tpad alphamerge \
    atrim asetpts aresample aformat concat amix; do
    require_output_token "$filters" "$filter_name" "filter"
  done
  for protocol_name in file pipe; do
    require_output_token "$protocols" "$protocol_name" "protocol"
  done
  require_output_token "$formats" "mp4" "format"
  require_output_token "$formats" "rawvideo" "format"

  echo "Validated Linux FFmpeg: $binary_path"
  echo "  $version_line"
}

require_command curl
require_command file
require_command ldd
require_command sha256sum
require_command tar

if validate_binary "$FFMPEG_PATH"; then
  exit 0
fi

ARCHIVE_PATH="$CACHE_DIR/$FFMPEG_ARCHIVE_NAME"
EXTRACT_DIR="$CACHE_DIR/ffmpeg-n7.1.5-9-gb9a218bc1e"
mkdir -p "$CACHE_DIR"

if [[ ! -f "$ARCHIVE_PATH" ]] \
  || [[ "$(compute_sha256 "$ARCHIVE_PATH")" != "$EXPECTED_ARCHIVE_SHA256" ]]; then
  download_path="$ARCHIVE_PATH.part"
  echo "Downloading fixed Linux FFmpeg from $FFMPEG_URL"
  curl \
    --fail \
    --location \
    --http1.1 \
    --retry 5 \
    --retry-all-errors \
    --retry-delay 2 \
    --output "$download_path" \
    "$FFMPEG_URL"
  actual_archive_sha256="$(compute_sha256 "$download_path")"
  if [[ "$actual_archive_sha256" != "$EXPECTED_ARCHIVE_SHA256" ]]; then
    echo "Downloaded FFmpeg archive hash mismatch" >&2
    echo "Expected: $EXPECTED_ARCHIVE_SHA256" >&2
    echo "Actual:   $actual_archive_sha256" >&2
    rm -f "$download_path"
    exit 1
  fi
  mv -f "$download_path" "$ARCHIVE_PATH"
fi

rm -rf "$EXTRACT_DIR"
mkdir -p "$EXTRACT_DIR"
tar -xJf "$ARCHIVE_PATH" -C "$EXTRACT_DIR"

mapfile -t downloaded_paths < <(find "$EXTRACT_DIR" -type f -name ffmpeg -print)
if [[ "${#downloaded_paths[@]}" -ne 1 ]]; then
  echo "FFmpeg binary was not found in $ARCHIVE_PATH" >&2
  exit 1
fi
downloaded_path="${downloaded_paths[0]}"

mkdir -p "$FFMPEG_DIR"
install -m 755 "$downloaded_path" "$FFMPEG_PATH"
validate_binary "$FFMPEG_PATH"

echo "Prepared Linux FFmpeg at $FFMPEG_PATH"
echo "Download cache retained at $CACHE_DIR"
