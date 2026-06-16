# websocket

`websocket.c` owns the libwebsockets server, per-connection authentication state, request/response queues, configuration/status broadcasts, and binary firmware-upload transport.

## Session lifecycle

1. The server sends `auth_required` on connection.
2. `auth` verifies the supplied local account through Linux PAM and confirms that it is root or belongs to `postmerkos-admin`.
3. Only an authenticated session receives the initial configuration/status and later broadcasts.
4. `logout`, disconnect, or reconnect clears authentication and any unfinished upload.

Responses are held in a per-session FIFO queue so simultaneous status polling, terminal commands, and firmware requests cannot overwrite one another before libwebsockets schedules a writable callback.

## Management requests

The module dispatches:

- authentication, logout, user list, and password update;
- `get_config`, `get_status`, configuration delta, and full replacement;
- bounded root terminal execution;
- updater status;
- firmware upload start/status/cancel/finish.

Configuration changes continue to use `config_apply`, validation, atomic persistence, hardware capabilities, and management-address rebind logic.

## Firmware transport

Only one connection may own an upload. The declared file is limited to 16 MiB, includes a SHA-256 and overlay policy, and is written with mode `0600` under `/tmp`. Binary messages are accepted only for the owning authenticated session. On finish, the file is flushed and handed to `fw_update`; incomplete or oversized uploads are deleted.
