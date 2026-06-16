# main module

`main.c` parses process options, initializes detected hardware and the network manager, implements local CLI commands, starts libwebsockets, and handles clean shutdown.

## Inputs

- `--config PATH`: configuration path; default `/etc/switch.json`.
- `--dry-run`: validate and log operations without hardware writes.
- `--status-interval 1..3600`: broadcast period.
- `--websocket-port 1..65535`: listen port.
- `--network-bootstrap`: apply only management IPv4 and exit.
- `--network-wait 0..300`: DHCP wait used by bootstrap mode; default 60 seconds.
- `--get-config`, `--get-status`, `--validate`, `--apply-file`, `--apply-json`.

Only one command mode should be used per invocation. Malformed numeric values or trailing arguments exit with status 2.

## Startup order

1. Detect hardware and PoE controllers.
2. Read the Meraki base MAC.
3. Load, restore, or create persistent configuration.
4. Initialize management networking.
5. In service mode, replay all global and per-port desired state.
6. Start WebSocket timers and serve requests.

The full replay is required for write-only Click settings such as storm control.
