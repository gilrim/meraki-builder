#!/usr/bin/env bash
set -Eeuo pipefail
MX80_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MX80_REPO_ROOT="$(cd "$MX80_SCRIPT_DIR/../.." && pwd)"
MX80_WORK_DIR="${MX80_WORK_DIR:-$MX80_REPO_ROOT/.work/mx80}"
MX80_DOWNLOAD_DIR="${MX80_DOWNLOAD_DIR:-$MX80_WORK_DIR/downloads}"
MX80_BUILDROOT_VERSION="${MX80_BUILDROOT_VERSION:-2020.02.8}"
MX80_BUILDROOT_ARCHIVE="$MX80_DOWNLOAD_DIR/buildroot-$MX80_BUILDROOT_VERSION.tar.bz2"
MX80_BUILDROOT_URL="${MX80_BUILDROOT_URL:-https://buildroot.org/downloads/buildroot-$MX80_BUILDROOT_VERSION.tar.bz2}"
MX80_BUILDROOT_DIR="$MX80_WORK_DIR/buildroot-$MX80_BUILDROOT_VERSION"
MX80_BOARD_SOURCE="$MX80_REPO_ROOT/buildroot/board/meraki/mx80"
MX80_BOARD_TARGET="$MX80_BUILDROOT_DIR/board/meraki/mx80"
MX80_ARTIFACT_DIR="${MX80_ARTIFACT_DIR:-$MX80_REPO_ROOT/artifacts/mx80}"
MX80_IMAGE="$MX80_BUILDROOT_DIR/output/images/ubi_image.bin"
MX80_JOBS="${JOBS:-$(nproc 2>/dev/null || echo 1)}"
mkdir -p "$MX80_WORK_DIR" "$MX80_DOWNLOAD_DIR" "$MX80_ARTIFACT_DIR"
mx80_log(){ printf '\n==> MX80: %s\n' "$*"; }
mx80_die(){ printf 'ERROR: MX80: %s\n' "$*" >&2; exit 1; }
mx80_need(){ command -v "$1" >/dev/null 2>&1 || mx80_die "required command not found: $1"; }
mx80_download(){
  local url=$1 destination=$2 temporary="$2.part"
  rm -f "$temporary"
  if command -v curl >/dev/null 2>&1; then curl --fail --location --retry 4 --output "$temporary" "$url"
  elif command -v wget >/dev/null 2>&1; then wget --tries=4 --output-document="$temporary" "$url"
  else mx80_die 'curl or wget is required'; fi
  test -s "$temporary" || mx80_die "downloaded file is empty: $url"
  mv -f "$temporary" "$destination"
}
