#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_JOBS="${BUILD_JOBS:-8}"
BUILDER_IMAGE="${MIACODE_LINUX_BUILDER_IMAGE:-miacode-linux-builder:ubuntu22.04-qt6.11.1-r3}"
BUILD_ROOT="$ROOT_DIR/build"
CONTAINER_ENGINE="${MIACODE_CONTAINER_ENGINE:-}"

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Required command not found: $1" >&2
    exit 1
  fi
}

if ! [[ "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "BUILD_JOBS must be a positive integer (got: $BUILD_JOBS)" >&2
  exit 2
fi

build_builder_image() {
  echo "Preparing fixed Ubuntu 22.04 / Qt 6.11.1 builder image..."
  "$CONTAINER_ENGINE" build \
    --platform linux/amd64 \
    --tag "$BUILDER_IMAGE" \
    --file - \
    "$ROOT_DIR/scripts/build" <<'CONTAINERFILE'
FROM docker.io/library/ubuntu@sha256:0d779ea97881505f5ef0039336ee85edba27519bdba968c284c86ee066a973c8

ARG DEBIAN_FRONTEND=noninteractive
ARG QT_VERSION=6.11.1
ARG FCITX5_QT_VERSION=5.1.14
ARG FCITX5_QT_COMMIT=46651831dac2520944f573c14214827c0e87d2a6
ARG LINUXDEPLOY_VERSION=1-alpha-20251107-1
ARG LINUXDEPLOY_QT_VERSION=1-alpha-20250213-1

ENV TZ=Etc/UTC
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV QT_ROOT=/opt/Qt/6.11.1/gcc_64
ENV PATH=/opt/Qt/6.11.1/gcc_64/bin:/usr/local/bin:/usr/bin:/bin
ENV APPIMAGE_EXTRACT_AND_RUN=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake curl desktop-file-utils \
    extra-cmake-modules file git libdbus-1-3 libegl1 libegl1-mesa-dev \
    libfontconfig1 libfuse2 libgl1 libgl-dev libice6 libpulse0 libsm6 \
    libx11-6 libx11-dev libx11-xcb1 libxcb-cursor0 libxcb-icccm4 \
    libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
    libxcb-shape0 libxcb-xfixes0 libxcb-xinerama0 libxcb-xkb1 libxcb1-dev \
    libxkbcommon-dev libxkbcommon-x11-0 libxrandr2 libxrender1 ninja-build \
    patchelf python3-pip xz-utils zstd \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir aqtinstall==3.3.0 \
 && aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 \
      --outputdir /opt/Qt \
      --modules qtmultimedia qtshadertools

RUN git clone --branch "${FCITX5_QT_VERSION}" --depth 1 \
      https://github.com/fcitx/fcitx5-qt.git /opt/src/fcitx5-qt \
 && test "$(git -C /opt/src/fcitx5-qt rev-parse HEAD)" = "${FCITX5_QT_COMMIT}" \
 && cmake -S /opt/src/fcitx5-qt -B /opt/build/fcitx5-qt -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="${QT_ROOT}" \
      -DBUILD_ONLY_PLUGIN=ON \
      -DBUILD_STATIC_PLUGIN=OFF \
      -DENABLE_QT4=OFF \
      -DENABLE_QT5=OFF \
      -DENABLE_QT6=ON \
      -DENABLE_X11=ON \
      -DENABLE_QT6_WAYLAND_WORKAROUND=OFF \
 && cmake --build /opt/build/fcitx5-qt --parallel 8 \
 && install -Dm755 \
      /opt/build/fcitx5-qt/qt6/platforminputcontext/libfcitx5platforminputcontextplugin.so \
      /opt/fcitx5-qt6/platforminputcontexts/libfcitx5platforminputcontextplugin.so \
 && rm -rf /opt/src/fcitx5-qt /opt/build/fcitx5-qt

RUN mkdir -p /opt/appimage-tools \
 && curl -fL --retry 5 \
      -o /opt/appimage-tools/linuxdeploy-x86_64.AppImage \
      "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage" \
 && echo "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d  /opt/appimage-tools/linuxdeploy-x86_64.AppImage" | sha256sum -c - \
 && curl -fL --retry 5 \
      -o /opt/appimage-tools/linuxdeploy-plugin-qt-x86_64.AppImage \
      "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_QT_VERSION}/linuxdeploy-plugin-qt-x86_64.AppImage" \
 && echo "15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724  /opt/appimage-tools/linuxdeploy-plugin-qt-x86_64.AppImage" | sha256sum -c - \
 && chmod 755 /opt/appimage-tools/*.AppImage \
 && cd /opt/appimage-tools \
 && ./linuxdeploy-x86_64.AppImage --appimage-extract >/dev/null \
 && mv squashfs-root /opt/linuxdeploy \
 && curl -fL --retry 5 \
      -H "Accept: application/octet-stream" \
      -o /opt/appimage-tools/runtime-x86_64 \
      "https://api.github.com/repos/AppImage/type2-runtime/releases/assets/456065460" \
 && echo "1cc49bcf1e2ccd593c379adb17c9f85a36d619088296504de95b1d06215aebbf  /opt/appimage-tools/runtime-x86_64" | sha256sum -c -

RUN test "$("${QT_ROOT}/bin/qmake" -query QT_VERSION)" = "${QT_VERSION}" \
 && ldd /opt/fcitx5-qt6/platforminputcontexts/libfcitx5platforminputcontextplugin.so \
      | tee /opt/fcitx5-qt6/ldd.txt \
 && ! grep -q "not found" /opt/fcitx5-qt6/ldd.txt
CONTAINERFILE
}

if [[ "${MIACODE_PORTABLE_CONTAINER:-0}" != "1" ]]; then
  if [[ -z "$CONTAINER_ENGINE" ]]; then
    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
      CONTAINER_ENGINE="docker"
    elif command -v podman >/dev/null 2>&1 && podman info >/dev/null 2>&1; then
      CONTAINER_ENGINE="podman"
    else
      echo "A working Docker or Podman installation is required." >&2
      exit 1
    fi
  fi
  case "$(basename "$CONTAINER_ENGINE")" in
    docker|podman)
      ;;
    *)
      echo "MIACODE_CONTAINER_ENGINE must select Docker or Podman (got: $CONTAINER_ENGINE)" >&2
      exit 2
      ;;
  esac
  require_command "$CONTAINER_ENGINE"
  if ! "$CONTAINER_ENGINE" info >/dev/null 2>&1; then
    echo "Container engine is installed but unavailable: $CONTAINER_ENGINE" >&2
    exit 1
  fi

  if ! "$CONTAINER_ENGINE" image inspect "$BUILDER_IMAGE" >/dev/null 2>&1; then
    build_builder_image
  fi

  mkdir -p "$BUILD_ROOT/home" "$ROOT_DIR/dist"
  container_user_args=()
  if [[ "$(basename "$CONTAINER_ENGINE")" == "podman" ]]; then
    container_user_args+=(--userns=keep-id)
  else
    container_user_args+=(--user "$(id -u):$(id -g)")
  fi
  exec "$CONTAINER_ENGINE" run --rm \
    --platform linux/amd64 \
    --security-opt label=disable \
    "${container_user_args[@]}" \
    --volume "$ROOT_DIR:/work:rw" \
    --workdir /work \
    --env BUILD_JOBS="$BUILD_JOBS" \
    --env HOME=/work/build/home \
    --env XDG_CACHE_HOME=/work/build/home/.cache \
    --env MIACODE_PORTABLE_CONTAINER=1 \
    "$BUILDER_IMAGE" \
    bash /work/scripts/build/package-linux.sh
fi

QT_ROOT="/opt/Qt/6.11.1/gcc_64"
BUILD_DIR="/work/build"
APP_DIR="/work/build/AppDir"
APP_RUN_SOURCE="/work/build/AppRun"
DIST_ROOT="/work/dist"
FFMPEG_PATH="/work/third_party/ffmpeg/linux/ffmpeg"
LINUXDEPLOY="/opt/appimage-tools/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="/opt/appimage-tools/linuxdeploy-plugin-qt-x86_64.AppImage"
APPIMAGETOOL="/opt/linuxdeploy/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool"
APPIMAGE_RUNTIME="/opt/appimage-tools/runtime-x86_64"
LINUXDEPLOY_LOG="/work/build/linuxdeploy.log"

cmake_value() {
  sed -nE "s/^[[:space:]]*set\\($1[[:space:]]+\"([^\"]*)\"\\).*/\\1/p" \
    /work/CMakeLists.txt | head -n 1
}

version="$(cmake_value MIACODE_VERSION_MAJOR).$(cmake_value MIACODE_VERSION_MINOR).$(cmake_value MIACODE_VERSION_PATCH)"
prerelease="$(cmake_value MIACODE_VERSION_PRERELEASE)"
if [[ "$version" == *".."* || "$version" == .* || "$version" == *. ]]; then
  echo "Failed to parse MiaCode version from /work/CMakeLists.txt" >&2
  exit 1
fi
if [[ -n "$prerelease" ]]; then
  version="${version}-${prerelease}"
fi
package_name="MiaCode-v${version}-linux-x86_64"
package_dir="$DIST_ROOT/$package_name"
output="$package_dir/MiaCode.AppImage"
archive="$DIST_ROOT/${package_name}.tar.gz"

MIACODE_LINUX_FFMPEG_CACHE_DIR="/work/build/ffmpeg" \
  bash /work/scripts/ffmpeg/ensure-linux-ffmpeg.sh

for path in \
  "$FFMPEG_PATH" \
  /work/assets \
  /work/licenses \
  /work/resources/extensions/bundled \
  /work/resources/icons/app.png; do
  if [[ ! -e "$path" ]]; then
    echo "Missing required package input: $path" >&2
    exit 1
  fi
done
for doc in LICENSE LICENSE_SCOPE.md THIRD_PARTY_NOTICES.md README.md README_EN.md; do
  if [[ ! -f "/work/$doc" ]]; then
    echo "Missing required release document: /work/$doc" >&2
    exit 1
  fi
done

rm -rf "$DIST_ROOT"
mkdir -p "$package_dir/docs" "$package_dir/licenses"

cmake -S /work -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DMIACODE_BUILD_DEV_TOOLS=OFF
cmake --build "$BUILD_DIR" --target MiaCode --parallel "$BUILD_JOBS"

rm -rf "$APP_DIR"
mkdir -p \
  "$APP_DIR/usr/app/ffmpeg" \
  "$APP_DIR/usr/assets" \
  "$APP_DIR/usr/extensions" \
  "$APP_DIR/usr/lib" \
  "$APP_DIR/usr/plugins/platforminputcontexts" \
  "$APP_DIR/usr/share/applications" \
  "$APP_DIR/usr/share/icons/hicolor/512x512/apps"

install -m 755 "$BUILD_DIR/MiaCode" "$APP_DIR/usr/app/MiaCode"
install -m 755 "$FFMPEG_PATH" "$APP_DIR/usr/app/ffmpeg/ffmpeg"
cp -a /work/assets/. "$APP_DIR/usr/assets/"
rm -rf "$APP_DIR/usr/assets/fonts" "$APP_DIR/usr/assets/reference"
cp -a /work/resources/extensions/bundled/. "$APP_DIR/usr/extensions/"
cp /work/resources/extensions/README.md "$APP_DIR/usr/extensions/README.md"

for doc in LICENSE LICENSE_SCOPE.md THIRD_PARTY_NOTICES.md README.md README_EN.md; do
  cp "/work/$doc" "$package_dir/$doc"
done
cp -a /work/licenses/. "$package_dir/licenses/"
install -Dm644 /usr/share/doc/libstdc++6/copyright \
  "$package_dir/licenses/libstdc++6/copyright"
install -Dm644 /usr/share/doc/libgcc-s1/copyright \
  "$package_dir/licenses/libgcc-s1/copyright"
for doc in \
  docs/ops/DEBUG_INDEX.md \
  docs/specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md; do
  if [[ -f "/work/$doc" ]]; then
    cp "/work/$doc" "$package_dir/docs/$(basename "$doc")"
  fi
done
cat >"$package_dir/docs/RELEASE_README.txt" <<'EOF'
MiaCode release package (Linux x86_64)

Run:
  ./MiaCode.AppImage

The AppImage contains all application runtime files. Documentation and license
notices are provided beside it in this release directory.
EOF

install -m 755 \
  /opt/fcitx5-qt6/platforminputcontexts/libfcitx5platforminputcontextplugin.so \
  "$APP_DIR/usr/plugins/platforminputcontexts/"
install -m 755 \
  "$QT_ROOT/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so" \
  "$QT_ROOT/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so" \
  "$APP_DIR/usr/plugins/platforminputcontexts/"
install -m 755 /usr/lib/x86_64-linux-gnu/libstdc++.so.6 "$APP_DIR/usr/lib/"
install -m 755 /lib/x86_64-linux-gnu/libgcc_s.so.1 "$APP_DIR/usr/lib/"

"$FFMPEG_PATH" -hide_banner -loglevel error \
  -i /work/resources/icons/app.png \
  -vf scale=512:512 \
  -frames:v 1 \
  "$APP_DIR/usr/share/icons/hicolor/512x512/apps/miacode.png"
chmod 644 "$APP_DIR/usr/share/icons/hicolor/512x512/apps/miacode.png"
cat >"$APP_DIR/usr/share/applications/io.github.fanfaredash.MiaCode.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=MiaCode
Comment=maimai chart editor
Exec=MiaCode
Icon=miacode
Terminal=false
Categories=AudioVideo;Audio;Development;
StartupWMClass=MiaCode
EOF

cat >"$APP_RUN_SOURCE" <<'EOF'
#!/bin/sh
set -eu
APPDIR="${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}"
export MIACODE_FFMPEG_PATH="$APPDIR/usr/app/ffmpeg/ffmpeg"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export QML2_IMPORT_PATH="$APPDIR/usr/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
exec "$APPDIR/usr/app/MiaCode" "$@"
EOF
chmod 755 "$APP_RUN_SOURCE"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64
export QMAKE="$QT_ROOT/bin/qmake"
export QML_SOURCES_PATHS="/work/src/app/quick_shell/qml:/work/src/preview/runtime/qml"
export EXTRA_QT_MODULES="svg;multimedia;dbus"
export LINUXDEPLOY_PLUGIN_QT="$LINUXDEPLOY_QT"

if ! "$LINUXDEPLOY" \
  --verbosity=2 \
  --appdir "$APP_DIR" \
  --executable "$APP_DIR/usr/app/MiaCode" \
  --deploy-deps-only "$APP_DIR/usr/plugins/platforminputcontexts" \
  --desktop-file "$APP_DIR/usr/share/applications/io.github.fanfaredash.MiaCode.desktop" \
  --icon-file "$APP_DIR/usr/share/icons/hicolor/512x512/apps/miacode.png" \
  --custom-apprun "$APP_RUN_SOURCE" \
  --plugin qt \
  >"$LINUXDEPLOY_LOG" 2>&1; then
  cat "$LINUXDEPLOY_LOG" >&2
  exit 1
fi

rm -f "$APP_DIR/usr/bin/MiaCode"
ln -s ../app/MiaCode "$APP_DIR/usr/bin/MiaCode"
patchelf --set-rpath '$ORIGIN/../lib' "$APP_DIR/usr/app/MiaCode"

"$APPIMAGETOOL" \
  --no-appstream \
  --runtime-file "$APPIMAGE_RUNTIME" \
  --mksquashfs-opt=-processors \
  --mksquashfs-opt="$BUILD_JOBS" \
  "$APP_DIR" \
  "$output"

test -x "$output"
required_package_paths=(
  "$APP_DIR/AppRun"
  "$APP_DIR/usr/app/MiaCode"
  "$APP_DIR/usr/app/ffmpeg/ffmpeg"
  "$APP_DIR/usr/assets"
  "$APP_DIR/usr/extensions"
  "$APP_DIR/usr/lib/libQt6Core.so.6"
  "$APP_DIR/usr/lib/libstdc++.so.6"
  "$APP_DIR/usr/lib/libgcc_s.so.1"
  "$APP_DIR/usr/plugins/platforms/libqxcb.so"
  "$APP_DIR/usr/plugins/platforminputcontexts/libfcitx5platforminputcontextplugin.so"
  "$APP_DIR/usr/plugins/platforminputcontexts/libibusplatforminputcontextplugin.so"
)
for path in "${required_package_paths[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "Missing required packaged path: $path" >&2
    exit 1
  fi
done
if [[ "$(readlink "$APP_DIR/usr/bin/MiaCode")" != "../app/MiaCode" ]]; then
  echo "Packaged launcher is not the expected relative symlink." >&2
  exit 1
fi

elf_paths=()
while IFS= read -r -d '' elf_path; do
  if ! file -L "$elf_path" | grep -q 'ELF '; then
    continue
  fi
  elf_paths+=("$elf_path")
  if readelf -d "$elf_path" 2>/dev/null | grep -Eq '/(opt|work)/'; then
    echo "Packaged ELF contains a build-machine runtime path: $elf_path" >&2
    readelf -d "$elf_path" | grep -E 'RPATH|RUNPATH' >&2
    exit 1
  fi
  ldd_output="$(LD_LIBRARY_PATH="$APP_DIR/usr/lib" ldd "$elf_path" 2>&1 || true)"
  missing_dependencies="$(sed -n '/not found/p' <<<"$ldd_output")"
  if [[ -n "$missing_dependencies" ]]; then
    echo "Packaged ELF has unresolved dependencies: $elf_path" >&2
    echo "$missing_dependencies" >&2
    exit 1
  fi
done < <(find "$APP_DIR" -type f -print0)

maximum_glibc="$(
  readelf --version-info "${elf_paths[@]}" 2>/dev/null \
    | grep -Eo 'GLIBC_[0-9]+(\.[0-9]+)+' \
    | sed 's/^GLIBC_//' \
    | sort -Vu \
    | tail -n 1
)"
if [[ -z "$maximum_glibc" || "$(printf '%s\n' "$maximum_glibc" 2.35 | sort -V | tail -n 1)" != "2.35" ]]; then
  echo "Unexpected packaged GLIBC requirement: ${maximum_glibc:-unknown} (maximum supported: 2.35)" >&2
  exit 1
fi
if ! file -L "$output" | grep -q 'ELF 64-bit.*x86-64'; then
  echo "Unexpected AppImage architecture: $(file -L "$output")" >&2
  exit 1
fi
(
  cd "$package_dir"
  sha256sum "$(basename "$output")" >"$(basename "$output").sha256"
  sha256sum -c "$(basename "$output").sha256"
)
tar -C "$DIST_ROOT" -czf "$archive" "$package_name"
archive_listing="$(tar -tzf "$archive")"
grep -Fxq "$package_name/MiaCode.AppImage" <<<"$archive_listing"
echo "Created release directory: $package_dir"
echo "Created archive: $archive"
du -h "$output" "$archive"
