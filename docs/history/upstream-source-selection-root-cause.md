> **Historical record:** This document describes a completed incident, migration, or release snapshot. It is not the current operating guide. Follow the active documentation linked from [`docs/README.md`](../README.md).

# Root cause

`LOADER_REF` defaulted to `latest`, but the builder defined `latest` as the
highest semantic version tag. It therefore selected tag `0.7.0` even after
`origin/main` advanced, detached from the newer commits, and tried to recreate
those commits using builder-owned patch files.

That model had three problems:

1. The selected source was intentionally stale.
2. Patches duplicated changes already maintained in meraki-redboot.
3. The PMOSREC v3 patch depended on an intermediate source layout and failed
   when replayed against a different tagged tree.

The same ownership boundary applies to postmerkos-ui: source changes belong in
that repository, not in meraki-builder.
