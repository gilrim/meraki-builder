#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

if command -v apt-get >/dev/null 2>&1; then
  log "Installing Debian/Ubuntu build dependencies"
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential git ca-certificates curl wget rsync unzip zip file bc bzip2 xz-utils \
    python3 python3-setuptools python3-distutils perl ruby gawk gettext flex bison patch diffutils \
    libgmp-dev libmpfr-dev libmpc-dev libexpat1-dev zlib1g-dev libncurses-dev \
    pkg-config libtool autoconf automake autotools-dev m4 texinfo help2man \
    u-boot-tools device-tree-compiler squashfs-tools mtd-utils xxd cpio jq \
    tar gzip sed grep coreutils findutils
  if [[ -x /usr/bin/aclocal-1.16 ]]; then sudo ln -sf /usr/bin/aclocal-1.16 /usr/bin/aclocal-1.14; fi
  if [[ -x /usr/bin/automake-1.16 ]]; then sudo ln -sf /usr/bin/automake-1.16 /usr/bin/automake-1.14; fi
elif command -v pacman >/dev/null 2>&1; then
  log "Installing CachyOS/Arch orchestration dependencies"
  sudo pacman -Syu --needed --noconfirm \
    base-devel git curl wget rsync unzip zip file bc bzip2 xz python perl ruby \
    gawk gettext flex bison patch diffutils gmp mpfr libmpc expat zlib ncurses \
    pkgconf libtool autoconf automake m4 texinfo uboot-tools dtc squashfs-tools \
    mtd-utils xxd cpio jq distrobox podman
  warn "The old OpenWrt toolchain/kernel build is most reliable in the Ubuntu 22.04 distrobox."
else
  die "Unsupported package manager. Install the dependencies listed in README.md manually."
fi
