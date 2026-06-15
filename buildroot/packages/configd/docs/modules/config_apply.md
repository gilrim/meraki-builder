# config_apply module

`config_apply.c` is the transaction coordinator for desired state.

## Delta transaction

1. Load current complete JSON.
2. Deep-copy it.
3. Deep-merge the requested delta.
4. Validate the complete merged object.
5. Atomically save it.
6. Apply only sections touched by the delta.

At daemon startup `config_apply_full()` replays all settings. Storm control is deliberately sourced from persistent configuration because the binary `SwitchPortTable` provides a setter but no reliable readback handler.

A valid configuration can be saved even when a runtime handler is temporarily unavailable; this is returned as an acknowledgement warning. Invalid data is never saved.
