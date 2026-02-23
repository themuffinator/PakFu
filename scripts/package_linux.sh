#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-builddir}"
out_dir="${2:-dist}"
version="${3:-$(cat VERSION)}"
root_dir="$(cd "$(dirname "$0")/.." && pwd)"
raw_arch="${4:-$(uname -m)}"
case "${raw_arch}" in
  x86_64|amd64|x64)
    arch="x64"
    linuxdeploy_arch="x86_64"
    ;;
  arm64|aarch64)
    arch="arm64"
    linuxdeploy_arch="aarch64"
    ;;
  *)
    arch="${raw_arch}"
    linuxdeploy_arch="${raw_arch}"
    ;;
esac

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

if command -v qmake6 >/dev/null 2>&1; then
  qmake_bin="$(command -v qmake6)"
elif command -v qmake >/dev/null 2>&1; then
  qmake_bin="$(command -v qmake)"
else
  echo "qmake6/qmake not found on PATH." >&2
  exit 1
fi

mkdir -p "${out_dir}"
portable_dir="${out_dir}/pakfu-${version}-linux-${arch}-portable"
portable_archive="${out_dir}/pakfu-${version}-linux-${arch}-portable.tar.gz"
installer_appimage="${out_dir}/pakfu-${version}-linux-${arch}-installer.AppImage"
app_dir="${out_dir}/PakFu.AppDir"
desktop_file="${app_dir}/usr/share/applications/pakfu.desktop"
icon_file="${app_dir}/usr/share/icons/hicolor/256x256/apps/pakfu-app.png"
linuxdeployqt_tool="${out_dir}/linuxdeployqt-${linuxdeploy_arch}.AppImage"

rm -rf "${portable_dir}" "${portable_archive}" "${installer_appimage}" "${app_dir}"
mkdir -p "${portable_dir}"
cp "${binary}" "${portable_dir}/pakfu"
chmod +x "${portable_dir}/pakfu"
if [[ -d "${root_dir}/assets" ]]; then
  cp -R "${root_dir}/assets" "${portable_dir}/assets"
fi
tar -czf "${portable_archive}" -C "${out_dir}" "$(basename "${portable_dir}")"
echo "Packaged portable archive: ${portable_archive}"

mkdir -p "$(dirname "${desktop_file}")" "$(dirname "${icon_file}")" "${app_dir}/usr/bin" "${app_dir}/usr/share/pakfu"
cp "${binary}" "${app_dir}/usr/bin/pakfu"
chmod +x "${app_dir}/usr/bin/pakfu"
if [[ -d "${root_dir}/assets" ]]; then
  cp -R "${root_dir}/assets" "${app_dir}/usr/share/pakfu/assets"
fi
if [[ -f "${root_dir}/assets/img/pakfu-icon-256.png" ]]; then
  cp "${root_dir}/assets/img/pakfu-icon-256.png" "${icon_file}"
fi
mkdir -p "${app_dir}/usr/share/doc/libc6"
if [[ -f "/usr/share/doc/libc6/copyright" ]]; then
  cp "/usr/share/doc/libc6/copyright" "${app_dir}/usr/share/doc/libc6/copyright"
else
  printf "Bundled by PakFu nightly build.\n" > "${app_dir}/usr/share/doc/libc6/copyright"
fi

cat > "${desktop_file}" <<DESKTOP
[Desktop Entry]
Type=Application
Name=PakFu
Exec=pakfu
Icon=pakfu-app
Categories=Utility;
Terminal=false
DESKTOP

if [[ ! -f "${linuxdeployqt_tool}" ]]; then
  linuxdeployqt_url="https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-${linuxdeploy_arch}.AppImage"
  curl -fL --retry 3 --retry-delay 2 "${linuxdeployqt_url}" -o "${linuxdeployqt_tool}"
  chmod +x "${linuxdeployqt_tool}"
fi

find "${out_dir}" -maxdepth 1 -type f -name "*.AppImage" ! -name "$(basename "${linuxdeployqt_tool}")" -delete
export VERSION="${version}"
APPIMAGE_EXTRACT_AND_RUN=1 "${linuxdeployqt_tool}" \
  "${desktop_file}" \
  -qmake="${qmake_bin}" \
  -unsupported-allow-new-glibc \
  -no-copy-copyright-files \
  -bundle-non-qt-libs \
  -appimage

generated_appimage="$(find "${out_dir}" -maxdepth 1 -type f -name "*.AppImage" ! -name "$(basename "${linuxdeployqt_tool}")" | head -n1)"
if [[ -z "${generated_appimage}" ]]; then
  echo "linuxdeployqt did not produce an AppImage." >&2
  exit 1
fi
mv "${generated_appimage}" "${installer_appimage}"
chmod +x "${installer_appimage}"
echo "Packaged installer: ${installer_appimage}"

rm -rf "${portable_dir}" "${app_dir}"
rm -f "${linuxdeployqt_tool}"
