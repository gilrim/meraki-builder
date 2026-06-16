# configd

`configd` is the persistent configuration and status service for postmerkOS switches. It owns the desired switch configuration in `/etc/switch.json`, replays that state into the Meraki Click graph and PD690xx PoE controllers, monitors the management IPv4 lease, and exposes the same JSON API to the web interface, CLI clients, and scripts.

## Design rules

- `/etc/switch.json` is **desired state** and is stored on the JFFS2-backed `/etc` overlay.
- Status messages are **observed state** and may be partial when hardware or a Click handler is unavailable.
- Missing handlers and device read failures produce warnings; they do not terminate the daemon.
- Invalid requests never modify the saved configuration and receive `Bad Request`.
- Write-only settings, notably storm control, are read from persistent desired state and replayed at daemon startup.
- PoE is exposed only for ports supported by the detected hardware. The strict schema is `{ "enabled": boolean, "mode": "af" | "at" }`.

## Service usage

```sh
configd --config /etc/switch.json --websocket-port 4001 --status-interval 3
```

The Buildroot init script starts the service after the Click graph has been mounted and configured.

## CLI usage

```sh
configd --get-config
configd --get-status
configd --validate /tmp/complete-switch.json
configd --apply-file /tmp/change.json
configd --apply-json '{"ports":{"1":{"storm_control":false}}}'
configd --network-bootstrap --network-wait 60
configd --dry-run --apply-file /tmp/change.json
```

Bootstrap mode waits for DHCP for the requested bounded interval, applies the
lease or configured fallback address, prints the selected management settings,
and exits without probing PoE hardware or creating a full configuration file.

CLI responses use the same envelope as WebSocket responses:

```json
{"type":"ack","data":{"message":"Configuration accepted","applied":1,"warnings":[]}}
```

Invalid input exits non-zero and prints:

```json
{"type":"error","data":{"status":400,"message":"Bad Request","detail":"..."}}
```

## WebSocket protocol

The service listens on TCP port 4001 by default. Requests and responses are UTF-8 JSON text frames.

```json
{"id":"1","type":"get_status"}
{"id":"2","type":"get_config"}
{"id":"3","type":"config","data":{"network":{"ipv4":{"mode":"dhcp","fallback_address":"169.254.0.10/16","mtu":1500}}}}
```

The `id` is optional, but clients should supply one to correlate acknowledgements. See [WebSocket and CLI protocol](docs/PROTOCOL.md).

## Configuration outline

```json
{
  "network": {"ipv4":{"mode":"dhcp","fallback_address":"169.254.0.10/16","mtu":1500}},
  "ports": {
    "1": {
      "enabled": true,
      "name": "uplink",
      "speed": "auto",
      "flow_control": false,
      "eee": true,
      "storm_control": true,
      "vlan": {"mode":"access","pvid":1,"allowed":"","untagged_vid":1,"ingress_filter":true},
      "stp": {"enabled":true,"priority":128,"cost":0,"edge":false,"auto_edge":true},
      "poe": {"enabled":true,"mode":"at"}
    }
  },
  "stp": {"priority":32768,"hello_time":2,"forward_delay":15,"max_age":20,"hold_count":6},
  "lacp": {"enabled":false},
  "multicast": {"igmp_snooping":true,"igmp_querier_interval":125,"mld_snooping":true,"mld_querier_interval":125}
}
```

The `poe` object is present only on PoE-capable copper ports.

## Source modules

Each module is documented under [`docs/modules`](docs/modules/README.md). The Click integration is documented in the repository-level [`docs/CLICK-GRAPH.md`](../../../docs/CLICK-GRAPH.md).

## Building

```sh
make -C buildroot/packages/configd
```

The Buildroot package links against JSON-C, libwebsockets, `libpostmerkos`, and `libpd690xx`.

## Authentication and size constraints

The WebSocket service authenticates root and members of `postmerkos-admin` directly against `/etc/shadow` with `crypt()`. Linux-PAM is deliberately not used because its locale, wchar, Flex, and module dependencies consume too much of the 8 MiB SquashFS region. Password updates use the existing BusyBox `chpasswd` applet.
