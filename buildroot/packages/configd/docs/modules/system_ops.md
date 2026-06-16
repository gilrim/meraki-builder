# system_ops

`system_ops.c` contains the operating-system actions exposed by authenticated WebSocket requests:

- root command execution through `/bin/sh -c`, with a 15-second timeout and 64 KiB combined stdout/stderr limit;
- parsing `/run/fwupdate/status.json`;
- detached handoff of an uploaded image to the existing `fw_update` validator/flasher.

The updater receives the browser-computed SHA-256, overlay policy, source label, noninteractive confirmation, and optional force flag. `fw_update` remains responsible for image classification, checksum verification, flash-region safety, status reporting, service shutdown, writing, verification, and reboot.
