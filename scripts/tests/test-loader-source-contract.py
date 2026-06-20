#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
prepare = (ROOT / "scripts/prepare-loader-source.sh").read_text(encoding="utf-8")
patch = ROOT / "patches/meraki-redboot/0007-pmosrec-v3-stage-validator.patch"
assert patch.is_file(), "PMOSREC v3 stage-validator patch is missing"
patch_text = patch.read_text(encoding="utf-8")
for marker in (
    "PMOSRECOVERY3;SOC=luton26",
    "PMOSRECOVERY3;SOC=jaguar1",
    "PMOSRECOVERY3;SOC=luton26;STRUCTURAL",
    "PMOSRECOVERY3;SOC=jaguar1;STRUCTURAL",
):
    assert marker in patch_text, f"stage-validator patch lacks {marker}"
    assert marker in prepare, f"loader source preparation does not verify {marker}"
assert "0007-pmosrec-v3-stage-validator.patch" in prepare
assert "stale fixed-RAM PMOSREC recovery markers" in prepare
print("meraki-redboot PMOSREC v3 stage-validator patch contract passed")
