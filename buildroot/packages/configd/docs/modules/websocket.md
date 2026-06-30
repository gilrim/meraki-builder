# websocket module

`websocket.c` implements a UI-independent JSON protocol over libwebsockets.

## Accepted inputs

Text frames containing authenticated protocol requests such as `get_status`, `reset_button_status`, `get_config`, or `config`. Messages may arrive in fragments and are reassembled up to `MAX_MSG_LEN` (65,536 bytes). Binary, oversized, malformed, missing-type, and unknown requests receive `Bad Request`.

## Timers

- Status refresh: configurable, default 3 seconds.
- Configuration file check: 10 seconds.
- Network poll: scheduled by the network state machine.

## Important behavior

A client-specific direct response is sent before queued broadcasts. Configuration requests receive an explicit `ack`; clients must not use the later configuration broadcast as the acknowledgement. External edits to `/etc/switch.json` are validated before application. Invalid external files are ignored and reported in status.

## Lightweight reset-button polling

`reset_button_status` reads the atomically published button state and LED owner without collecting temperatures, clients, filesystems, or Click port tables. The System panel uses this request every 500 ms so press/release/cancellation feedback is independent of the normal three-second full-status broadcast.
