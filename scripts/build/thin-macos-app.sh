#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: thin-macos-app.sh <MiaCode.app> <arm64|x86_64>

Removes the non-target CPU slice from every universal Mach-O inside a staged
macOS app bundle. A Mach-O that does not contain the requested architecture is
treated as a packaging error; it is never deleted or silently skipped.
EOF
}

APP_PATH="${1:-}"
TARGET_ARCH="${2:-}"

if [[ -z "$APP_PATH" || -z "$TARGET_ARCH" ]]; then
  usage >&2
  exit 2
fi

case "$TARGET_ARCH" in
  arm64|x86_64)
    ;;
  *)
    echo "Unsupported target architecture: $TARGET_ARCH" >&2
    usage >&2
    exit 2
    ;;
esac

if [[ ! -d "$APP_PATH" || "$(basename "$APP_PATH")" != *.app ]]; then
  echo "App bundle not found: $APP_PATH" >&2
  exit 1
fi

for required_command in find lipo mktemp stat chmod mv rm; do
  if ! command -v "$required_command" >/dev/null 2>&1; then
    echo "Required command not found: $required_command" >&2
    exit 1
  fi
done

contains_arch() {
  local arch_list="$1"
  local requested_arch="$2"
  [[ " $arch_list " == *" $requested_arch "* ]]
}

mach_o_count=0
thinned_count=0
already_thin_count=0
bytes_before=0
bytes_after=0
preflight_file_count=0
processed_mach_o_count=0
verified_mach_o_count=0
progress_interval=25
current_temp_path=""
manifest_path="$(mktemp "${TMPDIR:-/tmp}/miacode-thin-manifest.XXXXXX")"

cleanup_temp_files() {
  if [[ -n "$current_temp_path" && -f "$current_temp_path" ]]; then
    rm -f "$current_temp_path"
  fi
  if [[ -f "$manifest_path" ]]; then
    rm -f "$manifest_path"
  fi
}
trap cleanup_temp_files EXIT

# Freeze the input file set before creating adjacent temporary outputs. A live
# `find` during rewriting could otherwise observe a newly created `.thin.*`
# file on a sufficiently large bundle.
find "$APP_PATH" -type f -print0 > "$manifest_path"
manifest_file_count="$(tr -cd '\0' < "$manifest_path" | wc -c | tr -d '[:space:]')"
echo "[thin] Preflight: scanning $manifest_file_count staged files for $TARGET_ARCH compatibility..."

# Complete a read-only preflight before rewriting the first file. This keeps a
# newly introduced x86_64-only helper from leaving a partially thinned staging
# bundle when the package is supposed to be arm64-only (and vice versa).
while IFS= read -r -d '' binary_path; do
  preflight_file_count=$((preflight_file_count + 1))
  archs="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  if [[ -z "$archs" ]]; then
    continue
  fi

  mach_o_count=$((mach_o_count + 1))
  binary_size_before="$(stat -f '%z' "$binary_path")"
  bytes_before=$((bytes_before + binary_size_before))

  if ! contains_arch "$archs" "$TARGET_ARCH"; then
    echo "Mach-O is missing required architecture '$TARGET_ARCH': $binary_path (has: $archs)" >&2
    exit 1
  fi
  if (( mach_o_count % progress_interval == 0 )); then
    echo "[thin] Preflight: checked $mach_o_count Mach-O files ($preflight_file_count/$manifest_file_count staged files)."
  fi
done < "$manifest_path"

if (( mach_o_count == 0 )); then
  echo "No Mach-O binaries found in app bundle: $APP_PATH" >&2
  exit 1
fi

echo "[thin] Rewriting $mach_o_count Mach-O files to $TARGET_ARCH..."

while IFS= read -r -d '' binary_path; do
  archs="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  if [[ -z "$archs" ]]; then
    continue
  fi
  processed_mach_o_count=$((processed_mach_o_count + 1))
  binary_size_before="$(stat -f '%z' "$binary_path")"
  if [[ "$archs" == "$TARGET_ARCH" ]]; then
    already_thin_count=$((already_thin_count + 1))
    bytes_after=$((bytes_after + binary_size_before))
    continue
  fi

  binary_mode="$(stat -f '%Lp' "$binary_path")"
  current_temp_path="$(mktemp "${binary_path}.thin.XXXXXX")"
  if ! lipo "$binary_path" -thin "$TARGET_ARCH" -output "$current_temp_path"; then
    echo "Failed to extract '$TARGET_ARCH' from: $binary_path" >&2
    exit 1
  fi
  chmod "$binary_mode" "$current_temp_path"
  mv "$current_temp_path" "$binary_path"
  current_temp_path=""

  output_archs="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  if [[ "$output_archs" != "$TARGET_ARCH" ]]; then
    echo "Unexpected architecture after thinning: $binary_path (has: ${output_archs:-unknown})" >&2
    exit 1
  fi

  binary_size_after="$(stat -f '%z' "$binary_path")"
  bytes_after=$((bytes_after + binary_size_after))
  thinned_count=$((thinned_count + 1))

  if (( processed_mach_o_count % progress_interval == 0 || processed_mach_o_count == mach_o_count )); then
    echo "[thin] Rewrite: processed $processed_mach_o_count/$mach_o_count Mach-O files; thinned=$thinned_count."
  fi
done < "$manifest_path"

# Re-scan the completed bundle as a final architecture-closure gate. This also
# catches a file that appeared or changed while the first pass was running.
verified_count=0
echo "[thin] Final verification: checking $mach_o_count Mach-O files are $TARGET_ARCH-only..."
while IFS= read -r -d '' binary_path; do
  archs="$(lipo -archs "$binary_path" 2>/dev/null || true)"
  if [[ -z "$archs" ]]; then
    continue
  fi
  if [[ "$archs" != "$TARGET_ARCH" ]]; then
    echo "Architecture closure failed: $binary_path (expected: $TARGET_ARCH, has: $archs)" >&2
    exit 1
  fi
  verified_count=$((verified_count + 1))
  verified_mach_o_count=$((verified_mach_o_count + 1))
  if (( verified_mach_o_count % progress_interval == 0 || verified_mach_o_count == mach_o_count )); then
    echo "[thin] Verification: checked $verified_mach_o_count/$mach_o_count Mach-O files."
  fi
done < <(find "$APP_PATH" -type f -print0)

if (( verified_count != mach_o_count )); then
  echo "Mach-O file count changed during thinning: before=$mach_o_count after=$verified_count" >&2
  exit 1
fi

bytes_saved=$((bytes_before - bytes_after))
printf 'Architecture thinning complete: target=%s mach_o=%d thinned=%d already_thin=%d bytes_before=%d bytes_after=%d bytes_saved=%d\n' \
  "$TARGET_ARCH" "$mach_o_count" "$thinned_count" "$already_thin_count" \
  "$bytes_before" "$bytes_after" "$bytes_saved"
