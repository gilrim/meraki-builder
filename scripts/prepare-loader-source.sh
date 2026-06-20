#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

bootstrap_loader_archive() {
  local archive="$1" temp top version
  [[ -f "$archive" ]] || die "meraki-redboot source archive not found: $archive"
  need python3
  temp="$(mktemp -d)"
  python3 - "$archive" "$temp" <<'PY'
from pathlib import Path
import sys, zipfile
archive, output = map(Path, sys.argv[1:])
with zipfile.ZipFile(archive) as zf:
    zf.extractall(output)
PY
  top="$(find "$temp" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  [[ -n "$top" && -f "$top/Makefile" && -f "$top/VERSION" ]] || {
    rm -rf "$temp"; die "source archive does not contain a meraki-redboot project"
  }
  rm -rf "$LOADER_SOURCE_DIR"
  mkdir -p "$(dirname "$LOADER_SOURCE_DIR")"
  mv "$top" "$LOADER_SOURCE_DIR"
  rm -rf "$temp"

  # GitHub source archives do not preserve executable mode bits.  The
  # meraki-redboot Makefile intentionally invokes its host-side helpers
  # directly, so restore the source repository's executable contract before
  # importing the archive into the local fallback Git repository.
  chmod 0755 "$LOADER_SOURCE_DIR/build.sh" 2>/dev/null || true
  while IFS= read -r -d '' helper; do
    chmod 0755 "$helper"
  done < <(find "$LOADER_SOURCE_DIR/scripts" "$LOADER_SOURCE_DIR/tools" \
    -type f \( -name '*.sh' -o -name '*.py' \) -print0 2>/dev/null)
  chmod 0755 "$LOADER_SOURCE_DIR/payloads/uart-firmware-recovery/write_descriptor.py" 2>/dev/null || true

  version="$(tr -d '[:space:]' < "$LOADER_SOURCE_DIR/VERSION")"
  git -C "$LOADER_SOURCE_DIR" init -q
  git -C "$LOADER_SOURCE_DIR" config user.name postmerkOS-builder
  git -C "$LOADER_SOURCE_DIR" config user.email builder@localhost
  git -C "$LOADER_SOURCE_DIR" add -A
  git -C "$LOADER_SOURCE_DIR" commit -q -m "Imported meraki-redboot $version source archive"
  git -C "$LOADER_SOURCE_DIR" tag "$version"
  git -C "$LOADER_SOURCE_DIR" remote add origin "$LOADER_REPO_URL"
  log "Bootstrapped cached meraki-redboot $version source archive"
}

if [[ ! -d "$LOADER_SOURCE_DIR/.git" && -n "$LOADER_SOURCE_ARCHIVE" ]]; then
  bootstrap_loader_archive "$LOADER_SOURCE_ARCHIVE"
fi

clone_or_update_git_ref "$LOADER_REPO_URL" "$LOADER_SOURCE_DIR" "$LOADER_REF" "meraki-redboot"

entry_fix_patch="$REPO_ROOT/patches/meraki-redboot/0001-recovery-flat-binary-byte-zero-entry.patch"
header_grace_patch="$REPO_ROOT/patches/meraki-redboot/0002-recovery-package-header-grace.patch"
entry_descriptor="$LOADER_SOURCE_DIR/payloads/uart-firmware-recovery/write_descriptor.py"
recovery_source="$LOADER_SOURCE_DIR/payloads/uart-firmware-recovery/recovery.c"
if ! grep -q 'flat-binary-byte-zero-v1' "$entry_descriptor" 2>/dev/null; then
  [[ -f "$entry_fix_patch" ]] || die "meraki-redboot recovery entry patch is missing: $entry_fix_patch"
  log "Applying meraki-redboot flat-binary recovery entry correction"
  if ! git -C "$LOADER_SOURCE_DIR" apply --check "$entry_fix_patch"; then
    die "meraki-redboot source does not contain the expected v0.7.0 recovery layout and lacks the corrected entry contract"
  fi
  git -C "$LOADER_SOURCE_DIR" apply "$entry_fix_patch"
  git -C "$LOADER_SOURCE_DIR" config user.name postmerkOS-builder
  git -C "$LOADER_SOURCE_DIR" config user.email builder@localhost
  git -C "$LOADER_SOURCE_DIR" add -A
  GIT_AUTHOR_DATE='2026-06-20T00:00:00Z' GIT_COMMITTER_DATE='2026-06-20T00:00:00Z' \
    git -C "$LOADER_SOURCE_DIR" commit -q -m 'Fix recovery flat-binary byte-zero entry'
  RESOLVED_GIT_REF="$(git -C "$LOADER_SOURCE_DIR" rev-parse HEAD)"
  RESOLVED_GIT_DESCRIBE="$(git -C "$LOADER_SOURCE_DIR" describe --tags --always --dirty)"
fi


if ! grep -q 'PACKAGE_HEADER_TIMEOUT_MS' "$recovery_source" 2>/dev/null; then
  [[ -f "$header_grace_patch" ]] || die "meraki-redboot package-header grace patch is missing: $header_grace_patch"
  log "Applying meraki-redboot recovery package-header grace correction"
  if ! git -C "$LOADER_SOURCE_DIR" apply --check "$header_grace_patch"; then
    die "meraki-redboot source does not contain the expected recovery package-header timeout layout"
  fi
  git -C "$LOADER_SOURCE_DIR" apply "$header_grace_patch"
  git -C "$LOADER_SOURCE_DIR" config user.name postmerkOS-builder
  git -C "$LOADER_SOURCE_DIR" config user.email builder@localhost
  git -C "$LOADER_SOURCE_DIR" add -A
  GIT_AUTHOR_DATE='2026-06-20T00:01:00Z' GIT_COMMITTER_DATE='2026-06-20T00:01:00Z'     git -C "$LOADER_SOURCE_DIR" commit -q -m 'Increase recovery package-header grace period'
  RESOLVED_GIT_REF="$(git -C "$LOADER_SOURCE_DIR" rev-parse HEAD)"
  RESOLVED_GIT_DESCRIBE="$(git -C "$LOADER_SOURCE_DIR" describe --tags --always --dirty)"
fi

[[ -f "$LOADER_SOURCE_DIR/Makefile" ]] || die "meraki-redboot Makefile is missing"
[[ -f "$LOADER_SOURCE_DIR/VERSION" ]] || die "meraki-redboot VERSION is missing"
[[ -f "$LOADER_PAYLOAD_PACKER" ]] || die "meraki-redboot payload packer is missing"

printf '%s\n' "$RESOLVED_GIT_REF" > "$LOADER_SOURCE_REVISION_FILE"
cat "$LOADER_SOURCE_DIR/VERSION" > "$LOADER_SOURCE_VERSION_FILE"
if grep -q 'flat-binary-byte-zero-v1' "$entry_descriptor"; then
  log "meraki-redboot recovery entry contract: flat-binary-byte-zero-v1"
else
  die "selected meraki-redboot source lacks the corrected recovery entry contract"
fi
if grep -q 'PACKAGE_HEADER_TIMEOUT_MS 30000u' "$recovery_source"; then
  log "meraki-redboot recovery package-header grace: 30000 ms"
else
  die "selected meraki-redboot source lacks the recovery package-header grace correction"
fi
log "meraki-redboot $(cat "$LOADER_SOURCE_VERSION_FILE") selected at $RESOLVED_GIT_REF"
