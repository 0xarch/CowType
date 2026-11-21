#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs only in disposable CI containers; never installs on the developer desktop.
set -euo pipefail
case "${1:?Usage: ci-linux.sh dependencies|build|verify DISTRO}" in
dependencies)
  case "$2" in
    arch)
      pacman -Syu --noconfirm --needed base-devel cmake ninja rust libime boost openssl librsvg fcitx5 python python-gobject gtk4 libadwaita git pkgconf dbus
      useradd --create-home builder
      ;;
    fedora*)
      dnf install -y gcc-c++ cmake ninja-build rust cargo libime-devel boost-devel openssl-devel librsvg2-tools fcitx5-devel fcitx5 python3 python3-gobject gtk4-devel libadwaita rpm-build rpmdevtools git pkgconf-pkg-config dbus-daemon findutils file
      ;;
    debian*|ubuntu*)
      export DEBIAN_FRONTEND=noninteractive
      apt-get update
      apt-get install -y --no-install-recommends build-essential cmake ninja-build cargo rustc libimecore-dev libimepinyin-dev libboost-dev libssl-dev librsvg2-bin libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5 python3 python3-gi libgtk-4-bin gir1.2-gtk-4.0 gir1.2-adw-1 dpkg-dev git pkg-config dbus-bin ca-certificates
      ;;
    *) exit 2 ;;
  esac
  ;;
build)
  # The source artifact contains verified default data and Cargo vendor inputs.
  archive=$(find dist -maxdepth 1 -name 'cowtype-*-source.tar.gz' -print -quit)
  test -n "$archive"
  mkdir -p build/ci-source
  tar -xf "$archive" -C build/ci-source
  source_dir=$(find build/ci-source -mindepth 1 -maxdepth 1 -type d -name 'cowtype-*' -print -quit)
  source_dir=$(realpath "$source_dir")
  python3 "$source_dir/tooling/development.py"
  libdir=$(pkg-config --variable=libdir Fcitx5Core)
  libdir=${libdir#/usr/}
  cmake -S "$source_dir" -B build/ci-test -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR="$libdir" -DBUILD_TESTING=ON
  cmake --build build/ci-test -j2
  ctest --test-dir build/ci-test --output-on-failure
  python3 -m unittest discover -s "$source_dir/tests" -p 'test_*.py'
  case "$2" in
    arch)
      mkdir -p build/arch dist/packages
      cp dist/PKGBUILD "$archive" build/arch/
      chown -R builder:builder build/arch dist/packages
      runuser -u builder -- env PKGDEST="$PWD/dist/packages" bash -c 'cd build/arch && makepkg --cleanbuild --force --noconfirm'
      ;;
    *) python3 "$source_dir/tooling/package_linux.py" --source "$source_dir" --format "${3:?deb or rpm}" --distro "$2" --libdir "$libdir" --output "$PWD/dist/packages" ;;
  esac
  ;;
verify)
  # Package installation only happens inside these disposable containers.
  case "$2" in
    arch) sed -i '/^NoExtract/d' /etc/pacman.conf; pacman -U --noconfirm dist/packages/*.pkg.tar.zst; pacman -Qkk cowtype ;;
    fedora*) dnf install -y dist/packages/*.rpm; rpm -V cowtype ;;
    *) apt-get install -y ./dist/packages/*.deb; dpkg --verify cowtype ;;
  esac
  libdir=$(pkg-config --variable=libdir Fcitx5Core)
  test -s "$libdir/fcitx5/libcowtype-fcitx5.so"
  ldd "$libdir/fcitx5/libcowtype-fcitx5.so" > build/ci-ldd.txt
  if grep -q 'not found' build/ci-ldd.txt; then cat build/ci-ldd.txt; exit 1; fi
  cowtype --version
  cowtype-voice --version
  test -s /usr/share/cowtype/dictionaries/ice/lexicon.tsv
  test -s /usr/share/cowtype/dictionaries/wubi/lexicon.tsv
  test -s /usr/share/icons/hicolor/scalable/apps/cowtype.svg
  test -s /usr/share/icons/hicolor/22x22/apps/cowtype.png
  # Use package binaries, explicit synthetic dictionaries and isolated Fcitx paths.
  python3 tests/native/isolated.py --root build/ci-installed --prefix build/ci-installed/prefix --data build/ci-test/test-data build/ci-test/cowtype-fcitx-probe "$libdir/fcitx5/libcowtype-fcitx5.so"
  ;;
*) exit 2 ;;
esac
