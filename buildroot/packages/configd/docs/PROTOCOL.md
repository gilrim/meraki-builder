# configd WebSocket and CLI protocol

## Envelope

All messages are JSON objects with a string `type`, optional `id`, and optional `data`.

```json
{"id":"client-generated-id","type":"get_status"}
```

Maximum request size is 65,536 bytes. Binary frames are rejected. Fragmented WebSocket text messages are reassembled before parsing.

## Requests

### `get_status`

```json
{"id":"1","type":"get_status"}
```

Returns current best-effort device state. A missing sensor or Click handler is represented in `data.errors`; the rest of the status remains available.

### `get_config`

```json
{"id":"2","type":"get_config"}
```

Returns the complete persistent desired configuration.

### `config`

`data` is a partial configuration delta. It is deep-merged into the saved complete configuration, the complete result is validated, atomically saved, and then applied.

```json
{"id":"3","type":"config","data":{"ports":{"4":{"poe":{"enabled":true,"mode":"af"}}}}}
```

A successful response is an `ack`. The daemon then broadcasts the complete `config` and updated `status` to connected clients.

## Responses

### Acknowledgement

```json
{"id":"3","type":"ack","data":{"message":"Configuration accepted","applied":2,"warnings":[]}}
```

Warnings mean the desired state was valid and saved, but one or more runtime operations could not be verified or applied. Clients must not treat warnings as `Bad Request`.

### Bad request

```json
{"id":"3","type":"error","data":{"status":400,"message":"Bad Request","detail":"ports.49.poe is not supported by port 49"}}
```

Bad requests are not saved and do not change hardware.

### Broadcasts

```json
{"type":"config","data":{}}
{"type":"status","data":{}}
```

Broadcasts do not normally contain an `id`.

## CLI equivalents

```sh
configd --get-status
configd --get-config
configd --apply-json '{"ports":{"1":{"enabled":false}}}'
configd --apply-file ./delta.json
configd --validate ./complete.json
```

The CLI intentionally uses the same envelopes and validation rules so a future standalone CLI can use either local process execution or the WebSocket transport without changing its data model.
