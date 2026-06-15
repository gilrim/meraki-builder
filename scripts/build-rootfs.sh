#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
load_build_state

[[ -f "$BUILDROOT_DIR/.config" ]] || die "Buildroot is not prepared. Run make prepare first."
[[ -f "$KERNEL_ARTIFACT_DIR/vmlinuz" ]] || die "Missing kernel ELF"
[[ -f "$KERNEL_ARTIFACT_DIR/vmlinuz.bin" ]] || die "Missing compressed kernel binary"
[[ -f "$LOADER_ARTIFACT" ]] || die "Missing RedBoot loader"

cd "$BUILDROOT_DIR"
if bool_enabled "${CLEAN_BUILDROOT:-0}"; then
  log "Cleaning Buildroot output while preserving the download cache"
  make clean
fi

log "Prefetching Buildroot sources"
run_logged buildroot-download \
  make -j1 BR2_DL_DIR="$BUILDROOT_DL_DIR" \
  BR2_PRIMARY_SITE="https://sources.buildroot.net" source

log "Building root filesystem and complete NOR image"
export MS42P_KERNEL_ELF="$KERNEL_ARTIFACT_DIR/vmlinuz"
export MS42P_KERNEL_BIN="$KERNEL_ARTIFACT_DIR/vmlinuz.bin"
export MS42P_LOADER="$LOADER_ARTIFACT"
export MS42P_RELEASE="${MS42P_RELEASE:-$(date -u +%Y%m%d)}"
run_logged buildroot-build \
  make -j"$JOBS" BR2_DL_DIR="$BUILDROOT_DL_DIR" \
  BR2_PRIMARY_SITE="https://sources.buildroot.net"

ROOTFS="$BUILDROOT_DIR/output/images/rootfs.squashfs"
IMAGE="$BUILDROOT_DIR/output/images/ms42p-firmware.bin"
[[ -f "$ROOTFS" ]] || die "Buildroot did not produce rootfs.squashfs"
[[ -f "$IMAGE" ]] || die "The MS42P post-image script did not produce ms42p-firmware.bin"
[[ "$(file_size "$IMAGE")" -eq $((0x1000000)) ]] || die "Firmware image is not 16 MiB"

stamp="$(date -u +%Y%m%d-%H%M%S)"
name="ms42p-postmerkos-$stamp.bin"
if bool_enabled "${INCLUDE_UI:-0}"; then
  name="ms42p-postmerkos-webui-$stamp.bin"
fi
cp -f "$IMAGE" "$ARTIFACTS_DIR/$name"
cp -f "$ROOTFS" "$ARTIFACTS_DIR/rootfs.squashfs"
sha256sum "$ARTIFACTS_DIR/$name" > "$ARTIFACTS_DIR/$name.sha256"
sha256sum "$ARTIFACTS_DIR/rootfs.squashfs" > "$ARTIFACTS_DIR/rootfs.squashfs.sha256"
printf '%s\n' "$ARTIFACTS_DIR/$name" > "$ARTIFACTS_DIR/latest-image.txt"
touch "$STAMP_DIR/rootfs-built"
log "Created $ARTIFACTS_DIR/$name"
