#!/usr/bin/env bash
set -Eeuo pipefail
. "$(dirname "$0")/common.sh"
"$MX80_SCRIPT_DIR/prepare.sh"
exec make -C "$MX80_BUILDROOT_DIR" BR2_DL_DIR="$MX80_DOWNLOAD_DIR/buildroot-dl" menuconfig
