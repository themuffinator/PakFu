#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
out_dir="${2:-dist}"
version="${3:-$(cat VERSION)}"

binary="${build_dir}/src/pakfu"
if [[ ! -f "${binary}" ]]; then
  echo "pakfu binary not found at ${binary}" >&2
  exit 1
fi

mkdir -p "${out_dir}"
tar -C "${build_dir}/src" -czf "${out_dir}/pakfu-linux-${version}.tar.gz" pakfu
echo "Packaged: ${out_dir}/pakfu-linux-${version}.tar.gz"
