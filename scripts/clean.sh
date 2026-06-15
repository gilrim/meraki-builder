#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
mode="${1:-build}"
case "$mode" in
  build)
    if [[ -d "$BUILDROOT_DIR" ]]; then
      make -C "$BUILDROOT_DIR" clean || true
    fi
    rm -rf "$GENERATED_OVERLAY" "$BUILD_DIR/rootfs-validation" "$STAMP_DIR"
    ;;
  all)
    rm -rf "$WORK_DIR" "$ARTIFACTS_DIR"
    ;;
  *) die "Usage: $0 [build|all]" ;;
esac
