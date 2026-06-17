#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DUMP_FILE="${1:-mtd12-original.dat}"
OUTPUT_DIR="${2:-$SCRIPT_DIR}"
BIN_DIR="$OUTPUT_DIR/bin"
RAMDISK_DIR="$OUTPUT_DIR/ramdisk"
RAMDISK_GZ="$OUTPUT_DIR/ramdisk.gz"

[[ -f "$DUMP_FILE" ]] || { echo "firmware volume not found: $DUMP_FILE" >&2; exit 1; }
mkdir -p "$BIN_DIR"

if command -v find_hdr >/dev/null 2>&1; then
  FIND_HDR=find_hdr
else
  HOST_HELPER="${TMPDIR:-/tmp}/postmerkos-find_hdr"
  cc -O2 -Wall -Wextra "$SCRIPT_DIR/../firmware-analysis/find_hdr.c" -o "$HOST_HELPER"
  FIND_HDR="$HOST_HELPER"
fi
RAMDISK_OFFSET="$($FIND_HDR -g "$DUMP_FILE")"
[[ "$RAMDISK_OFFSET" =~ ^[0-9]+$ ]] && (( RAMDISK_OFFSET > 1024 )) || {
  echo 'unable to locate the embedded gzip ramdisk' >&2
  exit 1
}

dd if="$DUMP_FILE" of="$BIN_DIR/mtd12-header" bs=1024 count=1 status=none
dd if="$DUMP_FILE" of="$BIN_DIR/mtd12-vmlinux" bs=1 skip=1024 count=$((RAMDISK_OFFSET - 1024)) status=none
dd if="$DUMP_FILE" of="$RAMDISK_GZ" bs=1 skip="$RAMDISK_OFFSET" status=none
file "$RAMDISK_GZ" | grep -q gzip || { echo 'embedded ramdisk is not a gzip archive' >&2; exit 1; }
rm -rf "$RAMDISK_DIR"
mkdir -p "$RAMDISK_DIR"
( cd "$RAMDISK_DIR" && gzip -dc "$RAMDISK_GZ" | cpio -idmu )
printf 'Extracted ramdisk to %s\n' "$RAMDISK_DIR"
