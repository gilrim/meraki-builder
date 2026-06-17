#!/usr/bin/env bash
set -Eeuo pipefail
. "$(dirname "$0")/common.sh"
case "${1:-build}" in
  build) rm -rf "$MX80_BUILDROOT_DIR/output" ;;
  all) rm -rf "$MX80_WORK_DIR" "$MX80_ARTIFACT_DIR" ;;
  *) echo 'usage: clean.sh [build|all]' >&2; exit 2 ;;
esac
