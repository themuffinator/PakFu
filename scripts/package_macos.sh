#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
out_dir="${2:-dist}"
version="${3:-$(cat VERSION)}"
root_dir="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v qmake6 >/dev/null 2>&1; then
  if [[ -f "${root_dir}/scripts/ensure_qt6.sh" ]]; then
    "${root_dir}/scripts/ensure_qt6.sh"
  fi
fi

binary="${build_dir}/src/pakfu"
if [[ ! -f "${binary}" ]]; then
  echo "pakfu binary not found at ${binary}" >&2
  exit 1
fi

mkdir -p "${out_dir}"

staging="${out_dir}/pakfu-macos-${version}"
rm -rf "${staging}"
mkdir -p "${staging}"
cp "${binary}" "${staging}/pakfu"
if [[ -d "${root_dir}/assets" ]]; then
  cp -R "${root_dir}/assets" "${staging}/assets"
fi

(cd "${staging}" && zip -r "${out_dir}/pakfu-macos-${version}.zip" .)
echo "Packaged: ${out_dir}/pakfu-macos-${version}.zip"
