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

find_qmake() {
  if command -v qmake6 >/dev/null 2>&1; then
    command -v qmake6
    return 0
  fi
  local candidate="$QT_INSTALL_ROOT/$QT_VERSION/$arch/bin/qmake6"
  if [[ -x "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi
  return 1
}

if qmake_path="$(find_qmake)"; then
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
python -m aqt install-qt "$platform" desktop "$QT_VERSION" "$arch" -O "$QT_INSTALL_ROOT" -m qtbase qtmultimedia qtsvg qtimageformats

if qmake_path="$(find_qmake)"; then
  echo "Qt installation complete: $qmake_path"
  exit 0
fi

echo "Qt installation completed but qmake6 not found. Set QT_INSTALL_ROOT/QT_ARCH or adjust PATH." >&2
exit 1

