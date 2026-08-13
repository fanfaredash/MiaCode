#!/usr/bin/env bash

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
package_root="${script_dir}"
log_dir="${package_root}/logs"
app_path="${package_root}/MiaCode.app/Contents/MacOS/MiaCode"

if ! mkdir -p "${log_dir}" 2>/dev/null; then
    printf 'Error: unable to create MiaCode log directory: %s\n' "${log_dir}" >&2
    exit 1
fi

export MIACODE_LOG_DIR="${log_dir}"

if [[ ! -x "${app_path}" ]]; then
    printf 'Error: MiaCode executable is missing or not executable: %s\n' "${app_path}" >&2
    exit 1
fi

printf 'MiaCode app: %s\nMiaCode logs: %s\n' "${app_path}" "${log_dir}"
exec "${app_path}" --debug "$@"
