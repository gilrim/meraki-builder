#!/usr/bin/env python3
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[2]
ui = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root.parent / "postmerkos-ui").resolve()
manifest_path = root / "buildroot/board/meraki/ms220/overlay/usr/share/postmerkos/management-features.json"
console_path = root / "buildroot/packages/postmerkos-console/files/postmerkos-console"
errors = []
try:
    manifest = json.loads(manifest_path.read_text())
except Exception as exc:
    raise SystemExit(f"cannot read feature manifest: {exc}")
if manifest.get("schema") != 1 or not isinstance(manifest.get("features"), dict):
    errors.append("feature manifest must use schema 1 with an object named features")
ui_text = "\n".join(p.read_text(errors="replace") for p in sorted((ui / "src").glob("*")) if p.is_file()) if (ui / "src").is_dir() else ""
pmc_text = console_path.read_text(errors="replace")
for name, entry in sorted(manifest.get("features", {}).items()):
    kind = entry.get("class")
    if kind not in {"switch_config", "client_local"}:
        errors.append(f"{name}: unknown class {kind!r}")
        continue
    if entry.get("web") is not True:
        errors.append(f"{name}: every tracked feature must declare web coverage")
    if kind == "switch_config":
        if entry.get("pmc") is not True:
            errors.append(f"{name}: switch_config feature lacks PMC coverage")
        if not entry.get("capability"):
            errors.append(f"{name}: switch_config feature lacks a server capability")
    elif entry.get("pmc") is not False:
        errors.append(f"{name}: client_local feature must explicitly exclude PMC")
    for marker in entry.get("web_markers", []):
        if marker not in ui_text:
            errors.append(f"{name}: web marker not found: {marker}")
    for marker in entry.get("pmc_markers", []):
        if marker not in pmc_text:
            errors.append(f"{name}: PMC marker not found: {marker}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"Management feature parity passed ({len(manifest.get('features', {}))} features)")
