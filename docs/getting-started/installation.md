# Installation overview

Installing postmerkOS on Vitesse-based switches normally requires direct access to the 16 MiB SPI NOR flash. Review the model page and recovery path before opening the switch.

## Required equipment

- Compatible or untested Meraki switch from the MS22, MS42, MS220, or MS320 family
- 3.3 V SPI programmer such as a CH341A, Raspberry Pi SPI interface, or Bus Pirate
- SOIC16 clip or soldered access wires/header appropriate to the model
- 3.3 V USB-to-UART adapter for boot observation and recovery
- Phillips screwdriver and ESD-safe work area

## Critical precautions

1. Unplug the switch before disassembly.
2. Remove Ethernet cables and SFP modules before programming.
3. Do not power the switch while an external programmer is driving the SPI bus.
4. Never connect UART VCC; use only GND, TX, and RX.
5. Verify the UART adapter is 3.3 V, not 5 V.
6. Read the entire flash at least twice and compare the files before writing.

## Installation sequence

1. Identify the model and access points in [Vitesse switch hardware](../hardware/vitesse-switches.md).
2. Connect the SPI programmer and read verified backups.
3. Connect UART at 115200 8N1 for boot visibility.
4. Build or obtain the appropriate 16 MiB firmware image.
5. Write and verify the complete image.
6. Disconnect the SPI programmer from USB before powering the switch.
7. Observe first boot and JFFS2 initialization over UART.
8. Connect to the assigned DHCP address or fallback management address.

See [Hardware flashing](../installation/hardware-flashing.md) for commands and [First boot](first-boot.md) for initial credentials.
