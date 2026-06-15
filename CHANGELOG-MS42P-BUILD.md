# MS42P build workflow changes

- Added a top-level Makefile and staged Bash build interface.
- Added interactive `make all`, explicit `make base`, and explicit `make web` workflows.
- Added CachyOS/Arch and Ubuntu/Debian dependency installation.
- Added an Ubuntu 22.04 distrobox route for the legacy kernel/OpenWrt toolchain.
- Pinned and automated the known Linux 3.18/OpenWrt source checkout.
- Added donor firmware discovery, download, size validation, extraction, and module validation.
- Made donor modules the default binary-only import instead of replacing the maintained `/etc` tree.
- Added optional fallback import of donor `/etc` beneath the repository overlay.
- Added optional cloning and building of `Gadorach/postmerkos-ui:ms42p-dev`.
- Added a portable Node.js fallback.
- Normalized the `status` and `findhdr` Buildroot package layouts.
- Replaced line-number-sensitive custom package patches with generated Kconfig registration.
- Added maintained configd/uhttpd init scripts as an optional web feature overlay.
- Integrated the released Click storm-control compatibility behavior directly into configd.
- Reworked the MS220 post-build script for quoting and deterministic source-controlled transformations.
- Replaced the incomplete mixed U-Boot/RedBoot post-image script with deterministic MS42P NOR assembly.
- Added image and rootfs validation, SHA-256 manifests, revision records, and effective-file manifests.
