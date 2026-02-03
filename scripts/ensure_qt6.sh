#!/usr/bin/env bash
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.10.1}"
QT_INSTALL_ROOT="${QT_INSTALL_ROOT:-$HOME/Qt}"
QT_ARCH="${QT_ARCH:-}"

os="$(uname -s)"
case "$os" in
  Linux)
    platform="linux"
    default_arch="gcc_64"
    ;;
  Darwin)
    platform="mac"
    default_arch="clang_64"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    platform="windows"
    default_arch="win64_msvc2022_64"
    ;;
  *)
    echo "Unsupported OS for Qt auto-install: $os" >&2
    exit 1
    ;;
esac

arch="${QT_ARCH:-$default_arch}"
arch_dir="$arch"
if [[ "$platform" == "windows" ]]; then
  arch_dir="${arch_dir#win64_}"
  arch_dir="${arch_dir#win32_}"
fi

find_qmake() {
  if command -v qmake6 >/dev/null 2>&1; then
    command -v qmake6
    return 0
  fi
  if command -v qmake >/dev/null 2>&1; then
    command -v qmake
    return 0
  fi
  local candidate="$QT_INSTALL_ROOT/$QT_VERSION/$arch_dir/bin/qmake6"
  if [[ -x "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi
  local legacy="$QT_INSTALL_ROOT/$QT_VERSION/$arch_dir/bin/qmake"
  if [[ -x "$legacy" ]]; then
    echo "$legacy"
    return 0
  fi
  return 1
}

ensure_qmake6() {
  local bin="$QT_INSTALL_ROOT/$QT_VERSION/$arch_dir/bin"
  if [[ -x "$bin/qmake6" ]]; then
    return 0
  fi
  if [[ -x "$bin/qmake" ]]; then
    ln -sf "$bin/qmake" "$bin/qmake6" 2>/dev/null || cp -f "$bin/qmake" "$bin/qmake6"
  fi
}

if qmake_path="$(find_qmake)"; then
  ensure_qmake6
  echo "Qt $QT_VERSION ($arch) already available: $qmake_path"
  exit 0
fi

if ! command -v python >/dev/null 2>&1; then
  echo "Python is required to auto-install Qt (aqtinstall). Install Python and re-run." >&2
  exit 1
fi

if ! python -m aqt --help >/dev/null 2>&1; then
  python -m pip install --user --upgrade aqtinstall
fi

echo "Installing Qt $QT_VERSION ($arch) to $QT_INSTALL_ROOT ..."
python -m aqt install-qt "$platform" desktop "$QT_VERSION" "$arch" -O "$QT_INSTALL_ROOT" -m qtmultimedia qtimageformats

ensure_qmake6
if qmake_path="$(find_qmake)"; then
  echo "Qt installation complete: $qmake_path"
  exit 0
fi

echo "Qt installation completed but qmake6 not found. Set QT_INSTALL_ROOT/QT_ARCH or adjust PATH." >&2
exit 1
