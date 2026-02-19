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

rm -rf "${app_dir}" "${pkg_root}" "${portable_zip}" "${installer_pkg}"
mkdir -p "${app_dir}/Contents/MacOS" "${app_dir}/Contents/Resources"
cp "${binary}" "${app_dir}/Contents/MacOS/PakFu"
chmod +x "${app_dir}/Contents/MacOS/PakFu"

if [[ -d "${root_dir}/assets" ]]; then
  cp -R "${root_dir}/assets" "${app_dir}/Contents/Resources/assets"
fi
if [[ -f "${root_dir}/assets/img/pakfu-icon-256.icns" ]]; then
  cp "${root_dir}/assets/img/pakfu-icon-256.icns" "${app_dir}/Contents/Resources/pakfu.icns"
fi

cat > "${app_dir}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>
  <string>PakFu</string>
  <key>CFBundleDisplayName</key>
  <string>PakFu</string>
  <key>CFBundleIdentifier</key>
  <string>com.pakfu.app</string>
  <key>CFBundleVersion</key>
  <string>${version}</string>
  <key>CFBundleShortVersionString</key>
  <string>${version}</string>
  <key>CFBundleExecutable</key>
  <string>PakFu</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>CFBundleIconFile</key>
  <string>pakfu.icns</string>
</dict>
</plist>
PLIST

if command -v macdeployqt >/dev/null 2>&1; then
  macdeployqt "${app_dir}" -always-overwrite -verbose=1
else
  echo "macdeployqt not found on PATH." >&2
  exit 1
fi

ditto -c -k --sequesterRsrc --keepParent "${app_dir}" "${portable_zip}"
echo "Packaged portable archive: ${portable_zip}"

mkdir -p "${pkg_root}/Applications"
cp -R "${app_dir}" "${pkg_root}/Applications/PakFu.app"
pkgbuild \
  --root "${pkg_root}" \
  --identifier "com.pakfu.app" \
  --version "${version}" \
  --install-location "/" \
  "${installer_pkg}"
echo "Packaged installer: ${installer_pkg}"

rm -rf "${app_dir}" "${pkg_root}"
