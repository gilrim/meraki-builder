#!/usr/bin/env bash
set -Eeuo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="$REPO_ROOT/buildroot/board/meraki/mx84"
echo 'MX84 build status: untested/incomplete'
echo
for file in overlay/bin/board_data overlay/etc/init.d/S03vtss overlay/usr/bin/vtss_poca_d.static post-build.sh; do
  [[ -e "$BOARD/$file" ]] && printf 'present  %s\n' "$file" || printf 'missing  %s\n' "$file"
done
echo
echo 'Required before a reproducible image build can be offered:'
echo '  - complete Buildroot configuration'
echo '  - kernel source/configuration and device tree'
echo '  - flash/container layout and image assembly script'
echo '  - hardware validation report'
