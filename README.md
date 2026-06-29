# postmerkOS

postmerkOS is an independent, locally managed firmware environment for selected Cisco Meraki Vitesse switches. This repository builds the firmware image, recovery components, management services, console, and optional browser interface.

## Before you begin

Firmware replacement can permanently damage the switch. Keep a verified external SPI-flash backup and a known-good recovery method available. UART is **3.3 V only**; never connect the UART VCC pin. Compatibility is exact-model and release specific, so review the current [hardware compatibility matrix](docs/hardware/compatibility.md) before building or flashing.

## Quick start

```sh
make doctor
make base       # console-managed image
make web        # image with the optional browser interface
```

Build output is written under `artifacts/`. Start with the [installation overview](docs/getting-started/installation.md), then follow the [hardware flashing guide](docs/installation/hardware-flashing.md) and [first-boot guide](docs/getting-started/first-boot.md).

Useful entry points:

- `make help` — list supported build and validation targets
- [Recovery procedures](docs/installation/recovery.md)
- [Build instructions](docs/building/switches.md)
- [Artifact and manifest reference](docs/building/artifacts.md)

## Documentation

The [documentation database](docs/README.md) is the authoritative map for current user, build, architecture, hardware, recovery, research, and project-history information. Repository documentation must follow [DOCUMENTATION-RULES.md](DOCUMENTATION-RULES.md).

This project is provided without warranty.
