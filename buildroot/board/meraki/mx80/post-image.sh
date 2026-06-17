#!/usr/bin/env bash
set -Eeuo pipefail

: "${BINARIES_DIR:?Buildroot must provide BINARIES_DIR}"
DTB_OFFSET=$((0x4000))
KERNEL_OFFSET=$((0x20000))
INITRAMFS_OFFSET=$((0x400000))
HEADER_SIZE=$((0x400))
IMAGE_LIMIT=$((0x1900000))

DTB="$BINARIES_DIR/fullerene.dtb"
KERNEL="$BINARIES_DIR/uImage"
INITRAMFS="$BINARIES_DIR/rootfs.cpio.uboot"
PAYLOAD="$BINARIES_DIR/part1_data.bin"
IMAGE="$BINARIES_DIR/ubi_image.bin"

for input in "$DTB" "$KERNEL" "$INITRAMFS"; do
    [[ -s "$input" ]] || { echo "MX80 post-image: missing $input" >&2; exit 1; }
done

dtb_size=$(stat --format '%s' "$DTB")
kernel_size=$(stat --format '%s' "$KERNEL")
(( dtb_size <= KERNEL_OFFSET - DTB_OFFSET )) || { echo 'MX80 DTB exceeds its region' >&2; exit 1; }
(( kernel_size <= INITRAMFS_OFFSET - KERNEL_OFFSET )) || { echo 'MX80 kernel exceeds its region' >&2; exit 1; }

python3 - "$DTB" "$KERNEL" "$INITRAMFS" "$PAYLOAD" <<'PY'
from pathlib import Path
import sys

dtb, kernel, initramfs, output = map(Path, sys.argv[1:])
header_size = 0x400
positions = ((0x4000, dtb.read_bytes()), (0x20000, kernel.read_bytes()), (0x400000, initramfs.read_bytes()))
with output.open('wb') as stream:
    cursor = header_size
    for offset, data in positions:
        if cursor > offset:
            raise SystemExit(f'component overlaps MX80 offset 0x{offset:x}')
        stream.write(b'\0' * (offset - cursor))
        stream.write(data)
        cursor = offset + len(data)
PY

python3 - "$PAYLOAD" "$IMAGE" <<'PY'
from pathlib import Path
import hashlib
import struct
import sys

payload_path, image_path = map(Path, sys.argv[1:])
payload = payload_path.read_bytes()
header = bytearray()
header += bytes.fromhex('8e73ed8a')
header += struct.pack('>I', 0x400)
header += struct.pack('>I', len(payload))
header += hashlib.sha1(payload).digest()
header += bytes.fromhex('a1f0beef0006000100020000004000000000000000004000')
if len(header) > 0x400:
    raise SystemExit('MX80 header exceeds 0x400 bytes')
header += b'\0' * (0x400 - len(header))
image_path.write_bytes(header + payload)
PY

image_size=$(stat --format '%s' "$IMAGE")
(( image_size <= IMAGE_LIMIT )) || { echo "MX80 image exceeds 0x$(printf '%x' "$IMAGE_LIMIT") bytes" >&2; exit 1; }
printf 'MX80 image: %s (%s bytes)\n' "$IMAGE" "$image_size"
