#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
load_build_state
need python3
need unsquashfs

IMAGE="${1:-}"
if [[ -z "$IMAGE" && -f "$ARTIFACTS_DIR/latest-image.txt" ]]; then
  IMAGE="$(cat "$ARTIFACTS_DIR/latest-image.txt")"
fi
[[ -f "$IMAGE" ]] || die "No firmware image was supplied and latest-image.txt is unavailable"
[[ "$(file_size "$IMAGE")" -eq $((0x1000000)) ]] || die "Firmware image is not exactly 16 MiB"

python3 - "$IMAGE" "$KERNEL_ARTIFACT_DIR/vmlinuz.bin" <<'PY'
from pathlib import Path
import hashlib, struct, sys
image=Path(sys.argv[1]).read_bytes()
kernel=Path(sys.argv[2]).read_bytes()
assert image[0x40000:0x40004] == b'SPIM', 'missing SPIM header at 0x40000'
load,length,entry=struct.unpack_from('<III',image,0x40004)
assert load == 0x81000000, f'unexpected load address: 0x{load:08x}'
assert entry == 0x81000000, f'unexpected entry address: 0x{entry:08x}'
assert length == len(kernel), f'header length {length} != kernel length {len(kernel)}'
embedded=image[0x40020:0x40020+length]
assert hashlib.sha256(embedded).digest() == hashlib.sha256(kernel).digest(), 'embedded kernel mismatch'
assert image[0x300000:0x300004] == b'hsqs', 'missing SquashFS magic at 0x300000'
print(f'SPIM load/entry: 0x{load:08x}')
print(f'Kernel payload:   {length} bytes')
PY

VERIFY_DIR="$BUILD_DIR/rootfs-validation"
rm -rf "$VERIFY_DIR"
unsquashfs -quiet -d "$VERIFY_DIR" "$ARTIFACTS_DIR/rootfs.squashfs"

required=(
  etc/fstab
  etc/init.d/S08kmods
  etc/init.d/S09clickinit
  etc/init.d/S10clickconfig
  etc/init.d/S11poe
  lib/modules/elts_meraki.ko
  lib/modules/merakiclick.ko
  lib/modules/proclikefs.ko
  lib/modules/jaguar_dual/vc_click.ko
  lib/modules/jaguar_dual/vtss_core.ko
  bin/pd690xx
)
if bool_enabled "${INCLUDE_UI:-0}"; then
  required+=(www/index.html bin/configd usr/bin/uhttpd etc/init.d/S15configd etc/init.d/S16uhttpd)
fi
for path in "${required[@]}"; do
  [[ -e "$VERIFY_DIR/$path" ]] || die "Rootfs verification failed: missing /$path"
done

if ! bool_enabled "${INCLUDE_UI:-0}"; then
  [[ ! -e "$VERIFY_DIR/etc/init.d/S15configd" ]] || die "Base image unexpectedly contains S15configd"
  [[ ! -e "$VERIFY_DIR/etc/init.d/S16uhttpd" ]] || die "Base image unexpectedly contains S16uhttpd"
fi

find "$VERIFY_DIR/etc/init.d" -maxdepth 1 -type f -printf '%f\n' | sort \
  > "$ARTIFACTS_DIR/effective-init-scripts.txt"
find "$VERIFY_DIR/lib/modules" -type f -printf '%P\n' | sort \
  > "$ARTIFACTS_DIR/effective-module-files.txt"
sha256sum "$IMAGE" > "$IMAGE.sha256"
touch "$STAMP_DIR/image-validated"
log "Validated $IMAGE"
