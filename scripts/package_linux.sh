#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
out_dir="${2:-dist}"
version="${3:-$(cat VERSION)}"
root_dir="$(cd "$(dirname "$0")/.." && pwd)"

binary="${build_dir}/src/pakfu"
if [[ ! -f "${binary}" ]]; then
  echo "pakfu binary not found at ${binary}" >&2
  exit 1
fi

mkdir -p "${out_dir}"
archive="${out_dir}/pakfu-linux-${version}.tar.gz"
if [[ -d "${root_dir}/assets" ]]; then
  tar -czf "${archive}" \
    -C "${build_dir}/src" pakfu \
    -C "${root_dir}" assets
else
  tar -czf "${archive}" -C "${build_dir}/src" pakfu
fi
echo "Packaged: ${out_dir}/pakfu-linux-${version}.tar.gz"
