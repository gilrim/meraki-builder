# configd module index

| Module | Responsibility |
|---|---|
| [main](main.md) | Process lifecycle and CLI commands |
| [websocket](websocket.md) | Authenticated JSON/binary WebSocket transport, timers, broadcasts |
| [auth](auth.md) | PAM authentication, administrator authorization, password updates |
| [system_ops](system_ops.md) | Authenticated terminal and firmware-updater handoff |
| [config_apply](config_apply.md) | Merge, validate, save, and apply orchestration |
| [config_file](config_file.md) | Atomic persistent storage and backup recovery |
| [validation](validation.md) | Strict complete-configuration schema validation |
| [network](network.md) | DHCP/static management IPv4 state machine |
| [hardware](hardware.md) | Model, port, and PoE capability detection |
| [status](status.md) | Best-effort observed-status aggregation |
| [click_port](click_port.md) | Per-port Click and PoE desired-state translation |
| [click_global](click_global.md) | Global STP, LACP, and multicast translation |
| [json_util](json_util.md) | Deep-copy and deep-merge helpers |
| [result](result.md) | Nonfatal apply counts and warning collection |
