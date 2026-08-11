#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
QT_ROOT="${QT_ROOT:-}"
# Default matches the prebuilt Qt 6.10 macOS floor (its frameworks are built
# with minos 13.0). Without this the app inherits the build machine's SDK as
# its minimum OS and won't launch on older systems.
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-13.0}"
BUILD_DEV_TOOLS="${MIACODE_BUILD_DEV_TOOLS:-OFF}"
MACOS_CODESIGN_IDENTITY="${MACOS_CODESIGN_IDENTITY:--}"
PACKAGE_ARCHITECTURES="${CMAKE_OSX_ARCHITECTURES:-arm64}"
THIN_SINGLE_ARCH_PACKAGE="${MIACODE_THIN_MACOS_APP:-ON}"
MIACODE_FFMPEG_DEV_DIR="${MIACODE_FFMPEG_DEV_DIR:-}"
if [[ -z "$QT_ROOT" && -n "${QT_ROOT_DIR:-}" ]]; then
  QT_ROOT="$QT_ROOT_DIR"
fi
case "$THIN_SINGLE_ARCH_PACKAGE" in
  ON|OFF)
    ;;
  *)
    echo "MIACODE_THIN_MACOS_APP must be ON or OFF (got: $THIN_SINGLE_ARCH_PACKAGE)" >&2
    exit 2
    ;;
esac
if [[ "$PACKAGE_ARCHITECTURES" != "arm64" ]]; then
  echo "MiaCode for macOS is arm64-only (got CMAKE_OSX_ARCHITECTURES=$PACKAGE_ARCHITECTURES)." >&2
  exit 2
fi

parse_version() {
  local cmake_file="$1"
  local major minor patch prerelease version
  major="$(grep -Eo 'set\(MIACODE_VERSION_MAJOR\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  minor="$(grep -Eo 'set\(MIACODE_VERSION_MINOR\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  patch="$(grep -Eo 'set\(MIACODE_VERSION_PATCH\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  if [[ -z "$major" || -z "$minor" || -z "$patch" ]]; then
    echo "Failed to parse MIACODE_VERSION_* from $cmake_file" >&2
    exit 1
  fi
  prerelease="$(grep -Eo 'set\(MIACODE_VERSION_PRERELEASE\s+"[^"]*"' "$cmake_file" | sed -E 's/.*"([^"]*)"/\1/' || true)"
  version="${major}.${minor}.${patch}"
  if [[ -n "$prerelease" ]]; then
    version="${version}-${prerelease}"
  fi
  echo "$version"
}

VERSION="$(parse_version "$ROOT_DIR/CMakeLists.txt")"
MACOS_PACKAGE_SUFFIX="macos-apple-silicon"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist/MiaCode-v${VERSION}-${MACOS_PACKAGE_SUFFIX}}"

version_gt() {
  local i
  local lhs rhs
  local lhs_parts=(${1//./ })
  local rhs_parts=(${2//./ })
  local limit=${#lhs_parts[@]}
  if (( ${#rhs_parts[@]} > limit )); then
    limit=${#rhs_parts[@]}
  fi
  for ((i=0; i<limit; i++)); do
    lhs=${lhs_parts[i]:-0}
    rhs=${rhs_parts[i]:-0}
    if ((10#$lhs > 10#$rhs)); then
      return 0
    fi
    if ((10#$lhs < 10#$rhs)); then
      return 1
    fi
  done
  return 1
}

extract_macos_minos() {
  local binary_path="$1"
  otool -l "$binary_path" | awk '
    $1 == "cmd" && ($2 == "LC_BUILD_VERSION" || $2 == "LC_VERSION_MIN_MACOSX") {
      cmd = $2
      next
    }
    cmd == "LC_BUILD_VERSION" && $1 == "minos" {
      print $2
      exit
    }
    cmd == "LC_VERSION_MIN_MACOSX" && $1 == "version" {
      print $2
      exit
    }
  '
}

validate_minos() {
  local binary_path="$1"
  local expected_target="$2"
  local actual_target
  if [[ ! -f "$binary_path" ]]; then
    echo "Missing binary for minOS validation: $binary_path" >&2
    exit 1
  fi
  actual_target="$(extract_macos_minos "$binary_path")"
  if [[ -z "$actual_target" ]]; then
    echo "Failed to determine minimum macOS version for $binary_path" >&2
    exit 1
  fi
  if version_gt "$actual_target" "$expected_target"; then
    echo "Binary requires macOS $actual_target, which exceeds requested target $expected_target: $binary_path" >&2
    exit 1
  fi
}

resolve_macos_ffmpeg_dev_dir() {
  local formula candidate
  if [[ -n "$MIACODE_FFMPEG_DEV_DIR" ]]; then
    if [[ -d "$MIACODE_FFMPEG_DEV_DIR/include/libavcodec" && -d "$MIACODE_FFMPEG_DEV_DIR/lib" ]]; then
      return
    fi
    echo "MIACODE_FFMPEG_DEV_DIR is not an FFmpeg development SDK: $MIACODE_FFMPEG_DEV_DIR" >&2
    exit 1
  fi

  if command -v brew >/dev/null 2>&1; then
    # ffmpeg@6 is the validated development SDK; current ffmpeg is accepted
    # when it is the only installed Homebrew formula.
    for formula in ffmpeg@6 ffmpeg; do
      candidate="$(brew --prefix "$formula" 2>/dev/null || true)"
      if [[ -d "$candidate/include/libavcodec" && -d "$candidate/lib" ]]; then
        MIACODE_FFMPEG_DEV_DIR="$candidate"
        return
      fi
    done
  fi

  echo "Missing macOS FFmpeg development SDK for the QtAVPlayer preview backend." >&2
  echo "Install Homebrew ffmpeg@6, or set MIACODE_FFMPEG_DEV_DIR to a root containing include/ and lib/." >&2
  exit 1
}

array_contains() {
  local needle="$1"
  shift
  local item
  for item in "$@"; do
    if [[ "$item" == "$needle" ]]; then
      return 0
    fi
  done
  return 1
}

mach_o_dependencies() {
  local binary_path="$1"
  otool -L "$binary_path" 2>/dev/null | awk '
    NR > 1 {
      sub(/^[[:space:]]*/, "")
      sub(/ \(compatibility version.*$/, "")
      print
    }
  '
}

mach_o_rpaths() {
  local binary_path="$1"
  otool -l "$binary_path" 2>/dev/null | awk '
    $1 == "cmd" && $2 == "LC_RPATH" {
      want_path = 1
      next
    }
    want_path && $1 == "path" {
      print $2
      want_path = 0
    }
  '
}

is_external_dylib() {
  local dependency="$1"
  case "$dependency" in
    /System/Library/*|/usr/lib/*|@rpath/*|@loader_path/*|@executable_path/*|*.framework/*)
      return 1
      ;;
  esac
  [[ "$dependency" == *.dylib ]]
}

# A relocatable SDK can use @rpath install names instead of Homebrew's usual
# absolute paths. Resolve only names provided by the selected FFmpeg SDK; Qt
# and BASS @rpath dependencies are already handled by macdeployqt/the bundle.
resolve_ffmpeg_dylib_source() {
  local dependency="$1"
  local sdk_candidate
  case "$dependency" in
    @rpath/*.dylib)
      sdk_candidate="$MIACODE_FFMPEG_DEV_DIR/lib/$(basename "$dependency")"
      if [[ -f "$sdk_candidate" ]]; then
        printf '%s\n' "$sdk_candidate"
        return 0
      fi
      return 1
      ;;
  esac
  if is_external_dylib "$dependency" && [[ -f "$dependency" ]]; then
    printf '%s\n' "$dependency"
    return 0
  fi
  return 1
}

# Homebrew FFmpeg dylibs encode absolute install names. Copy their complete
# non-system dependency closure into the app and change every reference to the
# app's Frameworks rpath before signing the bundle.
bundle_ffmpeg_dylib_closure() {
  local app_path="$1"
  local app_binary="$app_path/Contents/MacOS/MiaCode"
  local frameworks_dir="$app_path/Contents/Frameworks"
  local dependency source_path destination library_name macho_path
  local -a pending_dependencies=()
  local -a scanned_dependencies=()
  local -a rewrite_sources=()
  local -a bundled_library_names=()

  if [[ ! -f "$app_binary" ]]; then
    echo "Missing MiaCode executable while bundling FFmpeg dylibs: $app_binary" >&2
    exit 1
  fi
  mkdir -p "$frameworks_dir"

  while IFS= read -r dependency; do
    source_path="$(resolve_ffmpeg_dylib_source "$dependency" || true)"
    if [[ -n "$source_path" ]]; then
      pending_dependencies+=("$source_path")
      if is_external_dylib "$dependency"; then
        rewrite_sources+=("$dependency")
      fi
    elif is_external_dylib "$dependency"; then
      echo "Missing external dylib required by FFmpeg preview decode: $dependency" >&2
      exit 1
    fi
  done < <(mach_o_dependencies "$app_binary")
  if [[ ${#pending_dependencies[@]} -eq 0 ]]; then
    echo "MiaCode does not expose an external FFmpeg dylib dependency to bundle." >&2
    exit 1
  fi

  while [[ ${#pending_dependencies[@]} -gt 0 ]]; do
    dependency="${pending_dependencies[0]}"
    pending_dependencies=("${pending_dependencies[@]:1}")
    if array_contains "$dependency" "${scanned_dependencies[@]}"; then
      continue
    fi
    scanned_dependencies+=("$dependency")
    if [[ ! -f "$dependency" ]]; then
      echo "Missing external dylib required by FFmpeg preview decode: $dependency" >&2
      exit 1
    fi

    library_name="$(basename "$dependency")"
    destination="$frameworks_dir/$library_name"
    if [[ -e "$destination" ]]; then
      if ! cmp -s "$dependency" "$destination"; then
        echo "Dylib basename collision while bundling FFmpeg: $dependency and $destination" >&2
        exit 1
      fi
    else
      cp -L "$dependency" "$destination"
    fi
    if ! array_contains "$library_name" "${bundled_library_names[@]}"; then
      bundled_library_names+=("$library_name")
    fi

    while IFS= read -r dependency; do
      source_path="$(resolve_ffmpeg_dylib_source "$dependency" || true)"
      if [[ -z "$source_path" ]]; then
        if is_external_dylib "$dependency"; then
          echo "Missing external dylib required by FFmpeg preview decode: $dependency" >&2
          exit 1
        fi
        continue
      fi
      if is_external_dylib "$dependency" \
          && ! array_contains "$dependency" "${rewrite_sources[@]}"; then
        rewrite_sources+=("$dependency")
      fi
      if ! array_contains "$source_path" "${scanned_dependencies[@]}"; then
        pending_dependencies+=("$source_path")
      fi
    done < <(mach_o_dependencies "$destination")
  done

  for library_name in "${bundled_library_names[@]}"; do
    install_name_tool -id "@rpath/$library_name" "$frameworks_dir/$library_name"
  done

  while IFS= read -r -d '' macho_path; do
    if ! otool -L "$macho_path" >/dev/null 2>&1; then
      continue
    fi
    for dependency in "${rewrite_sources[@]}"; do
      if mach_o_dependencies "$macho_path" | grep -Fqx "$dependency"; then
        install_name_tool -change "$dependency" "@rpath/$(basename "$dependency")" "$macho_path"
      fi
    done
  done < <(find "$app_path/Contents" -type f -print0)
}

strip_ffmpeg_build_rpath() {
  local app_path="$1"
  local app_binary="$app_path/Contents/MacOS/MiaCode"
  local rpath

  while IFS= read -r rpath; do
    if [[ "$rpath" == "$MIACODE_FFMPEG_DEV_DIR/lib" ]]; then
      install_name_tool -delete_rpath "$rpath" "$app_binary"
    fi
  done < <(mach_o_rpaths "$app_binary")
}

verify_no_external_ffmpeg_dylib_references() {
  local app_path="$1"
  local app_binary="$app_path/Contents/MacOS/MiaCode"
  local dependency macho_path rpath
  local verification_failed=0

  while IFS= read -r -d '' macho_path; do
    if ! otool -L "$macho_path" >/dev/null 2>&1; then
      continue
    fi
    while IFS= read -r dependency; do
      if is_external_dylib "$dependency"; then
        echo "Unbundled external dylib remains in $macho_path: $dependency" >&2
        verification_failed=1
      fi
    done < <(mach_o_dependencies "$macho_path")
  done < <(find "$app_path/Contents" -type f -print0)

  while IFS= read -r rpath; do
    if [[ "$rpath" == "$MIACODE_FFMPEG_DEV_DIR/lib" ]]; then
      echo "FFmpeg SDK build rpath remains in $app_binary: $rpath" >&2
      verification_failed=1
    fi
  done < <(mach_o_rpaths "$app_binary")

  return "$verification_failed"
}

validate_bundled_ffmpeg_minos() {
  local app_path="$1"
  local expected_target="$2"
  local dylib_path

  while IFS= read -r -d '' dylib_path; do
    validate_minos "$dylib_path" "$expected_target"
  done < <(find "$app_path/Contents/Frameworks" -type f -name '*.dylib' -print0)
}

if [[ -n "$QT_ROOT" ]]; then
  export PATH="$QT_ROOT/bin:$PATH"
  export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-$QT_ROOT}"
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found in PATH" >&2
  exit 1
fi

if ! command -v macdeployqt >/dev/null 2>&1; then
  echo "macdeployqt not found in PATH (set QT_ROOT=/path/to/Qt/6.8.x/macos)" >&2
  exit 1
fi

resolve_macos_ffmpeg_dev_dir

cmake_args=(
  -S "$ROOT_DIR"
  -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE=Release
  "-DMIACODE_BUILD_DEV_TOOLS=$BUILD_DEV_TOOLS"
  "-DMIACODE_FFMPEG_DEV_DIR=$MIACODE_FFMPEG_DEV_DIR"
)
if [[ -n "$DEPLOYMENT_TARGET" ]]; then
  cmake_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET")
fi
if [[ -n "$PACKAGE_ARCHITECTURES" ]]; then
  cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=$PACKAGE_ARCHITECTURES")
fi
cmake "${cmake_args[@]}"
build_args=(--build "$BUILD_DIR" --config Release --parallel 2)
if [[ "$BUILD_DEV_TOOLS" == "ON" ]]; then
  build_args+=(--target MiaCode simai_native_dump soundtouch_probe)
else
  build_args+=(--target MiaCode)
fi
cmake "${build_args[@]}"

APP_PATH="$BUILD_DIR/MiaCode.app"
if [[ ! -d "$APP_PATH" ]]; then
  APP_PATH="$BUILD_DIR/Release/MiaCode.app"
fi
if [[ ! -d "$APP_PATH" ]]; then
  echo "MiaCode.app not found under $BUILD_DIR" >&2
  exit 1
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/docs"
cp -R "$APP_PATH" "$DIST_DIR/"

for release_doc in LICENSE LICENSE_SCOPE.md THIRD_PARTY_NOTICES.md README.md README_EN.md; do
  if [[ ! -f "$ROOT_DIR/$release_doc" ]]; then
    echo "Missing release documentation file: $ROOT_DIR/$release_doc" >&2
    exit 1
  fi
  cp "$ROOT_DIR/$release_doc" "$DIST_DIR/$release_doc"
done

if [[ -d "$ROOT_DIR/licenses" ]]; then
  cp -R "$ROOT_DIR/licenses" "$DIST_DIR/licenses"
else
  echo "Missing licenses dir: $ROOT_DIR/licenses" >&2
  exit 1
fi

if [[ "$BUILD_DEV_TOOLS" == "ON" ]]; then
  for helper_bin in simai_native_dump soundtouch_probe; do
    helper_path="$BUILD_DIR/$helper_bin"
    if [[ ! -f "$helper_path" && -f "$BUILD_DIR/Release/$helper_bin" ]]; then
      helper_path="$BUILD_DIR/Release/$helper_bin"
    fi
    if [[ -f "$helper_path" ]]; then
      cp "$helper_path" "$DIST_DIR/$helper_bin"
    fi
  done
fi

if [[ -d "$ROOT_DIR/assets" ]]; then
  required_sfx_dir="$ROOT_DIR/assets/SFX"
required_sfx_files=(
  "answer.wav"
  "break_tap.wav"
  "slide.wav"
  "break.wav"
  "break_slide.wav"
  "slide_break_start.wav"
  "slide_break_slide.wav"
  "tap_ex.wav"
  "tap_perfect.wav"
  "touch_hanabi.wav"
  "touch.wav"
  "touch_Hold_riser.wav"
  "clock.wav"
)
  for sfx_file in "${required_sfx_files[@]}"; do
    if [[ ! -f "$required_sfx_dir/$sfx_file" ]]; then
      echo "Missing required SFX asset: $required_sfx_dir/$sfx_file" >&2
      exit 1
    fi
  done
  # Assets ship ONLY inside the bundle (Contents/Resources/assets). That is
  # the FIRST candidate in AssetPaths.h::findAssetRoot(), so a loose
  # $DIST_DIR/assets copy would never be read — it just doubles the package
  # size and misleads users into editing files the app ignores.
  bundle_assets_dir="$DIST_DIR/MiaCode.app/Contents/Resources/assets"
  rm -rf "$bundle_assets_dir"
  mkdir -p "$(dirname "$bundle_assets_dir")"
  cp -R "$ROOT_DIR/assets" "$bundle_assets_dir"
  # slide_data.json + the bundled fonts are embedded in the binary via qrc
  # (:/data/slide_data.json, :/fonts/*); the loose copies are never read. Drop
  # them so the package does not ship ~17 MB twice.
  for redundant in reference fonts; do
    rm -rf "$bundle_assets_dir/$redundant"
  done
fi

ffmpeg_src="$ROOT_DIR/third_party/ffmpeg/macos/ffmpeg"
if [[ -f "$ffmpeg_src" && -s "$ffmpeg_src" ]]; then
  ffmpeg_bin_dir="$DIST_DIR/MiaCode.app/Contents/MacOS/ffmpeg"
  # A locally runnable app bundle can already contain a direct `ffmpeg` file
  # beside MiaCode. Release packages use the documented `ffmpeg/ffmpeg`
  # layout; normalize the copied bundle before creating that directory.
  if [[ -e "$ffmpeg_bin_dir" || -L "$ffmpeg_bin_dir" ]]; then
    if [[ ! -d "$ffmpeg_bin_dir" ]]; then
      rm -f "$ffmpeg_bin_dir"
    fi
  fi
  mkdir -p "$ffmpeg_bin_dir"
  cp "$ffmpeg_src" "$ffmpeg_bin_dir/ffmpeg"
  chmod +x "$ffmpeg_bin_dir/ffmpeg"
  if [[ ! -x "$ffmpeg_bin_dir/ffmpeg" ]]; then
    echo "ffmpeg copy failed or not executable: $ffmpeg_bin_dir/ffmpeg" >&2
    exit 1
  fi
else
  echo "Missing required ffmpeg binary: $ffmpeg_src" >&2
  echo "Run bash scripts/ffmpeg/ensure-macos-ffmpeg.sh to download the pinned macOS ffmpeg binary." >&2
  exit 1
fi

for doc in docs/ops/DEBUG_INDEX.md docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    cp "$ROOT_DIR/$doc" "$DIST_DIR/docs/$(basename "$doc")"
  fi
done

cat >"$DIST_DIR/docs/RELEASE_README.txt" <<'EOF'
MiaCode release package (macOS)

Run:
  Open MiaCode.app

Included:
  - MiaCode.app
  - Qt frameworks/plugins deployed by macdeployqt
  - BASS, BASSmix, BASS FX, and BASSOPUS arm64 runtime libraries
  - MiaCode.app/Contents/MacOS/ffmpeg/ffmpeg
  - assets (inside MiaCode.app/Contents/Resources/assets)
  - docs/
EOF

if [[ "$BUILD_DEV_TOOLS" != "ON" ]]; then
  cat >>"$DIST_DIR/docs/RELEASE_README.txt" <<'EOF'

Not included on purpose:
  - simai_native_dump
  - soundtouch_probe
EOF
else
  cat >>"$DIST_DIR/docs/RELEASE_README.txt" <<'EOF'
  - simai_native_dump
  - soundtouch_probe
EOF
fi

macdeployqt "$DIST_DIR/MiaCode.app" -qmldir="$ROOT_DIR/src" -always-overwrite

# MiaCode does not use Qt SQL. macdeployqt still deploys the sqldrivers
# plugins, and the odbc/psql/mimer ones reference third-party dylibs
# (homebrew libiodbc, Postgres.app, mimer) that only exist on the build
# machine — dead weight that also breaks strict signature/dependency scans.
rm -rf "$DIST_DIR/MiaCode.app/Contents/PlugIns/sqldrivers"

bundle_ffmpeg_dylib_closure "$DIST_DIR/MiaCode.app"
strip_ffmpeg_build_rpath "$DIST_DIR/MiaCode.app"

# macdeployqt copies universal Qt frameworks/plugins even when MiaCode itself is
# a single-architecture executable. For a package explicitly configured as
# arm64 or x86_64, keep only that matching slice. The helper hard-fails on any
# Mach-O that lacks the target architecture, preventing an x86-only Rosetta
# helper or another incompatible binary from being silently damaged.
if [[ "$THIN_SINGLE_ARCH_PACKAGE" == "ON" ]]; then
  "$ROOT_DIR/scripts/build/thin-macos-app.sh" \
    "$DIST_DIR/MiaCode.app" "arm64"
fi

verify_no_external_ffmpeg_dylib_references "$DIST_DIR/MiaCode.app"

bass_frameworks_dir="$DIST_DIR/MiaCode.app/Contents/Frameworks"
required_bass_libraries=(
  "libbass.dylib"
  "libbassmix.dylib"
  "libbass_fx.dylib"
  "libbassopus.dylib"
)
for bass_library in "${required_bass_libraries[@]}"; do
  bass_path="$bass_frameworks_dir/$bass_library"
  if [[ ! -f "$bass_path" ]]; then
    echo "Missing packaged macOS BASS runtime: $bass_path" >&2
    exit 1
  fi
  if [[ "$(lipo -archs "$bass_path")" != "arm64" ]]; then
    echo "Packaged BASS runtime is not arm64-only: $bass_path" >&2
    exit 1
  fi
done

# macdeployqt and architecture thinning rewrite Mach-O files and invalidate
# bundled signatures. Re-sign only after both operations finish, then verify the
# completed bundle before it can be archived.
if [[ -n "$MACOS_CODESIGN_IDENTITY" ]]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "codesign not found in PATH" >&2
    exit 1
  fi
  codesign --force --deep --sign "$MACOS_CODESIGN_IDENTITY" "$DIST_DIR/MiaCode.app"
  codesign --verify --deep --strict --verbose=2 "$DIST_DIR/MiaCode.app"
fi

if [[ -n "$DEPLOYMENT_TARGET" ]]; then
  validate_minos "$DIST_DIR/MiaCode.app/Contents/MacOS/MiaCode" "$DEPLOYMENT_TARGET"
  validate_minos "$DIST_DIR/MiaCode.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore" "$DEPLOYMENT_TARGET"
  validate_bundled_ffmpeg_minos "$DIST_DIR/MiaCode.app" "$DEPLOYMENT_TARGET"
  for bass_library in "${required_bass_libraries[@]}"; do
    validate_minos "$bass_frameworks_dir/$bass_library" "$DEPLOYMENT_TARGET"
  done
fi

ZIP_PATH="${DIST_DIR}.zip"
rm -f "$ZIP_PATH"
(
  cd "$(dirname "$DIST_DIR")"
  ditto -c -k --sequesterRsrc --keepParent "$(basename "$DIST_DIR")" "$(basename "$ZIP_PATH")"
)

echo "Packaged to $DIST_DIR"
echo "Zip created: $ZIP_PATH"
