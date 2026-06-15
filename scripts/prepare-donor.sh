#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
need unsquashfs

FULL_IMAGE_SIZE=$((0x1000000))
ROOTFS_REGION_SIZE=$((0x800000))
LOADER_SIZE=$((0x40000))

find_donor() {
  local candidate
  if [[ -n "${DONOR_IMAGE:-}" ]]; then
    printf '%s\n' "$DONOR_IMAGE"
    return
  fi
  for candidate in \
    "$INPUTS_DIR/postmerkOS-20240818.bin" \
    "$INPUTS_DIR/donor-firmware.bin" \
    "$INPUTS_DIR/good-rootfs.squashfs"; do
    [[ -f "$candidate" ]] && { printf '%s\n' "$candidate"; return; }
  done
}

DONOR="$(find_donor || true)"
if [[ -z "$DONOR" ]]; then
  if bool_enabled "${AUTO_DOWNLOAD_DONOR:-0}" || ask_yes_no "No donor image was found in inputs/. Download the known PostmerkOS donor?" yes; then
    download_file "$DONOR_URL" "$DONOR_DEFAULT"
    DONOR="$DONOR_DEFAULT"
  else
    die "Place a donor image in $INPUTS_DIR or set DONOR_IMAGE=/path/to/file."
  fi
fi
[[ -f "$DONOR" ]] || die "Donor input does not exist: $DONOR"

rm -rf "$DONOR_ROOT"
mkdir -p "$DONOR_ROOT" "$ARTIFACTS_DIR"

case "$DONOR" in
  *.squashfs)
    rootfs_image="$DONOR"
    ;;
  *)
    size="$(file_size "$DONOR")"
    [[ "$size" -eq "$FULL_IMAGE_SIZE" ]] || \
      die "Donor firmware must be exactly 16 MiB; got $size bytes"
    dd if="$DONOR" of="$DONOR_ROOTFS_REGION" bs=1M skip=3 count=8 status=none
    dd if="$DONOR" of="$LOADER_ARTIFACT" bs=64K count=4 status=none
    [[ "$(file_size "$DONOR_ROOTFS_REGION")" -eq "$ROOTFS_REGION_SIZE" ]] || \
      die "Extracted donor SquashFS region is not 8 MiB"
    [[ "$(file_size "$LOADER_ARTIFACT")" -eq "$LOADER_SIZE" ]] || \
      die "Extracted donor loader is not 256 KiB"
    rootfs_image="$DONOR_ROOTFS_REGION"
    ;;
esac

unsquashfs -f -d "$DONOR_ROOT" "$rootfs_image"
[[ -d "$DONOR_ROOT/lib/modules" ]] || die "Donor /lib/modules is missing"
for module in \
  elts_meraki.ko merakiclick.ko proclikefs.ko \
  jaguar_dual/vc_click.ko jaguar_dual/vtss_core.ko; do
  [[ -f "$DONOR_ROOT/lib/modules/$module" ]] || die "Required donor module missing: $module"
done

if [[ ! -f "$LOADER_ARTIFACT" ]]; then
  if [[ -f "$INPUTS_DIR/loader1.bin" ]]; then
    cp -f "$INPUTS_DIR/loader1.bin" "$LOADER_ARTIFACT"
  elif bool_enabled "${AUTO_DOWNLOAD_REDBOOT:-0}" || ask_yes_no "The SquashFS donor contains no bootloader. Download the known RedBoot loader?" yes; then
    download_file "$REDBOOT_URL" "$LOADER_ARTIFACT"
  else
    die "A 256 KiB loader1.bin is required."
  fi
fi
[[ "$(file_size "$LOADER_ARTIFACT")" -eq "$LOADER_SIZE" ]] || \
  die "loader1.bin must be exactly 256 KiB"

find "$DONOR_ROOT/lib/modules" -type f -printf '%P\n' | sort > "$ARTIFACTS_DIR/donor-module-files.txt"
sha256sum "$DONOR" > "$ARTIFACTS_DIR/donor-input.sha256"
sha256sum "$LOADER_ARTIFACT" > "$ARTIFACTS_DIR/loader1.bin.sha256"
touch "$STAMP_DIR/donor-prepared"
log "Donor modules and loader are ready"
