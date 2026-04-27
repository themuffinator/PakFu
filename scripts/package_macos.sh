#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-builddir}"
out_dir="${2:-dist}"
version="${3:-$(cat VERSION)}"
root_dir="$(cd "$(dirname "$0")/.." && pwd)"
raw_arch="${4:-$(uname -m)}"
case "${raw_arch}" in
  x86_64|amd64)
    arch="x64"
    ;;
  arm64|aarch64)
    arch="arm64"
    ;;
  *)
    arch="${raw_arch}"
    ;;
esac

python_cmd=""
if command -v python3 >/dev/null 2>&1; then
  python_cmd="python3"
elif command -v python >/dev/null 2>&1; then
  python_cmd="python"
else
  echo "Python is required to generate the packaged HTML user guide." >&2
  exit 1
fi

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

app_name="PakFu.app"
app_dir="${out_dir}/${app_name}"
portable_zip="${out_dir}/pakfu-${version}-macos-${arch}-portable.zip"
installer_pkg="${out_dir}/pakfu-${version}-macos-${arch}-installer.pkg"
pkg_root="${out_dir}/pkgroot"
portable_root="${out_dir}/pakfu-${version}-macos-${arch}-portable"
guide_dir="${out_dir}/Documentation"

rm -rf "${app_dir}" "${pkg_root}" "${portable_root}" "${guide_dir}" "${portable_zip}" "${installer_pkg}"
mkdir -p "${app_dir}/Contents/MacOS" "${app_dir}/Contents/Resources"
cp "${binary}" "${app_dir}/Contents/MacOS/PakFu"
chmod +x "${app_dir}/Contents/MacOS/PakFu"

if [[ -d "${root_dir}/assets" ]]; then
  cp -R "${root_dir}/assets" "${app_dir}/Contents/Resources/assets"
fi
if [[ -f "${root_dir}/assets/img/pakfu-icon-256.icns" ]]; then
  cp "${root_dir}/assets/img/pakfu-icon-256.icns" "${app_dir}/Contents/Resources/pakfu.icns"
fi

info_plist_template="${root_dir}/packaging/macos/Info.plist.in"
if [[ ! -f "${info_plist_template}" ]]; then
  echo "Info.plist template not found: ${info_plist_template}" >&2
  exit 1
fi
"${python_cmd}" - "${info_plist_template}" "${app_dir}/Contents/Info.plist" "${version}" <<'PY'
import sys
from pathlib import Path
template = Path(sys.argv[1]).read_text(encoding="utf-8")
Path(sys.argv[2]).write_text(template.replace("@PAKFU_VERSION@", sys.argv[3]), encoding="utf-8")
PY

if command -v macdeployqt >/dev/null 2>&1; then
  macdeployqt "${app_dir}" -always-overwrite -verbose=1
else
  echo "macdeployqt not found on PATH." >&2
  exit 1
fi

"${python_cmd}" "${root_dir}/scripts/build_user_guide.py" --output "${guide_dir}" --version "${version}"

mkdir -p "${portable_root}"
cp -R "${app_dir}" "${portable_root}/PakFu.app"
cp -R "${guide_dir}" "${portable_root}/Documentation"
ditto -c -k --sequesterRsrc --keepParent "${portable_root}" "${portable_zip}"
echo "Packaged portable archive: ${portable_zip}"

cp -R "${guide_dir}" "${app_dir}/Contents/Resources/Documentation"
mkdir -p "${pkg_root}/Applications"
cp -R "${app_dir}" "${pkg_root}/Applications/PakFu.app"
pkgbuild \
  --root "${pkg_root}" \
  --identifier "com.pakfu.app" \
  --version "${version}" \
  --install-location "/" \
  "${installer_pkg}"
echo "Packaged installer: ${installer_pkg}"

rm -rf "${app_dir}" "${pkg_root}" "${portable_root}" "${guide_dir}"
