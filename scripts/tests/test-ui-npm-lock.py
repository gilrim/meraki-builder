#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "scripts" / "sanitize-npm-lock.py"


def run(lock: dict, expect: int = 0) -> tuple[subprocess.CompletedProcess[str], dict]:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "package-lock.json"
        path.write_text(json.dumps(lock), encoding="utf-8")
        result = subprocess.run(
            [str(TOOL), str(path), "--registry", "https://registry.npmjs.org/"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        assert result.returncode == expect, (result.stdout, result.stderr)
        return result, json.loads(path.read_text(encoding="utf-8"))


public = {
    "lockfileVersion": 3,
    "packages": {"node_modules/x": {"resolved": "https://registry.npmjs.org/x/-/x-1.0.0.tgz"}},
}
result, materialized = run(public)
assert materialized == public
assert "rewritten: 0" in result.stdout

leaked = {
    "lockfileVersion": 3,
    "packages": {
        "node_modules/x": {
            "resolved": "https://packages.applied-caas-gateway1.internal.api.openai.org/artifactory/api/npm/npm-public/x/-/x-1.0.0.tgz"
        }
    },
}
result, materialized = run(leaked)
assert materialized["packages"]["node_modules/x"]["resolved"] == "https://registry.npmjs.org/x/-/x-1.0.0.tgz"
assert "rewritten: 1" in result.stdout

unsafe = {
    "lockfileVersion": 3,
    "packages": {"node_modules/x": {"resolved": "http://127.0.0.1:8080/x.tgz"}},
}
result, _ = run(unsafe, expect=3)
assert "private/local registry" in result.stderr

print("PASS: UI npm lockfile registry guard")
