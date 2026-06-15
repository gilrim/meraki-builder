#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
WORK_DIR="${MS42P_WORK_DIR:-$REPO_ROOT/.work}"
INPUTS_DIR="${MS42P_INPUTS_DIR:-$REPO_ROOT/inputs}"
SOURCES_DIR="$WORK_DIR/sources"
BUILD_DIR="$WORK_DIR/build"
EXTRACTED_DIR="$WORK_DIR/extracted"
GENERATED_OVERLAY="$WORK_DIR/generated-overlay"
ARTIFACTS_DIR="${MS42P_ARTIFACTS_DIR:-$REPO_ROOT/artifacts}"
LOG_DIR="$WORK_DIR/logs"
STAMP_DIR="$WORK_DIR/stamps"
STATE_FILE="$WORK_DIR/build-options.env"
DOWNLOAD_DIR="${MS42P_DOWNLOAD_DIR:-$WORK_DIR/downloads}"

BUILDROOT_VERSION="${BUILDROOT_VERSION:-2023.02.4}"
BUILDROOT_ARCHIVE="$DOWNLOAD_DIR/buildroot-$BUILDROOT_VERSION.tar.xz"
BUILDROOT_DIR="$BUILD_DIR/buildroot-$BUILDROOT_VERSION"
BUILDROOT_DL_DIR="${BUILDROOT_DL_DIR:-$DOWNLOAD_DIR/buildroot-dl}"

SWITCH_REPO_URL="${SWITCH_REPO_URL:-https://github.com/halmartin/switch-11-22-ms220.git}"
SWITCH_REF="${SWITCH_REF:-d167da8b01e46e29bf3347b8952ce9063ba75d29}"
SWITCH_DIR="$SOURCES_DIR/switch-11-22-ms220"
OPENWRT_DIR="$SWITCH_DIR/openwrt"
KERNEL_DIR="$SWITCH_DIR/linux-3.18"
CROSS_COMPILE="$OPENWRT_DIR/staging_dir_mipsel_nofpu_3.18/bin/mipsel-linux-musl-"
KERNEL_HEADERS_TARBALL="$BUILD_DIR/linux-3.18.123.tar.bz2"
KERNEL_ARTIFACT_DIR="$ARTIFACTS_DIR/kernel"

UI_REPO_URL="${UI_REPO_URL:-https://github.com/Gadorach/postmerkos-ui.git}"
UI_REF="${UI_REF:-ms42p-dev}"
UI_DIR="${UI_DIR:-$SOURCES_DIR/postmerkos-ui}"
NODE_VERSION="${NODE_VERSION:-22.14.0}"

DONOR_URL="${DONOR_URL:-https://watchmysys.com/files/meraki/ms220/postmerkOS-20240818.bin}"
DONOR_DEFAULT="$INPUTS_DIR/postmerkOS-20240818.bin"
DONOR_ROOT="$EXTRACTED_DIR/donor-rootfs"
DONOR_ROOTFS_REGION="$EXTRACTED_DIR/donor-rootfs-region.squashfs"
LOADER_ARTIFACT="$ARTIFACTS_DIR/loader1.bin"
REDBOOT_URL="${REDBOOT_URL:-https://github.com/halmartin/MS42-GPL-sources-3-18-122/raw/master/redboot/redboot-nocrc-sz.bin}"

JOBS="${JOBS:-$(nproc 2>/dev/null || printf '1')}"
DISTROBOX_NAME="${DISTROBOX_NAME:-meraki-build}"
DISTROBOX_IMAGE="${DISTROBOX_IMAGE:-ubuntu:22.04}"

mkdir -p "$INPUTS_DIR" "$SOURCES_DIR" "$BUILD_DIR" "$EXTRACTED_DIR" \
  "$GENERATED_OVERLAY" "$ARTIFACTS_DIR" "$LOG_DIR" "$STAMP_DIR" \
  "$DOWNLOAD_DIR" "$BUILDROOT_DL_DIR"

log() { printf '\n==> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"; }

is_interactive() { [[ -t 0 && -t 1 && "${NONINTERACTIVE:-0}" != 1 ]]; }

ask_yes_no() {
  local question="$1" default="${2:-yes}" answer suffix
  case "$default" in
    yes) suffix='[Y/n]' ;;
    no) suffix='[y/N]' ;;
    *) die "Invalid ask_yes_no default: $default" ;;
  esac

  if ! is_interactive; then
    [[ "$default" == yes ]]
    return
  fi

  read -r -p "$question $suffix " answer
  answer="${answer,,}"
  if [[ -z "$answer" ]]; then
    [[ "$default" == yes ]]
  else
    [[ "$answer" == y || "$answer" == yes ]]
  fi
}

bool_enabled() {
  case "${1:-}" in
    1|y|Y|yes|YES|true|TRUE|on|ON|enabled) return 0 ;;
    *) return 1 ;;
  esac
}

run_logged() {
  local name="$1"; shift
  mkdir -p "$LOG_DIR"
  "$@" 2>&1 | tee "$LOG_DIR/$name.log"
}

download_file() {
  local url="$1" dest="$2" tmp="$2.part"
  mkdir -p "$(dirname "$dest")"
  rm -f "$tmp"
  if command -v curl >/dev/null 2>&1; then
    curl --fail --location --retry 4 --retry-delay 2 --connect-timeout 20 \
      --output "$tmp" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget --tries=4 --timeout=30 --output-document="$tmp" "$url"
  else
    die "curl or wget is required to download $url"
  fi
  [[ -s "$tmp" ]] || die "Downloaded file is empty: $url"
  mv -f "$tmp" "$dest"
}

clone_or_update_ref() {
  local url="$1" dir="$2" ref="$3" label="${4:-repository}"
  need git

  if [[ ! -d "$dir/.git" ]]; then
    log "Cloning $label"
    git clone "$url" "$dir"
  fi

  if [[ "${ALLOW_DIRTY_SOURCES:-0}" != 1 && -n "$(git -C "$dir" status --porcelain)" ]]; then
    die "$label has local changes in $dir. Commit/stash them or set ALLOW_DIRTY_SOURCES=1."
  fi

  log "Selecting $label revision $ref"
  if [[ "$ref" =~ ^[0-9a-fA-F]{40}$ ]]; then
    if ! git -C "$dir" cat-file -e "$ref^{commit}" 2>/dev/null; then
      git -C "$dir" fetch origin "$ref" --tags
    fi
    git -C "$dir" checkout --detach "$ref"
  else
    git -C "$dir" fetch origin "+refs/heads/$ref:refs/remotes/origin/$ref" --tags
    git -C "$dir" checkout -B "$ref" "origin/$ref"
  fi
}

sha256_record() {
  local output="$1"; shift
  sha256sum "$@" > "$output"
}

write_sha256_sidecar() {
  local file="$1" dir base
  dir="$(dirname "$file")"
  base="$(basename "$file")"
  (cd "$dir" && sha256sum "$base" > "$base.sha256")
}

file_size() { stat -c %s "$1"; }

load_build_state() {
  [[ -f "$STATE_FILE" ]] || return 0
  local key value
  while IFS='=' read -r key value; do
    case "$key" in
      INCLUDE_UI)
        [[ -n "${INCLUDE_UI+x}" ]] || INCLUDE_UI="$value"
        ;;
      INCLUDE_CLICKSWSTATUS)
        [[ -n "${INCLUDE_CLICKSWSTATUS+x}" ]] || INCLUDE_CLICKSWSTATUS="$value"
        ;;
      DONOR_ETC_POLICY)
        [[ -n "${DONOR_ETC_POLICY+x}" ]] || DONOR_ETC_POLICY="$value"
        ;;
    esac
  done < "$STATE_FILE"
}
