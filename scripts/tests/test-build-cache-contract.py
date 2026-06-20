#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
build_all = (root / "scripts/build-all.sh").read_text()
build_rootfs = (root / "scripts/build-rootfs.sh").read_text()
validate = (root / "scripts/validate-image.sh").read_text()
init = (root / "buildroot/board/meraki/ms220/overlay/etc/init.d/S15configd").read_text()
marker = root / "buildroot/features/web-overlay/etc/postmerkos/features/web-ui"

assert build_all.count('CLEAN_BUILDROOT="${CLEAN_BUILDROOT:-0}"') >= 2
for text in ('.ms42p-built-ui-mode', '.ms42p-configd-build-fingerprint', 'make configd-dirclean'):
    assert text in build_rootfs, text
for text in ('websocket: enabled', 'libwebsockets', 'WebSocket-disabled configd'):
    assert text in validate, text
assert 'websocket_required' in init and 'web image contains WebSocket-disabled configd' in init
assert marker.is_file()
print('Buildroot WebSocket/cache contract tests passed')
