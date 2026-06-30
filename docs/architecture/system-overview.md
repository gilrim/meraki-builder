# System overview

PostmerkOS combines the vendor Linux 3.18 platform and binary switch modules with a Buildroot userspace and local management services.

## Boot sequence

1. RedBoot/loader starts the compressed kernel.
2. Buildroot mounts read-only SquashFS.
3. JFFS2 persistent state is mounted, its overlayfs upper/work layout is verified or repaired, and defaults are initialized.
4. Hardware/model detection selects port, PoE, ASIC, LED, and compatibility capabilities.
5. Binary kernel modules and Click forwarding graphs load.
6. PoE, network, SSH, chrony, and configured optional services start.
7. Configd starts its privileged Unix-socket core; the optional WebSocket frontend starts only in web builds.
8. Serial getty and SSH provide role-aware console access.

Configd is the single configuration and authorization authority. The console, browser, scripts, and updater use the same schema and validation paths.

## Persistent-overlay recovery boundary

Before account or management-service initialization, `S01postmerkos-overlay` re-executes from `/run`, verifies writable `/etc` and `/root` overlays, and can safely detach partial mounts left by the early `mount -a` pass. If persistent JFFS2 is unavailable, it mounts a temporary RAM-backed recovery overlay and publishes a runtime warning. This keeps SSH and browser management available for diagnosis, but changes made in recovery mode are intentionally nonpersistent.
