#!/usr/bin/env python3
"""Reject unsafe npm lockfile registries and rewrite known build-environment leaks."""

from __future__ import annotations

import argparse
import ipaddress
import json
import sys
from pathlib import Path
from urllib.parse import urlparse, urlunparse

KNOWN_PROXY_PATHS = {
    "packages.applied-caas-gateway1.internal.api.openai.org": "/artifactory/api/npm/npm-public/",
    "packages.hub.ace-research.openai.org": "/artifactory/api/npm/npm-public/",
}
PUBLIC_NON_REGISTRY_HOSTS = {"github.com", "codeload.github.com"}


def private_or_local_host(host: str) -> bool:
    lowered = host.lower().rstrip(".")
    if lowered in {"localhost", "localhost.localdomain"}:
        return True
    if lowered.endswith((".localhost", ".local", ".internal")):
        return True
    try:
        address = ipaddress.ip_address(lowered.strip("[]"))
    except ValueError:
        return False
    return not address.is_global


def normalize_registry(value: str) -> tuple[str, str, str]:
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError("registry must be an absolute HTTPS URL")
    base_path = parsed.path.rstrip("/") + "/"
    return parsed.scheme, parsed.netloc.lower(), base_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("lockfile", type=Path)
    parser.add_argument("--registry", default="https://registry.npmjs.org/")
    args = parser.parse_args()

    try:
        registry_scheme, registry_host, registry_path = normalize_registry(args.registry)
    except ValueError as exc:
        parser.error(str(exc))

    try:
        document = json.loads(args.lockfile.read_text(encoding="utf-8"))
    except FileNotFoundError:
        print(f"ERROR: npm lockfile not found: {args.lockfile}", file=sys.stderr)
        return 2
    except json.JSONDecodeError as exc:
        print(f"ERROR: invalid npm lockfile {args.lockfile}: {exc}", file=sys.stderr)
        return 2

    rewritten = 0
    rejected: list[str] = []
    observed: set[str] = set()

    for package_name, metadata in document.get("packages", {}).items():
        if not isinstance(metadata, dict):
            continue
        resolved = metadata.get("resolved")
        if not isinstance(resolved, str) or not resolved.startswith(("http://", "https://")):
            continue
        parsed = urlparse(resolved)
        host = (parsed.hostname or "").lower()
        observed.add(host)

        proxy_prefix = KNOWN_PROXY_PATHS.get(host)
        if proxy_prefix is not None:
            if not parsed.path.startswith(proxy_prefix):
                rejected.append(f"{package_name or '<root>'}: unexpected proxy path {resolved}")
                continue
            suffix = parsed.path[len(proxy_prefix):]
            metadata["resolved"] = urlunparse(
                (registry_scheme, registry_host, registry_path + suffix, "", parsed.query, parsed.fragment)
            )
            rewritten += 1
            observed.add(registry_host)
            continue

        if private_or_local_host(host):
            rejected.append(f"{package_name or '<root>'}: private/local registry {resolved}")
            continue

        if host not in {registry_host, "registry.npmjs.org", *PUBLIC_NON_REGISTRY_HOSTS}:
            rejected.append(f"{package_name or '<root>'}: unapproved package host {resolved}")

    if rejected:
        print("ERROR: package-lock.json contains unsafe or non-public package URLs:", file=sys.stderr)
        for item in rejected[:20]:
            print(f"  - {item}", file=sys.stderr)
        if len(rejected) > 20:
            print(f"  - ... and {len(rejected) - 20} more", file=sys.stderr)
        return 3

    if rewritten:
        args.lockfile.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")

    hosts = ", ".join(sorted(host for host in observed if host)) or "none"
    print(f"npm lockfile registry check passed; hosts: {hosts}; rewritten: {rewritten}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
