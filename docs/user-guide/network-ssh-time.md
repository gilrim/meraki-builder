# Network, SSH, and time

## Management IPv4

The management interface supports DHCP with fallback addressing or static IPv4. Configd records DHCP acquisition, duration, renewal, rebind, server, address, gateway, and calculated expiry in RAM.

## SSH

Fresh installations enable Dropbear password authentication. Root’s initial password is the device serial number and a warning remains until changed.

Administrators can configure:

- SSH enabled and autostart
- listening port
- password authentication or key-only mode
- authorized public keys and fingerprints
- host-key regeneration

Key-only mode requires at least one authorized key. No host firewall is added; upstream network policy is expected to protect the management network.

## Time

System time remains UTC and chrony supplies network synchronization. Local display uses a fixed offset or compact recurring DST rules rather than full tzdata. North American and European presets plus custom rules are available. Status includes UTC/local time, active offset, NTP source, synchronization state, stratum, and polling information.
