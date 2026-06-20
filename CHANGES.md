# Affected repository

## meraki-builder

- Default `LOADER_REF` changed from tag-oriented `latest` to `main`.
- Default `UI_REF` changed from `ms42p-dev` to `main`.
- `latest` now aliases `main`.
- Removed automatic v0.7.0 archive fallback.
- Removed all `patches/meraki-redboot/*` files.
- Replaced loader patching with read-only contract validation.
- Added clean-tree and unchanged-revision enforcement.
- Added UI tracked-source immutability verification.
- Added normalized and requested refs to loader provenance.
- Updated current documentation and regression tests.

Local validation commit: `1a6a2b4abb8e50abd02e35af96c2a5ca5803536a`

## meraki-redboot

No changes. The supplied latest source already contains the required PMOSREC v3
and stage-validator implementation.

## postmerkos-ui

No changes. The builder now consumes its latest `main` branch without patching.
