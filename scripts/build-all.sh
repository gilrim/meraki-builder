#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

# On rolling-release Arch/CachyOS hosts, run the complete firmware build in the
# supported Ubuntu container.  Previously only build-kernel.sh entered
# Distrobox, so build-all.sh returned to the host and Buildroot compiled its
# host tools with the host GCC.  GCC 16 cannot compile the binutils 2.38 bundled
# by Buildroot 2023.02.4.
if [[ "${MS42P_IN_DISTROBOX:-0}" != 1 ]]; then
  if bool_enabled "${USE_DISTROBOX:-0}"; then
    exec "$SCRIPT_DIR/distrobox-run.sh" env \
      INCLUDE_UI="${INCLUDE_UI:-ask}" \
      ./scripts/build-all.sh
  elif command -v pacman >/dev/null 2>&1 && command -v distrobox >/dev/null 2>&1; then
    if ask_yes_no "Run the complete firmware build in Ubuntu 22.04 Distrobox?" yes; then
      exec "$SCRIPT_DIR/distrobox-run.sh" env \
        INCLUDE_UI="${INCLUDE_UI:-ask}" \
        ./scripts/build-all.sh
    fi
    export ALLOW_UNSUPPORTED_HOST_BUILD=1
    warn "Continuing on the Arch/CachyOS host. Buildroot 2023.02.4 is not compatible with GCC 16 without additional patches."
  fi
fi

missing=()
for cmd in git make tar xz rsync python3 sha256sum readelf unsquashfs mkfs.jffs2 file; do
  command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
done
if (( ${#missing[@]} )); then
  warn "Missing required commands: ${missing[*]}"
  if bool_enabled "${AUTO_INSTALL_DEPS:-0}" || ask_yes_no "Install build dependencies now?" yes; then
    "$SCRIPT_DIR/install-deps.sh"
  else
    die "Install the missing build dependencies before continuing."
  fi
fi

case "${INCLUDE_UI:-ask}" in
  ask|'')
    if ask_yes_no "Include and build Gadorach/postmerkos-ui ($UI_REF)?" yes; then
      INCLUDE_UI=1
    else
      INCLUDE_UI=0
    fi
    ;;
  *)
    bool_enabled "$INCLUDE_UI" && INCLUDE_UI=1 || INCLUDE_UI=0
    ;;
esac
export INCLUDE_UI

# Always select the pinned local revision. This does not contact the network when
# the commit is already present in the existing checkout.
"$SCRIPT_DIR/prepare-sources.sh"

if [[ ! -f "$KERNEL_ARTIFACT_DIR/vmlinuz.bin" || ! -f "$KERNEL_HEADERS_TARBALL" ]] || \
   bool_enabled "${REBUILD_KERNEL:-0}"; then
  if bool_enabled "${AUTO_BUILD_KERNEL:-0}" || ask_yes_no "A usable kernel build is missing. Build it now?" yes; then
    "$SCRIPT_DIR/build-kernel.sh"
  else
    die "Kernel artifacts are required."
  fi
else
  log "Reusing existing kernel artifacts"
fi

if [[ ! -d "$DONOR_ROOT/lib/modules" || ! -f "$LOADER_ARTIFACT" ]] || \
   bool_enabled "${REEXTRACT_DONOR:-0}"; then
  "$SCRIPT_DIR/prepare-donor.sh"
else
  log "Reusing extracted donor modules and loader"
fi

if (( INCLUDE_UI )); then
  "$SCRIPT_DIR/build-ui.sh"
else
  log "Building without the optional web interface"
fi

"$SCRIPT_DIR/prepare-buildroot.sh"
"$SCRIPT_DIR/build-rootfs.sh"
"$SCRIPT_DIR/validate-image.sh"

IMAGE="$(cat "$ARTIFACTS_DIR/latest-image.txt")"
printf '\nBuild complete.\nImage: %s\nSHA256: %s\n' \
  "$IMAGE" "$(sha256sum "$IMAGE" | awk '{print $1}')"
