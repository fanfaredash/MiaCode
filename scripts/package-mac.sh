#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
QT_ROOT="${QT_ROOT:-}"
DEPLOYMENT_TARGET="${CMAKE_OSX_DEPLOYMENT_TARGET:-}"
MACOS_CODESIGN_IDENTITY="${MACOS_CODESIGN_IDENTITY:--}"
if [[ -z "$QT_ROOT" && -n "${QT_ROOT_DIR:-}" ]]; then
  QT_ROOT="$QT_ROOT_DIR"
fi

parse_version() {
  local cmake_file="$1"
  local major minor patch
  major="$(grep -Eo 'set\(MIACODE_VERSION_MAJOR\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  minor="$(grep -Eo 'set\(MIACODE_VERSION_MINOR\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  patch="$(grep -Eo 'set\(MIACODE_VERSION_PATCH\s+"[^"]+"' "$cmake_file" | sed -E 's/.*"([^"]+)"/\1/')"
  if [[ -z "$major" || -z "$minor" || -z "$patch" ]]; then
    echo "Failed to parse MIACODE_VERSION_* from $cmake_file" >&2
    exit 1
  fi
  echo "${major}.${minor}.${patch}"
}

VERSION="$(parse_version "$ROOT_DIR/CMakeLists.txt")"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist/MiaCode-v${VERSION}-macos}"

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

cmake_args=(
  -S "$ROOT_DIR"
  -B "$BUILD_DIR"
  -DCMAKE_BUILD_TYPE=Release
)
if [[ -n "$DEPLOYMENT_TARGET" ]]; then
  cmake_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET")
fi
cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" --config Release

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

if [[ -d "$ROOT_DIR/assets" ]]; then
  required_sfx_dir="$ROOT_DIR/assets/SFX"
  required_sfx_files=(
    "answer.wav"
    "slide.wav"
    "break.wav"
    "judge_ex.wav"
    "touch.wav"
    "touchHold_riser.wav"
  )
  for sfx_file in "${required_sfx_files[@]}"; do
    if [[ ! -f "$required_sfx_dir/$sfx_file" ]]; then
      echo "Missing required SFX asset: $required_sfx_dir/$sfx_file" >&2
      exit 1
    fi
  done
  cp -R "$ROOT_DIR/assets" "$DIST_DIR/assets"
fi

for doc in README.md README_EN.md; do
  if [[ -f "$ROOT_DIR/$doc" ]]; then
    cp "$ROOT_DIR/$doc" "$DIST_DIR/docs/$doc"
  fi
done

cat >"$DIST_DIR/docs/RELEASE_README.txt" <<'EOF'
MiaCode release package (macOS)

Run:
  Open MiaCode.app

Included:
  - MiaCode.app
  - Qt frameworks/plugins deployed by macdeployqt
  - assets/
  - docs/
EOF

macdeployqt "$DIST_DIR/MiaCode.app" -always-overwrite

# macdeployqt may rewrite Qt binaries and invalidate bundled signatures.
# Re-sign the packaged app (ad-hoc by default) so macOS won't kill it at launch.
if [[ -n "$MACOS_CODESIGN_IDENTITY" ]]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "codesign not found in PATH" >&2
    exit 1
  fi
  codesign --force --deep --sign "$MACOS_CODESIGN_IDENTITY" "$DIST_DIR/MiaCode.app"
fi

if [[ -n "$DEPLOYMENT_TARGET" ]]; then
  validate_minos "$DIST_DIR/MiaCode.app/Contents/MacOS/MiaCode" "$DEPLOYMENT_TARGET"
  validate_minos "$DIST_DIR/MiaCode.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore" "$DEPLOYMENT_TARGET"
fi

ZIP_PATH="${DIST_DIR}.zip"
rm -f "$ZIP_PATH"
(
  cd "$(dirname "$DIST_DIR")"
  ditto -c -k --sequesterRsrc --keepParent "$(basename "$DIST_DIR")" "$(basename "$ZIP_PATH")"
)

echo "Packaged to $DIST_DIR"
echo "Zip created: $ZIP_PATH"
