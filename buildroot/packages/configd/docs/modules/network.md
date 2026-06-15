# network module

`network.c` owns management IPv4 desired state and DHCP lease monitoring. It is independent of the web UI and is used by service, WebSocket, and CLI execution paths.

Read-only status requests use `network_manager_observe()`, which parses desired state and readable DHCP data without writing any Click handlers.

## DHCP configuration

```json
{"network":{"ipv4":{"mode":"dhcp","fallback_address":"169.254.0.10/16","mtu":1500}}}
```

The manager reads `/click/uplinkstate/dhcp_state`, confirms details with `/click/uplinkstate/dhcpc_state_for_brain`, and applies changed leases through `/click/set_host_ip/run`.

Polling policy:

- no bound lease: 5 seconds;
- read/parse failure: short retry;
- bound lease: `renew_at`, clamped to 5–300 seconds;
- static mode: periodic verification interval.

An active DHCP address is retained during a transient renewal miss until expiry. After expiry, or before the first lease, the fallback address is used.

## Static configuration

```json
{"network":{"ipv4":{"mode":"static","address":"192.168.1.20/24","gateway":"192.168.1.1","mtu":1500}}}
```

The gateway must be in the configured subnet. A WebSocket-triggered address change is deferred briefly so the acknowledgement can be transmitted before the connection is disrupted.

## Click command

`set_host_ip` receives:

```text
ADDRESS PREFIX GATEWAY MTU BROADCAST 1
```

The second value is the CIDR prefix length, not a dotted netmask.

## Static-to-DHCP transition

When a running switch is changed from static mode to DHCP, the already-applied
static address remains active until Click reports a valid bound lease. This is a
make-before-break transition and prevents an accidental loss of management
access on networks without a DHCP server. A cold boot in DHCP mode still uses
the configured link-local fallback until a lease appears.

## Early boot

`configd --network-bootstrap` runs from `S10clickconfig`, before `S11poe`. It
loads only the persisted `network` object and deliberately avoids hardware/PoE
probing or creation of the full switch configuration. On a fresh flash, where no
configuration exists yet, it uses an in-memory DHCP default and does not create
`/etc/switch.json`; the normal daemon creates the complete configuration after
PoE initialization.
