# System identity and local discovery

postmerkOS uses `postmerkos` as the default kernel hostname and advertises `postmerkos.local` on the management network when mDNS is enabled. The `.local` suffix is discovery output and is not stored as part of the hostname.

## Hostname rules

Administrators may configure one DNS label containing lowercase letters, digits, and hyphens. The value must be 1–63 characters, cannot begin or end with a hyphen, and cannot be `localhost` or `local`. Uppercase input is normalized to lowercase; periods, spaces, underscores, and control characters are rejected.

The **Use postmerkos** action restores the default. **Generate unique name** creates `m` followed by the base MAC address without punctuation.

The effective hostname is applied before Click networking and mDNS start. It is persisted in `/config/postmerkos/system.json`, written to the writable `/etc/hostname`, and exposed in System Information. Invalid persistent data fails closed to the shipped `postmerkos` default.

## Multicast DNS

mDNS is a managed service and is enabled/autostarted by default. The responder is configured without D-Bus, publishes IPv4 host-address records only, and binds to `linux_mgmt`. It does not advertise on switching/data-plane interfaces and does not initially publish SSH or web service records.

The requested local name is `[hostname].local`. The responder performs mDNS conflict handling; the current no-D-Bus integration cannot query the responder's internally suffixed conflict name, so status marks the requested advertised name as unverified and conflict state as unobserved. A normal client lookup is the authoritative deployment test.

Changing the hostname is transactional: configd validates and applies the candidate, reloads or starts mDNS when service policy requires it, and persists only after reconfiguration succeeds. Failure restores the prior hostname and responder configuration. Disabling mDNS prevents a hostname change from starting it implicitly.

## DNS registration boundary

`.local` uses multicast DNS and generally works only on the same Layer 2 management network unless a gateway provides an mDNS reflector. It is separate from ordinary DHCP-integrated or authoritative DNS.

The current proprietary Click DHCP client does not expose verified hostname/FQDN option handling. Status therefore reports DHCP hostname registration as unsupported. Static-IP dynamic DNS and authenticated RFC 2136 updates are not implemented.
