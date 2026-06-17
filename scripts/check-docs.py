#!/usr/bin/env python3
"""Validate local Markdown links and active README hygiene."""
from __future__ import annotations
import argparse
import pathlib
import re
import sys
from urllib.parse import unquote

LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "ftp://", "data:")
HISTORY_PARTS = {"history", "research"}
STALE = re.compile(
    r"\b(no longer needed|this is no longer|old path|old build path|obsolete workflow|"
    r"previously required|before firmware [0-9])\b",
    re.IGNORECASE,
)

def check(root: pathlib.Path) -> list[str]:
    errors: list[str] = []
    ignored = {"node_modules", "build", ".work", "artifacts", ".git"}
    for doc in sorted(root.rglob("*.md")):
        if set(doc.relative_to(root).parts) & ignored:
            continue
        text = doc.read_text(encoding="utf-8", errors="replace")
        for raw in LINK.findall(text):
            target = raw.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith("#") or target.startswith(SKIP_PREFIXES):
                continue
            target = unquote(target.split("#", 1)[0].split("?", 1)[0])
            if not target:
                continue
            resolved = (doc.parent / target).resolve()
            try:
                resolved.relative_to(root.resolve())
            except ValueError:
                # Cross-repository or intentionally external filesystem reference.
                continue
            if not resolved.exists():
                errors.append(f"{doc.relative_to(root)}: broken link: {raw}")
        rel_parts = set(doc.relative_to(root).parts)
        if doc.name.lower() == "readme.md" and not (rel_parts & HISTORY_PARTS):
            for match in STALE.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{doc.relative_to(root)}:{line}: stale implementation wording: {match.group(0)}")
    return errors

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roots", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    errors: list[str] = []
    for root in args.roots:
        errors.extend(check(root))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Documentation links and active README wording are valid.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
