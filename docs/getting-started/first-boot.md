# First boot and initial access

On first boot postmerkOS verifies the writable JFFS2 overlay layout, initializes persistent state, detects the switch model and capabilities, starts the forwarding graph, obtains a management address, and launches management services according to policy.

## Initial credentials

- User: `root`
- Password: the switch serial number without separators

A warning remains visible while the default password is active. A password change is recommended but not forced.

## Management access

- Hardware serial: 115200 baud, 8 data bits, no parity, 1 stop bit
- SSH: `ssh root@SWITCH_ADDRESS`
- Console command: `pmc`
- Optional web build: open `http://SWITCH_ADDRESS/`

The serial getty can open the console directly or require login according to the JFFS2 security policy. Running `pmc` from a raw shell returns to the management console.

## Network behavior

DHCP is the default. Configd records lease acquisition and expiry information in RAM and reports it to both interfaces. If DHCP is unavailable, the configured fallback address is used. Static IPv4 configuration is also supported.

## Untested model notice

Recognized but unconfirmed models show a first-access notice. The diagnostic report contains model, port, PoE, management, console, and firmware information without passwords or private keys. Acknowledgement is stored per model and firmware version.

If persistent JFFS2 cannot be mounted, the boot process uses a temporary recovery overlay so authentication and SSH can still initialize. System Information reports that mode and warns that changes will not survive reboot.
