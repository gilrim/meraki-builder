#!/usr/bin/env bash
set -Eeuo pipefail

: "${BINARIES_DIR:?Buildroot BINARIES_DIR is not set}"
: "${HOST_DIR:?Buildroot HOST_DIR is not set}"
: "${MS42P_KERNEL_ELF:?MS42P_KERNEL_ELF is not set}"
: "${MS42P_KERNEL_BIN:?MS42P_KERNEL_BIN is not set}"
: "${MS42P_LOADER:?MS42P_LOADER is not set}"

ROOTFS="$BINARIES_DIR/rootfs.squashfs"
OUTPUT="$BINARIES_DIR/ms42p-firmware.bin"
WORK="$BINARIES_DIR/ms42p-image-parts"
MKFS_JFFS2="$HOST_DIR/sbin/mkfs.jffs2"

LOADER_REGION=$((0x040000))
KERNEL_REGION=$((0x2c0000))
ROOTFS_REGION=$((0x800000))
JFFS2_REGION=$((0x500000))
TOTAL_SIZE=$((0x1000000))
LOAD_ADDRESS=$((0x81000000))

for input in "$MS42P_KERNEL_ELF" "$MS42P_KERNEL_BIN" "$MS42P_LOADER" "$ROOTFS"; do
    [[ -f "$input" ]] || { echo "Missing image input: $input" >&2; exit 1; }
done
[[ -x "$MKFS_JFFS2" ]] || { echo "Missing Buildroot host mkfs.jffs2: $MKFS_JFFS2" >&2; exit 1; }

loader_size="$(stat -c %s "$MS42P_LOADER")"
kernel_size="$(stat -c %s "$MS42P_KERNEL_BIN")"
rootfs_size="$(stat -c %s "$ROOTFS")"
entry="$(readelf -h "$MS42P_KERNEL_ELF" | awk '/Entry point address/ {print $4}')"
rootfs_remaining=$((ROOTFS_REGION - rootfs_size))
rootfs_percent="$(awk -v used="$rootfs_size" -v max="$ROOTFS_REGION" 'BEGIN { printf "%.2f", used * 100 / max }')"
{
    printf 'SquashFS bytes used: %s\n' "$rootfs_size"
    printf 'SquashFS maximum:    %s\n' "$ROOTFS_REGION"
    printf 'SquashFS usage:      %s%%\n' "$rootfs_percent"
    printf 'SquashFS remaining:  %s\n' "$rootfs_remaining"
    if [[ -n "${TARGET_DIR:-}" && -d "${TARGET_DIR:-}" ]]; then
        printf '\nLargest target files:\n'
        find "$TARGET_DIR" -type f -printf '%s %p\n' | sort -nr | head -n 40
    fi
} | tee "$BINARIES_DIR/squashfs-usage.txt"

[[ "$loader_size" -eq "$LOADER_REGION" ]] || { echo "Loader must be exactly 256 KiB" >&2; exit 1; }
(( kernel_size + 32 <= KERNEL_REGION )) || { echo "Kernel exceeds the 2816 KiB region" >&2; exit 1; }
(( rootfs_size > 0 && rootfs_size <= ROOTFS_REGION )) || { echo "SquashFS exceeds the 8 MiB region" >&2; exit 1; }
[[ "$entry" == 0x81000000 ]] || { echo "Unexpected kernel entry point: $entry" >&2; exit 1; }

rm -rf "$WORK"
mkdir -p "$WORK/jffs2-root/.upper/etc" "$WORK/jffs2-root/.work/etc" \
    "$WORK/jffs2-root/.upper/root" "$WORK/jffs2-root/.work/root"

python3 - "$WORK/boot1-header.bin" "$kernel_size" <<'PY'
import struct
import sys
path, length = sys.argv[1], int(sys.argv[2])
with open(path, 'wb') as stream:
    stream.write(b'SPIM')
    stream.write(struct.pack('<I', 0x81000000))
    stream.write(struct.pack('<I', length))
    stream.write(struct.pack('<I', 0x81000000))
    stream.write(b'\0' * 16)
PY

cat "$WORK/boot1-header.bin" "$MS42P_KERNEL_BIN" > "$WORK/kernel.region"
truncate -s "$KERNEL_REGION" "$WORK/kernel.region"
cp "$ROOTFS" "$WORK/rootfs.region"
truncate -s "$ROOTFS_REGION" "$WORK/rootfs.region"
"$MKFS_JFFS2" --pad="$JFFS2_REGION" -l -n -X lzo -x zlib -y 40:lzo \
    -r "$WORK/jffs2-root" -o "$WORK/overlay.region"

cat "$MS42P_LOADER" "$WORK/kernel.region" "$WORK/rootfs.region" \
    "$WORK/overlay.region" > "$OUTPUT"
[[ "$(stat -c %s "$OUTPUT")" -eq "$TOTAL_SIZE" ]] || {
    echo "Generated firmware is not exactly 16 MiB" >&2
    exit 1
}

cp -f "$WORK/boot1-header.bin" "$WORK/kernel.region" "$WORK/rootfs.region" \
    "$WORK/overlay.region" "$BINARIES_DIR/"
if [[ -n "${TARGET_DIR:-}" && -f "$TARGET_DIR/etc/postmerkos-release.json" ]]; then
    cp -f "$TARGET_DIR/etc/postmerkos-release.json" "$BINARIES_DIR/postmerkos-release.json"
fi
(cd "$BINARIES_DIR" && sha256sum "$(basename "$OUTPUT")" > "$(basename "$OUTPUT").sha256")
printf 'Generated %s\n' "$OUTPUT"
