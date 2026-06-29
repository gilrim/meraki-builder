# Firmware updates

Fwupdate verifies checksum, release metadata, exact-model compatibility, flash geometry, version direction, overlay policy, requested flash scope, and required acknowledgements before destructive work.

## Compatibility states

- `validated` — normal path for an exact model validated with the release artifact.
- `confirmed` — normal path based on recorded target confirmation.
- `untested` — requires the exact `ACCEPT-UNTESTED` acknowledgement.
- `known-incompatible` — blocked and cannot be overridden.
- missing or unsupported metadata — blocked by the normal path.

Installing the same or an older version requires force. Force never overrides known incompatibility, invalid geometry, corrupt metadata, or a missing full-flash acknowledgement.

## Sources

Local files, TFTP, HTTP/HTTPS, SFTP, browser upload, and Linux UART feed the same updater engine. Browser upload preserves the original filename and matching JSON sidecar manifest. Linux UART uses acknowledged Base64 frames with sequence numbers and CRC-32, then verifies byte count and whole-object SHA-256 before publishing the candidate in RAM.

## Asynchronous browser validation

Completing a browser upload starts a server-side validation job and returns status without blocking configd’s event loop. The candidate moves through receiving, validating, ready, or failed states. The UI polls `firmware_upload_status` and can recover the job after reconnect or page reload. Cancellation and session revocation terminate owned staging jobs and clean their temporary objects.

Validation alone never writes flash.

## Installation lifecycle

```text
idle → receiving → validating → ready → waiting-for-client-acknowledgement
     → starting → erasing → writing → verifying-flash
     → persisting-result → rebooting → complete/failed
```

Live status and logs are under `/run/fwupdate`; bounded persistent history is under `/config/postmerkos/update-history`. Interrupted operations are finalized as interrupted or indeterminate, never as success.

The root filesystem is single-bank. Power loss during erase or program can require external recovery even when configuration preservation is selected.

## Chassis indication

On MS42P, accepted installation alternates green/orange and accelerates with progress, failure/rollback uses triple-orange pulses, and verified success remains solid green until reboot. Checksum, compatibility, and verify-only checks do not acquire the indicator. See [Reset button and chassis status LED](reset-button-and-status-led.md).

## Pre-kernel recovery

When Linux cannot run but the UART-capable loader starts, use the [pre-kernel recovery procedure](../installation/recovery.md#pre-kernel-uart-recovery). Verify and dry-run operations are non-destructive. Flash mode rewrites the complete 16 MiB NOR and requires independent host and target authorization.
