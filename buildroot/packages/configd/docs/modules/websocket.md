# websocket module

`websocket.c` implements a UI-independent JSON protocol over libwebsockets.

## Accepted inputs

Text frames containing `get_status`, `get_config`, or `config` requests. Messages may arrive in fragments and are reassembled up to `MAX_MSG_LEN` (65,536 bytes). Binary, oversized, malformed, missing-type, and unknown requests receive `Bad Request`.

## Timers

- Status refresh: configurable, default 3 seconds.
- Configuration file check: 10 seconds.
- Network poll: scheduled by the network state machine.

## Important behavior

A client-specific direct response is sent before queued broadcasts. Configuration requests receive an explicit `ack`; clients must not use the later configuration broadcast as the acknowledgement. External edits to `/etc/switch.json` are validated before application. Invalid external files are ignored and reported in status.
