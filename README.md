# postmerkOS

postmerkOS is an independent, locally managed firmware environment for selected Cisco Meraki switches. It replaces cloud-dependent management with an interactive SSH/serial console, a persistent configuration service, and an optional browser interface while retaining the vendor Linux/Click switching platform required by the hardware.

## Current capabilities

- Hierarchical `pmc` management console over SSH and hardware serial
- Optional authenticated web interface
- Administrator, operator, and viewer roles
- Port, VLAN, STP, LACP, multicast, PoE, and management-network configuration
- Persistent JSON configuration stored in JFFS2
- Local upload, TFTP, HTTP/HTTPS, and SFTP firmware workflows
- Release/version validation, update history, recovery reset, and external configuration backup
- DHCP or static management addressing
- SSH, chrony/NTP, UTC offset, and compact DST-rule configuration
- Capability-driven support for MS22, MS42, MS220, and MS320 Vitesse switch families
- Separate integrated MX80 build path and retained untested MX84 board assets

## Compatibility

MS42P and MS320-24P are currently recorded as confirmed runtime targets. Other recognized Vitesse models are marked **untested**: they are allowed to boot and flash after an explicit warning, and successful users are invited to submit a compatibility report. Clearly incompatible architecture or flash geometry remains blocked by normal updater validation.

See [Hardware compatibility](docs/hardware/compatibility.md) for the complete model table.

## Getting started

1. Read the [safety and installation overview](docs/getting-started/installation.md).
2. Make at least two verified backups of the original SPI flash.
3. Build an image with `make base` or `make web`, or use a validated release image.
4. Flash the complete image using the [hardware flashing guide](docs/installation/hardware-flashing.md).
5. Connect using serial, SSH, or the optional web interface and follow the [first-boot guide](docs/getting-started/first-boot.md).

Build help is available with:

```sh
make help
```

## Safety

- Disconnect switch power before attaching or using an SPI programmer.
- Disconnect Ethernet cables and remove SFP modules during hardware flashing.
- UART is **3.3 V only**. Never connect the UART VCC pin.
- Cross-connect TX and RX between the switch and adapter.
- Never write flash until repeated backups have matching checksums.
- Hardware modification voids the manufacturer warranty and may permanently damage the device.

## Documentation

The [documentation index](docs/README.md) links installation, user, build, architecture, development, recovery, research, and project-history material.

This project is provided without warranty. Keep a direct hardware recovery method available while testing unconfirmed models or firmware-update changes.
