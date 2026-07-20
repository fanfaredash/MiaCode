#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FFMPEG_DIR="${MIACODE_MACOS_FFMPEG_DIR:-$ROOT_DIR/third_party/ffmpeg/macos}"
FFMPEG_PATH="$FFMPEG_DIR/ffmpeg"
# Pinned to a macOS arm64 static ffmpeg build that ships
# h264_videotoolbox/hevc_videotoolbox for hardware export.
FFMPEG_URL="${MIACODE_MACOS_FFMPEG_URL:-https://ffmpeg.martin-riedl.de/download/macos/arm64/1783011502_8.1.2/ffmpeg.zip}"
EXPECTED_SHA256="${MIACODE_MACOS_FFMPEG_SHA256:-EAF91238E104DD0E262BC6510E25061855CC99A6955A721B0AC99660D58C473D}"
EXPECTED_VERSION_PATTERN="${MIACODE_MACOS_FFMPEG_VERSION_PATTERN:-^ffmpeg version 8\\.1\\.2([.-]|$)}"
EXPECTED_ARCH="${MIACODE_MACOS_FFMPEG_ARCH:-arm64}"

compute_sha256() {
  shasum -a 256 "$1" | awk '{print toupper($1)}'
}

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "Required command not found: $name" >&2
    exit 1
  fi
}

validate_arch() {
  local binary_path="$1"
  local archs
  archs="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  if [[ "$archs" != *"$EXPECTED_ARCH"* ]]; then
    echo "ffmpeg binary is not $EXPECTED_ARCH (got: ${archs:-unknown}): $binary_path" >&2
    return 1
  fi
  return 0
}

validate_version_output() {
  local binary_path="$1"
  local version_line
  version_line="$("$binary_path" -version 2>/dev/null | head -n 1)"
  if [[ -z "$version_line" ]]; then
    echo "Failed to read ffmpeg version line: $binary_path" >&2
    return 1
  fi
  if ! printf '%s' "$version_line" | grep -Eq "$EXPECTED_VERSION_PATTERN"; then
    echo "Unexpected ffmpeg version output: $version_line" >&2
    return 1
  fi
  return 0
}

validate_existing_binary() {
  if [[ ! -f "$FFMPEG_PATH" || ! -s "$FFMPEG_PATH" ]]; then
    return 1
  fi

  local actual_sha256
  actual_sha256="$(compute_sha256 "$FFMPEG_PATH")"
  if [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
    echo "Existing ffmpeg hash mismatch at $FFMPEG_PATH" >&2
    echo "Expected: $EXPECTED_SHA256" >&2
    echo "Actual:   $actual_sha256" >&2
    return 1
  fi

  chmod +x "$FFMPEG_PATH"
  if ! validate_arch "$FFMPEG_PATH"; then
    return 1
  fi
  if ! validate_version_output "$FFMPEG_PATH"; then
    return 1
  fi
  echo "Using existing macOS ffmpeg: $FFMPEG_PATH"
  return 0
}

require_command curl
require_command unzip
require_command shasum

if validate_existing_binary; then
  exit 0
fi

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

archive_path="$tmp_dir/ffmpeg.zip"
extract_dir="$tmp_dir/extracted"

echo "Downloading macOS ffmpeg from $FFMPEG_URL"
curl \
  --fail \
  --location \
  --http1.1 \
  --retry 5 \
  --retry-all-errors \
  --retry-delay 2 \
  --output "$archive_path" \
  "$FFMPEG_URL"

mkdir -p "$extract_dir"
unzip -q "$archive_path" -d "$extract_dir"

downloaded_path="$(find "$extract_dir" -type f -name ffmpeg -print -quit)"
if [[ -z "$downloaded_path" ]]; then
  echo "ffmpeg binary not found inside downloaded archive: $archive_path" >&2
  exit 1
fi

actual_sha256="$(compute_sha256 "$downloaded_path")"
if [[ "$actual_sha256" != "$EXPECTED_SHA256" ]]; then
  echo "Downloaded ffmpeg hash mismatch" >&2
  echo "Expected: $EXPECTED_SHA256" >&2
  echo "Actual:   $actual_sha256" >&2
  exit 1
fi

mkdir -p "$FFMPEG_DIR"
install -m 755 "$downloaded_path" "$FFMPEG_PATH"
if ! validate_arch "$FFMPEG_PATH"; then
  exit 1
fi
if ! validate_version_output "$FFMPEG_PATH"; then
  exit 1
fi

echo "Prepared macOS ffmpeg at $FFMPEG_PATH"
