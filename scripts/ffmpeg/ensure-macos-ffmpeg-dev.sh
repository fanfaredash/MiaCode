#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK_DIR="${MIACODE_MACOS_FFMPEG_DEV_DIR:-$ROOT_DIR/third_party/ffmpeg/macos/dev}"
SDK_PARENT="$(dirname "$SDK_DIR")"
FFMPEG_VERSION="6.1.2"
FFMPEG_ARCHIVE="ffmpeg-${FFMPEG_VERSION}.tar.xz"
FFMPEG_URL="https://ffmpeg.org/releases/${FFMPEG_ARCHIVE}"
FFMPEG_SHA256="3b624649725ecdc565c903ca6643d41f33bd49239922e45c9b1442c63dca4e38"
MACOS_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-13.0}"
BUILD_JOBS="${MIACODE_FFMPEG_BUILD_JOBS:-4}"

required_libraries=(
  "libavcodec.60.dylib"
  "libavfilter.9.dylib"
  "libavformat.60.dylib"
  "libavutil.58.dylib"
  "libswresample.4.dylib"
  "libswscale.7.dylib"
)

temp_root=""
staging_dir=""

cleanup() {
  [[ -z "$temp_root" || ! -d "$temp_root" ]] || rm -rf "$temp_root"
  [[ -z "$staging_dir" || ! -d "$staging_dir" ]] || rm -rf "$staging_dir"
}
trap cleanup EXIT

extract_macos_minos() {
  otool -l "$1" | awk '
    $1 == "cmd" && ($2 == "LC_BUILD_VERSION" || $2 == "LC_VERSION_MIN_MACOSX") { command_name = $2; next }
    command_name == "LC_BUILD_VERSION" && $1 == "minos" { print $2; exit }
    command_name == "LC_VERSION_MIN_MACOSX" && $1 == "version" { print $2; exit }
  '
}

version_gt() {
  local lhs="$1" rhs="$2" index limit lhs_part rhs_part
  local -a lhs_parts=(${lhs//./ }) rhs_parts=(${rhs//./ })
  limit=${#lhs_parts[@]}
  (( ${#rhs_parts[@]} <= limit )) || limit=${#rhs_parts[@]}
  for ((index = 0; index < limit; ++index)); do
    lhs_part=${lhs_parts[index]:-0}
    rhs_part=${rhs_parts[index]:-0}
    ((10#$lhs_part > 10#$rhs_part)) && return 0
    ((10#$lhs_part < 10#$rhs_part)) && return 1
  done
  return 1
}

is_allowed_dependency() {
  case "$1" in
    /System/Library/*|/usr/lib/*|*.framework/*|@rpath/libavcodec.60.dylib|@rpath/libavfilter.9.dylib|@rpath/libavformat.60.dylib|@rpath/libavutil.58.dylib|@rpath/libswresample.4.dylib|@rpath/libswscale.7.dylib) return 0 ;;
  esac
  return 1
}

validate_sdk() {
  local sdk_root="$1" library library_path install_name dependency minos actual_count
  [[ -d "$sdk_root/include/libavcodec" && -d "$sdk_root/lib" ]] || {
    echo "FFmpeg SDK layout is incomplete: $sdk_root" >&2
    return 1
  }
  actual_count="$(find "$sdk_root/lib" -type f -name '*.dylib' | wc -l | tr -d '[:space:]')"
  [[ "$actual_count" == "${#required_libraries[@]}" ]] || {
    echo "Expected exactly ${#required_libraries[@]} FFmpeg dylibs in $sdk_root/lib, found $actual_count" >&2
    return 1
  }
  for library in "${required_libraries[@]}"; do
    library_path="$sdk_root/lib/$library"
    [[ -f "$library_path" ]] || { echo "Missing required FFmpeg SDK library: $library_path" >&2; return 1; }
    [[ "$(lipo -archs "$library_path")" == "arm64" ]] || { echo "FFmpeg SDK library is not arm64-only: $library_path" >&2; return 1; }
    install_name="$(otool -D "$library_path" | awk 'NR == 2 { print; exit }')"
    [[ "$install_name" == "@rpath/$library" ]] || { echo "FFmpeg SDK library has non-relocatable install name: $library_path -> $install_name" >&2; return 1; }
    minos="$(extract_macos_minos "$library_path")"
    [[ -n "$minos" ]] && ! version_gt "$minos" "$MACOS_DEPLOYMENT_TARGET" || {
      echo "FFmpeg SDK library exceeds macOS $MACOS_DEPLOYMENT_TARGET: $library_path (minos=${minos:-unknown})" >&2
      return 1
    }
    while IFS= read -r dependency; do
      is_allowed_dependency "$dependency" || { echo "FFmpeg SDK library has unsupported external dependency: $library_path -> $dependency" >&2; return 1; }
    done < <(otool -L "$library_path" | awk 'NR > 1 { sub(/^[[:space:]]*/, ""); sub(/ \(compatibility version.*$/, ""); print }' | awk -v install_name="$install_name" '$0 != install_name')
  done
  if find "$sdk_root/lib" -type f \( -name 'libavdevice*.dylib' -o -name 'libpostproc*.dylib' \) -print -quit | grep -q .; then
    echo "FFmpeg SDK unexpectedly contains avdevice or postproc" >&2
    return 1
  fi
}

[[ "$(uname -s)" == "Darwin" ]] || { echo "macOS FFmpeg SDK provisioning must run on macOS." >&2; exit 2; }
if [[ ! "$BUILD_JOBS" =~ ^[1-4]$ ]]; then
  echo "MIACODE_FFMPEG_BUILD_JOBS must be an integer from 1 through 4 (got: $BUILD_JOBS)" >&2
  exit 2
fi
for required_tool in curl shasum tar make xcrun install_name_tool lipo otool; do
  command -v "$required_tool" >/dev/null 2>&1 || { echo "Missing required tool: $required_tool" >&2; exit 2; }
done

mkdir -p "$SDK_PARENT"
temp_root="$(mktemp -d "${TMPDIR:-/tmp}/miacode-ffmpeg6.XXXXXX")"
archive_path="$temp_root/$FFMPEG_ARCHIVE"
source_dir="$temp_root/ffmpeg-$FFMPEG_VERSION"
staging_dir="$(mktemp -d "$SDK_PARENT/.dev.staging.XXXXXX")"

echo "[ffmpeg-sdk] Downloading FFmpeg $FFMPEG_VERSION"
curl -L --fail --retry 5 --retry-delay 2 -o "$archive_path" "$FFMPEG_URL"
actual_sha256="$(shasum -a 256 "$archive_path" | awk '{ print $1 }')"
[[ "$actual_sha256" == "$FFMPEG_SHA256" ]] || { echo "FFmpeg source checksum mismatch: expected $FFMPEG_SHA256, got $actual_sha256" >&2; exit 1; }
tar -xf "$archive_path" -C "$temp_root"
[[ -x "$source_dir/configure" ]] || { echo "Downloaded FFmpeg source has no configure script: $source_dir" >&2; exit 1; }

sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
cc_path="$(xcrun --sdk macosx --find clang)"
cxx_path="$(xcrun --sdk macosx --find clang++)"
common_flags="-arch arm64 -isysroot $sdk_path -mmacosx-version-min=$MACOS_DEPLOYMENT_TARGET"

echo "[ffmpeg-sdk] Configuring arm64 shared libraries for macOS $MACOS_DEPLOYMENT_TARGET"
(
  cd "$source_dir"
  MACOSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" ./configure \
    --prefix="$staging_dir" --target-os=darwin --arch=arm64 --cc="$cc_path" --cxx="$cxx_path" \
    --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
    --disable-autodetect --disable-gpl --disable-nonfree --disable-avdevice --disable-postproc \
    --enable-videotoolbox --install-name-dir='@rpath' \
    --extra-cflags="$common_flags" --extra-ldflags="$common_flags"
  make -j"$BUILD_JOBS"
  make install
)

for library in "${required_libraries[@]}"; do
  install_name_tool -id "@rpath/$library" "$staging_dir/lib/$library"
done
for library in "${required_libraries[@]}"; do
  library_path="$staging_dir/lib/$library"
  while IFS= read -r dependency; do
    dependency_base="$(basename "$dependency")"
    for required_library in "${required_libraries[@]}"; do
      if [[ "$dependency_base" == "$required_library" && "$dependency" != "@rpath/$required_library" ]]; then
        install_name_tool -change "$dependency" "@rpath/$required_library" "$library_path"
      fi
    done
  done < <(otool -L "$library_path" | awk 'NR > 1 { sub(/^[[:space:]]*/, ""); sub(/ \(compatibility version.*$/, ""); print }')
done

validate_sdk "$staging_dir"
[[ ! -e "$SDK_DIR" && ! -L "$SDK_DIR" ]] || rm -rf "$SDK_DIR"
mv "$staging_dir" "$SDK_DIR"
staging_dir=""
echo "[ffmpeg-sdk] Prepared self-contained FFmpeg $FFMPEG_VERSION SDK: $SDK_DIR"
