#!/usr/bin/env bash
set -Eeuo pipefail
. "$(dirname "$0")/common.sh"
"$MX80_SCRIPT_DIR/prepare.sh"
mx80_log 'Building the PowerPC toolchain, Fullerene kernel, and root filesystem'
make -C "$MX80_BUILDROOT_DIR" BR2_DL_DIR="$MX80_DOWNLOAD_DIR/buildroot-dl" -j"$MX80_JOBS"
"$MX80_SCRIPT_DIR/validate.py" "$MX80_IMAGE"
cp -f "$MX80_IMAGE" "$MX80_ARTIFACT_DIR/postmerkos-mx80.bin"
( cd "$MX80_ARTIFACT_DIR" && sha256sum postmerkos-mx80.bin > postmerkos-mx80.bin.sha256 )
mx80_log "Firmware: $MX80_ARTIFACT_DIR/postmerkos-mx80.bin"
