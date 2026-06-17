#!/usr/bin/env bash
set -Eeuo pipefail
. "$(dirname "$0")/common.sh"
missing=0
for tool in make tar bzip2 rsync sha1sum python3; do
  if command -v "$tool" >/dev/null 2>&1; then printf 'ok      %s\n' "$tool"; else printf 'missing %s\n' "$tool"; missing=1; fi
done
for file in buildroot-config post-image.sh overlay/etc/motd; do
  [[ -e "$MX80_BOARD_SOURCE/$file" ]] || { printf 'missing board file: %s\n' "$file"; missing=1; }
done
grep -q 'linux-fullerene-3.4.tar.bz2' "$MX80_BOARD_SOURCE/buildroot-config" || { echo 'missing Fullerene kernel source selection'; missing=1; }
printf 'Buildroot source: %s\n' "$MX80_BUILDROOT_URL"
printf 'Kernel source:    %s\n' "$(sed -n 's/^BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="\(.*\)"/\1/p' "$MX80_BOARD_SOURCE/buildroot-config")"
[[ $missing -eq 0 ]] || exit 1
