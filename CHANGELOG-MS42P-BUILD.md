## Lean authenticated management and CLI

- Add local `/etc/shadow` authentication through the existing system `crypt()` implementation without Linux-PAM.
- Keep uClibc locale and wide-character support disabled and remove the PAM/Flex dependency chain.
- Add authenticated firmware upload, bounded browser command execution, password updates, and configuration restore.
- Add the automatic SSH/TTL postmerkOS management CLI and raw-shell escape.
- Use plain JSON configuration backup/restore; no backup crypto helper or extra backup-specific library is included.
- Add clickable/hoverable port graphics and a persistent Apply/Discard notification for unapplied browser changes.

## DHCP and web-service reliability fixes

- Wait up to 60 seconds for a DHCP lease during early network bootstrap before applying the link-local fallback.
- Support Click graphs that expose only `dhcpc_state_for_brain`, including dotted subnet masks.
- Print the selected management address, gateway, broadcast, and source on the serial console.
- Continue polling after fallback and rebind uhttpd whenever the applied management address changes.
- Verify configd TCP 4001 and uhttpd TCP 80 listeners before reporting successful startup.
- Add persistent runtime diagnostics in `/tmp/configd.log`, `/tmp/uhttpd.log`, and `/tmp/network-rebind.log`.
- Correct the MS42/MS42P access-policy initialization range from 1-9 to 1-8.

## Distrobox Buildroot hotfix

- Route the complete Arch/CachyOS build through Ubuntu 22.04 Distrobox instead of containerizing only the kernel stage.
- Protect direct `make rootfs` runs with the same Distrobox routing.
- Record the Buildroot host environment and clean output automatically when the distribution or host compiler changes.
- Document the GCC 16 versus binutils 2.38 failure and recovery procedure.

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
