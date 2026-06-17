#!/usr/bin/env bash
set -Eeuo pipefail
. "$(dirname "$0")/common.sh"
for tool in make tar bzip2 rsync; do mx80_need "$tool"; done
if [[ ! -s "$MX80_BUILDROOT_ARCHIVE" ]]; then
  mx80_log "Downloading Buildroot $MX80_BUILDROOT_VERSION"
  mx80_download "$MX80_BUILDROOT_URL" "$MX80_BUILDROOT_ARCHIVE"
fi
if [[ ! -d "$MX80_BUILDROOT_DIR" ]]; then
  mx80_log "Extracting Buildroot $MX80_BUILDROOT_VERSION"
  tar -C "$MX80_WORK_DIR" -xjf "$MX80_BUILDROOT_ARCHIVE"
fi
mx80_log 'Installing the MX80 board definition'
rm -rf "$MX80_BOARD_TARGET"
mkdir -p "$(dirname "$MX80_BOARD_TARGET")"
cp -a "$MX80_BOARD_SOURCE" "$MX80_BOARD_TARGET"
cp "$MX80_BOARD_TARGET/buildroot-config" "$MX80_BUILDROOT_DIR/.config"
make -C "$MX80_BUILDROOT_DIR" BR2_DL_DIR="$MX80_DOWNLOAD_DIR/buildroot-dl" olddefconfig
mx80_log "Prepared $MX80_BUILDROOT_DIR"
