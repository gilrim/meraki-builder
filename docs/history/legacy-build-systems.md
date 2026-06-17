# Legacy build systems

The active Vitesse build is driven by the root Makefile and scripts. Ubuntu 22.04 Distrobox is the supported compatibility environment for Arch/CachyOS hosts.

Repository cleanup removed the superseded MS220 Docker wrapper, Nix/devenv definitions, tracked flashing logs, and duplicate top-level NAND/NOR build paths. Useful raw-image tools were moved to `tools/research`.

The MX80 Docker workflow was replaced by `make mx80*` targets before removal. MX84 assets remain present with an `mx84-check` inventory until complete build inputs are available.
