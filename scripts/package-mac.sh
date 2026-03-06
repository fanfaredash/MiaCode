#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
QT_ROOT="${QT_ROOT:-}"
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
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist/MiaCode-v${VERSION}-portable-macos}"

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

cat >"$DIST_DIR/docs/PORTABLE_README.txt" <<'EOF'
MiaCode portable package (macOS)

Run:
  Open MiaCode.app

Included:
  - MiaCode.app
  - Qt frameworks/plugins deployed by macdeployqt
  - assets/
  - docs/
EOF

macdeployqt "$DIST_DIR/MiaCode.app" -always-overwrite

ZIP_PATH="${DIST_DIR}.zip"
rm -f "$ZIP_PATH"
(
  cd "$(dirname "$DIST_DIR")"
  ditto -c -k --sequesterRsrc --keepParent "$(basename "$DIST_DIR")" "$(basename "$ZIP_PATH")"
)

echo "Packaged to $DIST_DIR"
echo "Zip created: $ZIP_PATH"
