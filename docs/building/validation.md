# Validation targets and release gates

Validation is part of the build contract. Run the narrow target while developing and the aggregate gates before publishing an image.

## Core targets

```sh
make test-docs
make test-configd
make test-ui-contract UI_DIR=../postmerkos-ui
make test-ui-build UI_DIR=../postmerkos-ui
make test-hardware
make test-modules
make test-fwupdate
make test-image
make test-loader-contract
make test-all
```

## What the gates cover

- **Documentation:** local links, root README hygiene, and current-state/history separation.
- **Configd:** host compilation, schema validation, roles/sessions, SSH-key transactions, system inventory, time operations, telemetry, sockets, services, and management-plane contracts.
- **UI contract/build:** request-name parity, lint, unit tests, and Vite production output.
- **Hardware policy:** immutable identity, PoE policy, reset-button arming/cancellation/locking, LED ownership, and fail-closed model evidence.
- **Updater/UART:** transport framing, manifests, exact-model policy, MTD geometry, asynchronous validation, recovery selection, RAM flasher, rollback, and status indication.
- **Image:** partition boundaries, SPIM header/alignment/CRC, full-capacity SquashFS, release metadata, source provenance, and artifact manifests.
- **Loader contract:** required meraki-redboot menu, recovery descriptors, source identity, and pre-kernel protocol features.

## Release procedure

1. Ensure the authoritative upstream repositories are clean and at the intended refs.
2. Run `make doctor` and the relevant narrow tests.
3. Run `make test-all`, `make test-ui-build`, and `make validate`.
4. Build the intended `base` or `web` image.
5. Verify generated SHA-256 and JSON sidecars and retain the source-revision records.
6. Perform supervised hardware validation for any model or hardware path not already validated by the exact release artifact.

A passing host suite does not replace a physical flash/boot/forwarding test for newly promoted hardware behavior.
