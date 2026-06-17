# Build artifacts and validation

Vitesse switch artifacts are published under `artifacts/` after image validation. MX80 publishes to `artifacts/mx80/`.

Validation covers:

- expected complete-image size and region boundaries
- release manifest and compatibility metadata
- exact SquashFS size
- checksums
- required target files and packages
- console-only and optional-web configurations

Host tests exercise configd, role enforcement, console navigation, updater parsing, shell syntax, and the UI production build. These tests do not replace a target cross-build or hardware boot/update test.
