# System overview

PostmerkOS combines the vendor Linux 3.18 platform and binary switch modules with a Buildroot userspace and local management services.

## Boot sequence

1. RedBoot/loader starts the compressed kernel.
2. Buildroot mounts read-only SquashFS.
3. JFFS2 persistent state is mounted and defaults are initialized.
4. Hardware/model detection selects port, PoE, ASIC, LED, and compatibility capabilities.
5. Binary kernel modules and Click forwarding graphs load.
6. PoE, network, SSH, chrony, and configured optional services start.
7. Configd starts its privileged Unix-socket core; the optional WebSocket frontend starts only in web builds.
8. Serial getty and SSH provide role-aware console access.

Configd is the single configuration and authorization authority. The console, browser, scripts, and updater use the same schema and validation paths.
