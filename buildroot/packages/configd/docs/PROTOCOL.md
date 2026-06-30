# configd WebSocket and local protocol

## Transport

Web builds listen on TCP port 4001 and require the WebSocket subprotocol:

```text
configd-ws
```

The local console uses `/run/postmerkos/configd.sock`. Both transports resolve one role/capability session and dispatch to the same validation and operation handlers.

Local requests and replies are one JSON object followed by a newline. Both peers
read until the complete delimiter, enforce bounded message sizes and a five-second
I/O timeout, and suppress SIGPIPE so a disconnected peer cannot terminate either
configd or `postmerkosctl`.

Run `configd --features` to confirm which transports were compiled into an image.

## Envelope

Text messages are JSON objects with a string `type`, optional `id`, and optional `data`.

```json
{"id":"client-generated-id","type":"hello"}
```

Text requests are limited to 65,536 bytes and fragmented frames are reassembled before parsing. Binary WebSocket frames are accepted only during an authenticated, token-owned firmware upload.

## Pre-authentication allowlist

Only these request types are accepted before login:

- `hello`
- `ping`
- `auth`
- `logout`

A newly connected client receives `auth_required` and should validate the endpoint with `hello` before enabling the login action.

```json
{"id":"1","type":"hello"}
```

```json
{
  "id":"1",
  "type":"hello",
  "data":{
    "service":"configd",
    "protocol":2,
    "websocket_protocol":"configd-ws",
    "authentication_required":true,
    "firmware":"2026-06-18-01-30"
  }
}
```

Authentication uses a local Linux account with a postmerkOS role:

```json
{"id":"2","type":"auth","data":{"username":"root","password":"..."}}
```

A successful response includes username, UID, role, and capabilities. The complete user list is returned only to sessions with `users.manage`.

## Authenticated requests

### `get_status`

```json
{"id":"3","type":"get_status"}
```

Returns current best-effort device state. Missing sensors or Click handlers appear in `data.errors`; the rest of the status remains available.

### `reset_button_status`

```json
{"id":"3b","type":"reset_button_status"}
```

Returns the lightweight live physical-reset object and current LED owner. The browser may poll this method at 500 ms without rebuilding the complete system inventory. Fields include debounced `pressed`, `armed`, `countdown_active`, `progress`, `led_indication_active`, and `last_event`.

### `get_config`

```json
{"id":"4","type":"get_config"}
```

Returns the complete persistent desired configuration.

### `config`

`data` is a partial configuration delta. It is deep-merged into the saved configuration, validated, atomically saved, and then applied.

```json
{"id":"5","type":"config","data":{"ports":{"4":{"poe":{"enabled":true,"mode":"af"}}}}}
```

A successful response is an `ack`. Configd then broadcasts the complete configuration and updated status to authenticated clients.

Every operation also checks the session capability. Hiding a control in the console or browser is not the security boundary.

### `time_set_clock`

Sets the system clock through the same validated time operation used by the local management socket. The request requires `services.manage` and accepts exactly one of an epoch value or a local wall-clock string:

```json
{"id":"6","type":"time_set_clock","data":{"local":"14:05:30 - 29:06:2026"}}
```

```json
{"id":"6","type":"time_set_clock","data":{"epoch":1782741930}}
```

Local values use `HH:MM:SS - DD:MM:YYYY`, are interpreted through the saved standard/DST policy, and reject invalid calendar values and nonexistent spring-forward times. A successful request returns an `ack` and broadcasts refreshed status.


## Operation catalogue

| Request | Purpose | Required capability |
|---|---|---|
| `get_status` | Read live status and system inventory | `status.read` |
| `reset_button_status` | Read lightweight debounced reset-button/LED state | `status.read` |
| `get_config` | Read desired switch configuration | `config.read` |
| `config` | Apply a validated partial configuration delta | capability selected from changed paths |
| `replace_config` | Validate/apply a complete restored configuration | `config.restore` |
| `ports_clone` | Dry-run or apply selected port categories to targets | `switching.write` |
| `compatibility_report` | Read exact-model compatibility/evidence | `status.read` |
| `compatibility_ack` | Record an allowed compatibility acknowledgement | administrator operation |
| `user_list`, `user_create`, `user_delete`, `user_role` | Manage Linux accounts and roles | `users.manage` |
| `password_change` | Change an allowed account password and revoke sessions | `users.manage` or self-service rules enforced by server |
| `ssh_key_list`, `ssh_key_add`, `ssh_key_remove` | Manage validated public keys transactionally | `users.manage` |
| `services_get` | Read service policy and observed state | `status.read` |
| `services_set`, `services_action` | Configure or operate managed services | `services.manage` |
| `system_identity_get`, `timezones_get` | Read identity/discovery status or canonical timezone catalogue | `status.read` |
| `system_identity_set` | Validate/apply/persist hostname and reconfigure mDNS | `network.write` |
| `time_get` | Read time/NTP/DST status and policy | `status.read` |
| `time_set`, `time_sync`, `time_set_clock` | Configure policy, request synchronization, or set the clock | `services.manage` |
| `terminal_start`, `terminal_exec` | Start/use the bounded command session | `terminal.exec` |
| `firmware_status` | Read updater state/history | `firmware.history.read` |
| `firmware_upload_start`, binary upload, `firmware_upload_finish`, `firmware_upload_status`, `firmware_upload_cancel` | Stage and asynchronously validate a browser candidate | `firmware.update` |
| `firmware_begin_flash` | Authorize installation of a ready candidate | `firmware.update` |
| `firmware_repo_get`, `firmware_repo_check` | Read/check configured repositories | `firmware.history.read` |
| `firmware_repo_set` | Change repository policy | `firmware.update` |

`hello`, `ping`, `auth`, `auth_token`, and `logout` are transport/session operations. The server remains authoritative for path-specific capability selection and may reject a request more strictly than this summary.

## Responses

### Acknowledgement

```json
{"id":"5","type":"ack","data":{"message":"Configuration accepted","applied":2,"warnings":[]}}
```

Warnings mean the desired state was valid and saved but one or more runtime operations could not be verified or applied.

### Error

```json
{"id":"5","type":"error","data":{"status":400,"message":"Bad Request","detail":"ports.49.poe is not supported by port 49"}}
```

Authorization failures use status 401 or 403. Rejected configuration is not saved.

### Broadcasts

```json
{"type":"config","data":{}}
{"type":"status","data":{}}
```

Broadcasts are sent only after authentication and normally do not contain an `id`.

## CLI equivalents

```sh
configd --features
configd --get-status
configd --get-config
configd --apply-json '{"ports":{"1":{"enabled":false}}}'
configd --apply-file ./delta.json
configd --validate ./complete.json
postmerkosctl session --shell
```


## Runtime health probe

`postmerkosctl management-health` validates the local process and Unix socket, performs a role-aware local `session` request, opens a WebSocket on TCP 4001, requires the `configd-ws` subprotocol, and sends a protocol `hello`. This is the readiness contract used by `S15configd`.

## Management feature parity

The release file `/usr/share/postmerkos/management-features.json` distinguishes switch-level configuration from browser-local presentation. Release tests require every tracked `switch_config` feature to declare a server capability and both browser and PMC markers.
