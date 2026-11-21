#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build an Arch package from this checkout without installing it.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: bash build-arch.sh [--check]

Download verified default dictionaries and locked Rust dependencies, then build
and test an Arch x86_64 package. Output: dist/*.pkg.tar.zst (or your makepkg format).
Run as a regular user. This script does not install packages or download models.
--check  Check the environment and source manifest only; no downloads or builds.
EOF
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  --check) check_only=true ;;
  '') check_only=false ;;
  *) usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then usage >&2; exit 2; fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
project_dir=$PWD
SECONDS=0
stage='environment checks'
trap 'printf "Build failed during %s (after %ss). See the error above.\n" "$stage" "$SECONDS" >&2' ERR

if (( EUID == 0 )); then
  echo 'Run this script as a regular user; makepkg cannot run as root.' >&2
  exit 1
fi
if [[ ! -f /etc/arch-release || $(uname -m) != x86_64 ]]; then
  echo 'This package entry point requires Arch Linux x86_64.' >&2
  exit 1
fi
if ! command -v pacman >/dev/null || ! command -v makepkg >/dev/null; then
  echo 'Install Arch packaging tools first: sudo pacman -S --needed base-devel' >&2
  exit 1
fi
# Reuse dependency declarations without maintaining a second package list.
source packaging/arch/PKGBUILD.in
if ! missing=$(pacman -T base-devel "${depends[@]}" "${makedepends[@]}"); then
  printf 'Missing build/runtime dependencies:\n%s\nInstall them with:\n  sudo pacman -S --needed base-devel' "$missing" >&2
  printf ' %q' "${depends[@]}" "${makedepends[@]}" >&2
  printf '\n' >&2
  exit 1
fi
if ! cargo --version || ! rustc --version; then
  echo 'A working Rust toolchain is required. With rustup, run: rustup default stable' >&2
  exit 1
fi
python3 tooling/development.py
if $check_only; then
  printf 'Environment and source checks passed (%ss).\n' "$SECONDS"
  exit 0
fi

mkdir -p build dist
inputs=$(mktemp -d "$project_dir/build/arch-inputs.XXXXXX")
trap 'rm -rf -- "$inputs"' EXIT

stage='default dictionary preparation'
echo '[1/4] Preparing verified Ice and Wubi dictionaries (network needed on first run)...'
python3 src-data-tools/dictionaries/manage.py build-default \
  --source-cache "$project_dir/build/review" --output "$inputs/data"

stage='Rust dependency preparation'
echo '[2/4] Preparing locked Rust dependencies (network may be needed)...'
cargo vendor --locked --versioned-dirs --manifest-path src-voice/Cargo.toml \
  "$inputs/voice-vendor" > "$inputs/vendor-config.toml"

stage='source archive preparation'
echo '[3/4] Verifying inputs and creating the source archive...'
python3 tooling/package_arch.py --dictionary-dir "$inputs/data" --vendor-dir "$inputs/voice-vendor"

stage='makepkg build and tests'
echo '[4/4] Building and testing the package...'
cd "$project_dir/build/arch"
PKGDEST="$project_dir/dist" makepkg --cleanbuild --force
printf '\nPackage build completed in %ss. Packages are in %s/dist/\n' "$SECONDS" "$project_dir"
package_file=$(PKGDEST="$project_dir/dist" makepkg --packagelist)
printf 'To install, run:\n  sudo pacman -U %q\n' "$package_file"
