# configd WebSocket and CLI protocol

## Envelope and authentication

Text messages are JSON objects with a string `type`, optional client-generated `id`, and optional `data`. The server sends `auth_required` immediately after a WebSocket connection and does not disclose configuration or status until PAM authentication succeeds.

```json
{"id":"1","type":"auth","data":{"username":"root","password":"..."}}
{"id":"1","type":"auth","data":{"username":"root","users":[{"username":"root","uid":0}]}}
```

Only root and members of `postmerkos-admin` may authenticate. Authentication is per WebSocket connection; no cookie or bearer token is stored in the browser.

```json
{"id":"2","type":"logout"}
{"id":"3","type":"get_auth"}
{"id":"4","type":"password_change","data":{"username":"root","current_password":"...","new_password":"..."}}
```

JSON text requests are limited to 65,536 bytes. Binary frames are accepted only while the same authenticated session owns an active firmware upload.

## Configuration and status

```json
{"id":"10","type":"get_status"}
{"id":"11","type":"get_config"}
```

`config` deep-merges a partial delta into persistent desired state, validates the complete result, atomically saves it, and applies the changed fields:

```json
{"id":"12","type":"config","data":{"ports":{"4":{"poe":{"enabled":true,"mode":"af"}}}}}
```

`replace_config` validates, saves, and applies a complete configuration. It is used for backup restoration:

```json
{"id":"13","type":"replace_config","data":{"network":{},"ports":{},"stp":{},"lacp":{},"multicast":{}}}
```

Port changes are based on desired state rather than current link activity. A disconnected PoE-capable port can therefore be enabled before its powered device establishes link.

## Authenticated terminal

```json
{"id":"20","type":"terminal_exec","data":{"command":"fw_update_status"}}
{"id":"20","type":"terminal","data":{"command":"fw_update_status","output":"...","exit_code":0,"timed_out":false,"truncated":false}}
```

Commands run as root, time out after 15 seconds, and return at most 64 KiB of combined stdout/stderr.

## Firmware upload

The client computes SHA-256 and starts an upload:

```json
{"id":"30","type":"firmware_upload_start","data":{"name":"firmware.bin","size":16777216,"sha256":"64-hex-digits","overlay":"preserve","force":false}}
```

After `firmware_upload_ready`, the client sends one or more binary WebSocket messages. The total may not exceed 16 MiB. The client then sends:

```json
{"id":"31","type":"firmware_upload_finish"}
```

configd flushes the upload and hands it to `fw_update`. The updater independently verifies the supplied digest and firmware structure before writing. The web and SSH connections are expected to close once flashing begins.

Other upload/status requests:

```json
{"id":"32","type":"firmware_upload_status"}
{"id":"33","type":"firmware_upload_cancel"}
{"id":"34","type":"firmware_status"}
```

Only one WebSocket may own an upload at a time.

## Responses and broadcasts

```json
{"id":"12","type":"ack","data":{"message":"Configuration accepted","applied":2,"warnings":[]}}
{"id":"12","type":"error","data":{"status":400,"message":"Bad Request","detail":"..."}}
{"type":"config","data":{}}
{"type":"status","data":{}}
```

Warnings mean valid desired state was saved but one or more runtime operations could not be completed or verified. Authenticated sessions receive unsolicited complete configuration and status broadcasts.

## Local CLI equivalents

```sh
configd --get-status
configd --get-config
configd --apply-json '{"ports":{"1":{"enabled":false}}}'
configd --apply-file ./delta.json
configd --replace-file ./complete.json
configd --validate ./complete.json
```
