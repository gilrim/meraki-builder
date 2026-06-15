#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
load_build_state
need readelf
need sha256sum

required=(
  "$KERNEL_ARTIFACT_DIR/vmlinuz"
  "$KERNEL_ARTIFACT_DIR/vmlinuz.bin"
  "$KERNEL_HEADERS_TARBALL"
  "$LOADER_ARTIFACT"
  "$DONOR_ROOT/lib/modules/elts_meraki.ko"
  "$DONOR_ROOT/lib/modules/merakiclick.ko"
  "$DONOR_ROOT/lib/modules/proclikefs.ko"
  "$DONOR_ROOT/lib/modules/jaguar_dual/vc_click.ko"
  "$DONOR_ROOT/lib/modules/jaguar_dual/vtss_core.ko"
)
if bool_enabled "${INCLUDE_UI:-0}"; then
  required+=("$BUILD_DIR/postmerkos-ui/index.html")
fi

for input in "${required[@]}"; do
  [[ -f "$input" ]] || die "Required build input is missing: $input"
done

[[ "$(file_size "$LOADER_ARTIFACT")" -eq $((0x40000)) ]] || \
  die "RedBoot loader must be exactly 256 KiB"
entry="$(readelf -h "$KERNEL_ARTIFACT_DIR/vmlinuz" | awk '/Entry point address/ {print $4}')"
[[ "$entry" == 0x81000000 ]] || die "Unexpected compressed-kernel entry point: $entry"
(( $(file_size "$KERNEL_ARTIFACT_DIR/vmlinuz.bin") + 32 <= 0x2c0000 )) || \
  die "Compressed kernel and SPIM header exceed the kernel region"

manifest="$ARTIFACTS_DIR/build-inputs.sha256"
: > "$manifest"
record_tree() {
  local root="$1"
  while IFS= read -r -d '' file; do
    hash="$(sha256sum "$file" | awk '{print $1}')"
    printf '%s  %s\n' "$hash" "${file#$REPO_ROOT/}" >> "$manifest"
  done < <(find "$root" -type f -print0 | sort -z)
}

for input in "$KERNEL_ARTIFACT_DIR/vmlinuz" "$KERNEL_ARTIFACT_DIR/vmlinuz.bin" \
  "$KERNEL_HEADERS_TARBALL" "$LOADER_ARTIFACT"; do
  hash="$(sha256sum "$input" | awk '{print $1}')"
  printf '%s  %s\n' "$hash" "${input#$REPO_ROOT/}" >> "$manifest"
done
record_tree "$DONOR_ROOT/lib/modules"
if bool_enabled "${INCLUDE_UI:-0}"; then
  record_tree "$BUILD_DIR/postmerkos-ui"
fi

# Hash the actual checked-out build definitions. The local repository contents,
# including uncommitted edits, are the source of truth for this build.
for source_tree in \
  "$REPO_ROOT/scripts" \
  "$REPO_ROOT/buildroot/board/meraki/ms220" \
  "$REPO_ROOT/buildroot/packages" \
  "$REPO_ROOT/buildroot/patches"; do
  [[ -d "$source_tree" ]] && record_tree "$source_tree"
done
if bool_enabled "${INCLUDE_UI:-0}" && [[ -d "$REPO_ROOT/buildroot/features" ]]; then
  record_tree "$REPO_ROOT/buildroot/features"
fi
for source_file in "$REPO_ROOT/Makefile"; do
  hash="$(sha256sum "$source_file" | awk '{print $1}')"
  printf '%s  %s\n' "$hash" "${source_file#$REPO_ROOT/}" >> "$manifest"
done

sort -u -o "$manifest" "$manifest"
log "Verified local build inputs; manifest written to $manifest"
