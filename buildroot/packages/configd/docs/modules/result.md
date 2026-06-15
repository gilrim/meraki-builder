# result module

`result.c` records how many runtime operations were applied and collects nonfatal warning strings.

An apply result is returned in CLI/WebSocket acknowledgements:

```json
{"message":"Configuration accepted","applied":3,"warnings":["port 4 PoE desired state retained; controller unavailable"]}
```

Warnings indicate a valid saved request with incomplete runtime application or verification. They are intentionally distinct from validation errors.
