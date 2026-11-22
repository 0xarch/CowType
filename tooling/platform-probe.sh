#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Disposable containers only: install the native Fcitx headers before full deps.
set -euo pipefail
case "${1:?distribution}" in
  arch) pacman -Syu --noconfirm --needed pkgconf fcitx5 ;;
  fedora*) dnf install -y pkgconf-pkg-config fcitx5-devel ;;
  opensuse*)
    zypper --non-interactive refresh
    zypper --non-interactive install --no-recommends pkgconf-pkg-config fcitx5-devel
    ;;
  *)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends pkg-config libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev
    ;;
esac
{
  uname -m
  cat /etc/os-release
  printf 'Fcitx5Core=%s\n' "$(pkg-config --modversion Fcitx5Core)"
} > build/platform-report/versions.txt
cat build/platform-report/versions.txt
if ! pkg-config --atleast-version=5.1.11 Fcitx5Core; then
  echo 'Fcitx5 is below required version 5.1.11; compatibility baseline is 5.1.11.' >&2
  exit 1
fi
