#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
load_build_state

# Keep Buildroot host tools in the same supported Ubuntu environment as the
# pinned kernel toolchain. This also protects direct `make rootfs` invocations
# that do not pass through build-all.sh.
if [[ "${MS42P_IN_DISTROBOX:-0}" != 1 ]] && ! bool_enabled "${ALLOW_UNSUPPORTED_HOST_BUILD:-0}"; then
  if bool_enabled "${USE_DISTROBOX:-0}"; then
    exec "$SCRIPT_DIR/distrobox-run.sh" env \
      INCLUDE_UI="${INCLUDE_UI:-0}" \
      CLEAN_BUILDROOT="${CLEAN_BUILDROOT:-0}" \
      ./scripts/build-rootfs.sh
  elif command -v pacman >/dev/null 2>&1 && command -v distrobox >/dev/null 2>&1; then
    if ask_yes_no "Build the root filesystem in Ubuntu 22.04 Distrobox?" yes; then
      exec "$SCRIPT_DIR/distrobox-run.sh" env \
        INCLUDE_UI="${INCLUDE_UI:-0}" \
        CLEAN_BUILDROOT="${CLEAN_BUILDROOT:-0}" \
        ./scripts/build-rootfs.sh
    fi
    warn "Continuing with the unsupported Arch/CachyOS host compiler."
  fi
fi

[[ -f "$BUILDROOT_DIR/.config" ]] || die "Buildroot is not prepared. Run make prepare first."
[[ -f "$KERNEL_ARTIFACT_DIR/vmlinuz" ]] || die "Missing kernel ELF"
[[ -f "$KERNEL_ARTIFACT_DIR/vmlinuz.bin" ]] || die "Missing compressed kernel binary"
[[ -f "$LOADER_ARTIFACT" ]] || die "Missing source-built LinuxLoader boot region"
[[ -f "$LOADER_MANIFEST" ]] || die "Missing LinuxLoader capability manifest"

# Refresh and validate both module staging paths for direct build-rootfs.sh
# invocations and reused Buildroot output trees.
"$SCRIPT_DIR/stage-vendor-modules.sh" \
  "$GENERATED_OVERLAY/lib/modules" \
  "$BUILDROOT_DIR/board/meraki/ms220/vendor-modules"

cd "$BUILDROOT_DIR"

# Buildroot compiles a substantial set of host utilities.  Reusing output made
# by another distribution/compiler is unsafe.  Track the environment and clean
# automatically when it changes.  An existing untracked output tree is cleaned
# once, which also recovers from the GCC 16 host-binutils failure.
env_stamp="$BUILDROOT_DIR/.ms42p-build-environment"
os_id="unknown"
os_version="unknown"
if [[ -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  os_id="${ID:-unknown}"
  os_version="${VERSION_ID:-unknown}"
fi
host_cc="$(gcc -dumpfullversion -dumpversion 2>/dev/null || printf unknown)"
if [[ "${MS42P_IN_DISTROBOX:-0}" == 1 ]]; then
  current_env="distrobox:${DISTROBOX_IMAGE}:${os_id}:${os_version}:gcc-${host_cc}"
else
  current_env="host:${os_id}:${os_version}:gcc-${host_cc}"
fi

need_environment_clean=0
if [[ -d output/build ]]; then
  if [[ ! -f "$env_stamp" ]]; then
    warn "Existing Buildroot output has no build-environment stamp; cleaning it before reuse."
    need_environment_clean=1
  elif [[ "$(cat "$env_stamp")" != "$current_env" ]]; then
    warn "Buildroot environment changed from '$(cat "$env_stamp")' to '$current_env'; cleaning output."
    need_environment_clean=1
  fi
fi

if bool_enabled "${CLEAN_BUILDROOT:-0}" || (( need_environment_clean )); then
  log "Cleaning Buildroot output while preserving the download cache"
  make clean
fi
printf '%s\n' "$current_env" > "$env_stamp"

log "Prefetching Buildroot sources"
run_logged buildroot-download \
  make -j1 BR2_DL_DIR="$BUILDROOT_DL_DIR" \
  BR2_PRIMARY_SITE="https://sources.buildroot.net" source

log "Building root filesystem and complete NOR image"
export MS42P_KERNEL_ELF="$KERNEL_ARTIFACT_DIR/vmlinuz"
export MS42P_KERNEL_BIN="$KERNEL_ARTIFACT_DIR/vmlinuz.bin"
export MS42P_LOADER="$LOADER_ARTIFACT"
export MS42P_LOADER_MANIFEST="$LOADER_MANIFEST"
export MS42P_PAYLOAD_PACKER="$LOADER_PAYLOAD_PACKER"
export MS42P_RECOVERY_ARTIFACT_DIR="$RECOVERY_ARTIFACT_DIR"
export MS42P_RELEASE="${MS42P_RELEASE:-$(date -u +%Y%m%d)}"
run_logged buildroot-build \
  make -j"$JOBS" BR2_DL_DIR="$BUILDROOT_DL_DIR" \
  BR2_PRIMARY_SITE="https://sources.buildroot.net"

ROOTFS="$BUILDROOT_DIR/output/images/rootfs.squashfs"
IMAGE="$BUILDROOT_DIR/output/images/ms42p-firmware.bin"
[[ -f "$ROOTFS" ]] || die "Buildroot did not produce rootfs.squashfs"
[[ -f "$IMAGE" ]] || die "The MS42P post-image script did not produce ms42p-firmware.bin"
[[ "$(file_size "$IMAGE")" -eq $((0x1000000)) ]] || die "Firmware image is not 16 MiB"

python3 "$VENDOR_MODULE_TOOL" verify \
  "$BUILDROOT_DIR/output/target/lib/modules" \
  --required-file "$VENDOR_MODULE_REQUIRED" --quiet || \
  die "Built target does not contain the complete platform module matrix"


stamp="$(date -u +%Y%m%d-%H%M%S)"
name="ms42p-postmerkos-$stamp.bin"
if bool_enabled "${INCLUDE_UI:-0}"; then
  name="ms42p-postmerkos-webui-$stamp.bin"
fi
cp -f "$IMAGE" "$ARTIFACTS_DIR/$name"
cp -f "$ROOTFS" "$ARTIFACTS_DIR/rootfs.squashfs"

release_manifest="$BUILDROOT_DIR/output/images/postmerkos-release.json"
if [[ -f "$release_manifest" ]]; then
  python3 "$SCRIPT_DIR/write-artifact-manifest.py" \
    "$release_manifest" \
    "$ARTIFACTS_DIR/$name.manifest.json" \
    "$ARTIFACTS_DIR/$name" \
    "$ARTIFACTS_DIR/rootfs.squashfs" \
    "$LOADER_MANIFEST" \
    "$RECOVERY_ARTIFACT_DIR" \
    "$LOADER_SOURCE_VERSION_FILE" \
    "$LOADER_SOURCE_REVISION_FILE"
  write_sha256_sidecar "$ARTIFACTS_DIR/$name.manifest.json"
else
  warn "Release manifest was not produced; modern artifact manifest sidecar omitted."
fi
write_sha256_sidecar "$ARTIFACTS_DIR/$name"
write_sha256_sidecar "$ARTIFACTS_DIR/rootfs.squashfs"
printf '%s\n' "$ARTIFACTS_DIR/$name" > "$ARTIFACTS_DIR/latest-image.txt"
touch "$STAMP_DIR/rootfs-built"
log "Created $ARTIFACTS_DIR/$name"
