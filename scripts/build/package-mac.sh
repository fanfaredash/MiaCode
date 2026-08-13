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

package_step() {
  printf '\n==> [package] %s\n' "$*"
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
  local library
  local -a required_libraries=(
    "libavcodec.60.dylib"
    "libavfilter.9.dylib"
    "libavformat.60.dylib"
    "libavutil.58.dylib"
    "libswresample.4.dylib"
    "libswscale.7.dylib"
  )

  if [[ -z "$MIACODE_FFMPEG_DEV_DIR" ]]; then
    MIACODE_FFMPEG_DEV_DIR="$ROOT_DIR/third_party/ffmpeg/macos/dev"
  fi
  if [[ "$MIACODE_FFMPEG_DEV_DIR" == /opt/homebrew/* || "$MIACODE_FFMPEG_DEV_DIR" == /usr/local/* ]]; then
    echo "macOS packaging requires the project-provisioned FFmpeg SDK, not a system package manager: $MIACODE_FFMPEG_DEV_DIR" >&2
    exit 1
  fi
  if [[ ! -d "$MIACODE_FFMPEG_DEV_DIR/include/libavcodec" || ! -d "$MIACODE_FFMPEG_DEV_DIR/lib" ]]; then
    echo "Missing macOS FFmpeg development SDK: $MIACODE_FFMPEG_DEV_DIR" >&2
    echo "Run: bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh" >&2
    exit 1
  fi
  for library in "${required_libraries[@]}"; do
    if [[ ! -f "$MIACODE_FFMPEG_DEV_DIR/lib/$library" ]]; then
      echo "macOS FFmpeg SDK is missing required library: $MIACODE_FFMPEG_DEV_DIR/lib/$library" >&2
      exit 1
    fi
  done
}

mach_o_install_name() {
  local binary_path="$1"
  otool -D "$binary_path" 2>/dev/null | awk 'NR == 2 { print; exit }'
}

mach_o_dependencies() {
  local binary_path="$1"
  local install_name
  install_name="$(mach_o_install_name "$binary_path")"
  otool -L "$binary_path" 2>/dev/null | awk '
    NR > 1 {
      sub(/^[[:space:]]*/, "")
      sub(/ \(compatibility version.*$/, "")
      print
    }
  ' | awk -v install_name="$install_name" '$0 != install_name'
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

mach_o_files() {
  local app_path="$1"

  # Scanning every packaged asset with otool makes Qt deployment look hung: a
  # QML-heavy bundle contains thousands of non-Mach-O files. Every dylib and
  # every executable bit is a possible dependency carrier, which covers the
  # app executable, framework binaries, plugins, helpers, and extensions.
  find "$app_path/Contents" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -111 \) -print0
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

stage_macos_ffmpeg_runtime() {
  local app_path="$1"
  local frameworks_dir="$app_path/Contents/Frameworks"
  local app_binary="$app_path/Contents/MacOS/MiaCode"
  local library source_path destination_path macho_path dependency dependency_base
  local -a required_libraries=(
    "libavcodec.60.dylib"
    "libavfilter.9.dylib"
    "libavformat.60.dylib"
    "libavutil.58.dylib"
    "libswresample.4.dylib"
    "libswscale.7.dylib"
  )

  if [[ ! -d "$frameworks_dir" ]]; then
    echo "Missing Frameworks directory while staging FFmpeg: $frameworks_dir" >&2
    return 1
  fi
  if [[ ! -f "$app_binary" ]]; then
    echo "Missing MiaCode executable while staging FFmpeg: $app_binary" >&2
    return 1
  fi

  for library in "${required_libraries[@]}"; do
    source_path="$MIACODE_FFMPEG_DEV_DIR/lib/$library"
    destination_path="$frameworks_dir/$library"
    cp -L "$source_path" "$destination_path"
    install_name_tool -id "@rpath/$library" "$destination_path"
  done

  while IFS= read -r -d '' macho_path; do
    while IFS= read -r dependency; do
      dependency_base="$(basename "$dependency")"
      for library in "${required_libraries[@]}"; do
        if [[ "$dependency_base" == "$library" && "$dependency" != "@rpath/$library" ]]; then
          install_name_tool -change "$dependency" "@rpath/$library" "$macho_path"
        fi
      done
    done < <(mach_o_dependencies "$macho_path")
  done < <(mach_o_files "$app_path")

  if ! mach_o_rpaths "$app_binary" | grep -Fxq '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$app_binary"
  fi
}

strip_absolute_build_rpaths() {
  local binary_path="$1"
  local rpath

  while IFS= read -r rpath; do
    if [[ "$rpath" == /* ]]; then
      install_name_tool -delete_rpath "$rpath" "$binary_path"
    fi
  done < <(mach_o_rpaths "$binary_path")
}

verify_loader_path_dependencies_stay_in_bundle() {
  local app_path="$1"
  local macho_path dependency loader_relative resolved_directory resolved_path
  local verification_failed=0

  while IFS= read -r -d '' macho_path; do
    while IFS= read -r dependency; do
      case "$dependency" in
        @loader_path/*)
          loader_relative="${dependency#@loader_path/}"
          resolved_directory="$(cd "$(dirname "$macho_path")" && cd "$(dirname "$loader_relative")" 2>/dev/null && pwd)"
          if [[ -z "$resolved_directory" ]]; then
            echo "Unresolvable @loader_path dependency remains in $macho_path: $dependency" >&2
            verification_failed=1
            continue
          fi
          resolved_path="$resolved_directory/$(basename "$loader_relative")"
          if [[ "$resolved_path" != "$app_path"/* || ! -f "$resolved_path" ]]; then
            echo "@loader_path dependency escapes the app bundle in $macho_path: $dependency" >&2
            verification_failed=1
          fi
          ;;
      esac
    done < <(mach_o_dependencies "$macho_path")
  done < <(mach_o_files "$app_path")

  return "$verification_failed"
}

remove_qt_ffmpeg_backend() {
  local app_path="$1"
  local plugin_path="$app_path/Contents/PlugIns/multimedia/libffmpegmediaplugin.dylib"
  local frameworks_dir="$app_path/Contents/Frameworks"
  local library macho_path dependencies
  local referenced=0
  local -a qt_ffmpeg_libraries=(
    "libavcodec.61.dylib"
    "libavfilter.10.dylib"
    "libavformat.61.dylib"
    "libavutil.59.dylib"
    "libswresample.5.dylib"
    "libswscale.8.dylib"
  )

  # macOS preview uses QtAVPlayer and FFmpeg 6. QVideoFrame, VideoOutput, and
  # QMediaDevices still require Qt Multimedia, but not Qt's parallel FFmpeg 7
  # backend. Keep libdarwinmediaplugin.dylib for the native device backend.
  rm -f "$plugin_path"

  while IFS= read -r -d '' macho_path; do
    case "$macho_path" in
      "$frameworks_dir"/libavcodec.61.dylib|"$frameworks_dir"/libavfilter.10.dylib|"$frameworks_dir"/libavformat.61.dylib|"$frameworks_dir"/libavutil.59.dylib|"$frameworks_dir"/libswresample.5.dylib|"$frameworks_dir"/libswscale.8.dylib)
        # The Qt FFmpeg runtime libraries may reference one another, but are
        # removed as one unit. Qt 6.10.2 does not stage avfilter.10; include
        # it defensively so a future Qt deployment cannot reintroduce it.
        continue
        ;;
    esac
    dependencies="$(mach_o_dependencies "$macho_path")"
    for library in "${qt_ffmpeg_libraries[@]}"; do
      if printf '%s\n' "$dependencies" | grep -Fq "$library"; then
        echo "Qt FFmpeg runtime remains referenced by $macho_path: $library" >&2
        referenced=1
      fi
    done
  done < <(mach_o_files "$app_path")
  if (( referenced )); then
    return 1
  fi

  for library in "${qt_ffmpeg_libraries[@]}"; do
    rm -f "$frameworks_dir/$library"
  done
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
  done < <(mach_o_files "$app_path")

  verify_loader_path_dependencies_stay_in_bundle "$app_path" || verification_failed=1

  while IFS= read -r rpath; do
    if [[ "$rpath" == /* ]]; then
      echo "Absolute build rpath remains in $app_binary: $rpath" >&2
      verification_failed=1
    fi
  done < <(mach_o_rpaths "$app_binary")
  if ! mach_o_rpaths "$app_binary" | grep -Fxq '@executable_path/../Frameworks'; then
    echo "MiaCode is missing the bundle Frameworks rpath: $app_binary" >&2
    verification_failed=1
  fi

  return "$verification_failed"
}

validate_bundled_ffmpeg_minos() {
  local app_path="$1"
  local expected_target="$2"
  local library_path library
  local -a required_libraries=(
    "libavcodec.60.dylib"
    "libavfilter.9.dylib"
    "libavformat.60.dylib"
    "libavutil.58.dylib"
    "libswresample.4.dylib"
    "libswscale.7.dylib"
  )

  for library in "${required_libraries[@]}"; do
    library_path="$app_path/Contents/Frameworks/$library"
    validate_minos "$library_path" "$expected_target"
  done
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

package_step "Resolving the FFmpeg development SDK"
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
package_step "Configuring Release build in $BUILD_DIR"
cmake "${cmake_args[@]}"
# Keep the complete build graph at four jobs or fewer. This cap is deliberate:
# release packaging must not saturate the local machine with compiler processes.
build_args=(--build "$BUILD_DIR" --config Release --parallel 4)
if [[ "$BUILD_DEV_TOOLS" == "ON" ]]; then
  build_args+=(--target MiaCode simai_native_dump soundtouch_probe)
else
  build_args+=(--target MiaCode)
fi
package_step "Building MiaCode (at most 4 concurrent jobs)"
cmake "${build_args[@]}"

APP_PATH="$BUILD_DIR/MiaCode.app"
if [[ ! -d "$APP_PATH" ]]; then
  APP_PATH="$BUILD_DIR/Release/MiaCode.app"
fi
if [[ ! -d "$APP_PATH" ]]; then
  echo "MiaCode.app not found under $BUILD_DIR" >&2
  exit 1
fi

package_step "Staging app bundle, documentation, assets, and runtime tools"
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
  - Start_MiaCode_Debug.command (runs MiaCode in debug mode; logs go to ./logs/)
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

debug_launcher_source="$ROOT_DIR/scripts/debug/Start_MiaCode_Debug.command"
debug_launcher_path="$DIST_DIR/Start_MiaCode_Debug.command"
if [[ ! -f "$debug_launcher_source" ]]; then
  echo "Missing macOS debug launcher source: $debug_launcher_source" >&2
  exit 1
fi
cp "$debug_launcher_source" "$debug_launcher_path"
chmod +x "$debug_launcher_path"
if [[ ! -x "$debug_launcher_path" ]]; then
  echo "Packaged macOS debug launcher is missing or not executable: $debug_launcher_path" >&2
  exit 1
fi

package_step "Deploying Qt frameworks, plugins, and QML imports (macdeployqt may be quiet for several minutes)"
macdeployqt "$DIST_DIR/MiaCode.app" -qmldir="$ROOT_DIR/src" -always-overwrite
package_step "Removing non-release Qt helper components"

# Qt Multimedia can pull Homebrew's dynamically linked ffprobe into the bundle.
# MiaCode does not invoke it; export uses the pinned static ffmpeg below. Leaving
# it in place would ship build-machine dylib references and fail relocation.
rm -f "$DIST_DIR/MiaCode.app/Contents/MacOS/ffprobe"

# MiaCode does not use Qt SQL. macdeployqt still deploys the sqldrivers
# plugins, and the odbc/psql/mimer ones reference third-party dylibs
# (homebrew libiodbc, Postgres.app, mimer) that only exist on the build
# machine — dead weight that also breaks strict signature/dependency scans.
rm -rf "$DIST_DIR/MiaCode.app/Contents/PlugIns/sqldrivers"

package_step "Removing the unused Qt FFmpeg 7 media backend"
remove_qt_ffmpeg_backend "$DIST_DIR/MiaCode.app"

package_step "Staging the self-contained FFmpeg 6 preview runtime"
stage_macos_ffmpeg_runtime "$DIST_DIR/MiaCode.app"
strip_absolute_build_rpaths "$DIST_DIR/MiaCode.app/Contents/MacOS/MiaCode"

# macdeployqt copies universal Qt frameworks/plugins even when MiaCode itself is
# a single-architecture executable. For a package explicitly configured as
# arm64 or x86_64, keep only that matching slice. The helper hard-fails on any
# Mach-O that lacks the target architecture, preventing an x86-only Rosetta
# helper or another incompatible binary from being silently damaged.
if [[ "$THIN_SINGLE_ARCH_PACKAGE" == "ON" ]]; then
  package_step "Thinning all bundled Mach-O files to arm64"
  "$ROOT_DIR/scripts/build/thin-macos-app.sh" \
    "$DIST_DIR/MiaCode.app" "arm64"
fi

package_step "Verifying there are no build-machine dylib references"
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
  package_step "Signing the completed app bundle"
  if ! command -v codesign >/dev/null 2>&1; then
    echo "codesign not found in PATH" >&2
    exit 1
  fi
  codesign --force --deep --sign "$MACOS_CODESIGN_IDENTITY" "$DIST_DIR/MiaCode.app"
  codesign --verify --deep --strict --verbose=2 "$DIST_DIR/MiaCode.app"
fi

if [[ -n "$DEPLOYMENT_TARGET" ]]; then
  package_step "Validating minimum macOS version $DEPLOYMENT_TARGET"
  validate_minos "$DIST_DIR/MiaCode.app/Contents/MacOS/MiaCode" "$DEPLOYMENT_TARGET"
  validate_minos "$DIST_DIR/MiaCode.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore" "$DEPLOYMENT_TARGET"
  validate_bundled_ffmpeg_minos "$DIST_DIR/MiaCode.app" "$DEPLOYMENT_TARGET"
  for bass_library in "${required_bass_libraries[@]}"; do
    validate_minos "$bass_frameworks_dir/$bass_library" "$DEPLOYMENT_TARGET"
  done
fi

package_step "Creating ZIP archive"
ZIP_PATH="${DIST_DIR}.zip"
rm -f "$ZIP_PATH"
(
  cd "$(dirname "$DIST_DIR")"
  ditto -c -k --sequesterRsrc --keepParent "$(basename "$DIST_DIR")" "$(basename "$ZIP_PATH")"
)

zip_launcher_path="$(basename "$DIST_DIR")/Start_MiaCode_Debug.command"
zip_launcher_mode="$(zipinfo -l "$ZIP_PATH" "$zip_launcher_path" | awk '$1 ~ /^-[rwx-]+$/ { print $1; exit }')"
if [[ ! "$zip_launcher_mode" =~ ^-..x ]]; then
  echo "ZIP is missing an executable macOS debug launcher: $zip_launcher_path" >&2
  exit 1
fi

if [[ -n "$MACOS_CODESIGN_IDENTITY" ]]; then
  package_step "Verifying the ZIP-extracted app signature"
  zip_verify_dir="$(mktemp -d "${TMPDIR:-/tmp}/miacode-package-verify.XXXXXX")"
  ditto -x -k "$ZIP_PATH" "$zip_verify_dir"
  zip_extracted_app="$zip_verify_dir/$(basename "$DIST_DIR")/MiaCode.app"
  if [[ ! -d "$zip_extracted_app" ]]; then
    echo "ZIP-extracted app is missing: $zip_extracted_app" >&2
    rm -rf "$zip_verify_dir"
    exit 1
  fi
  if ! codesign --verify --deep --strict --verbose=2 "$zip_extracted_app"; then
    rm -rf "$zip_verify_dir"
    exit 1
  fi
  rm -rf "$zip_verify_dir"
fi

package_step "Packaging complete"
echo "Packaged to $DIST_DIR"
echo "Zip created: $ZIP_PATH"
