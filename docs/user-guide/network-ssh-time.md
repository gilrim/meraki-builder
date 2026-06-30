# Network, SSH, and time

## System identity and local discovery

Hostname and management-only mDNS behavior are documented in [System identity and local discovery](system-identity-and-discovery.md). The default name is `postmerkos.local`; ordinary DHCP/local-DNS registration is not currently a verified capability.

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

System time remains UTC and chrony supplies network synchronization. Local display uses a fixed offset or compact recurring DST rules rather than installing full tzdata on the constrained image. The web interface provides a grouped list of common timezone identifiers; selecting one fills the current standard offset and recurring daylight-saving rules, which remain available for site-specific adjustment in the advanced controls.

Administrators may also set the clock manually with the exact local format `HH:MM:SS - DD:MM:YYYY`. Configd validates the full calendar value, converts it through the configured standard/DST policy, and rejects nonexistent spring-forward wall times. Manual changes require the `services.manage` capability. Status includes UTC/local time, active offset, selected timezone identifier, NTP source, synchronization state, stratum, and polling information.
