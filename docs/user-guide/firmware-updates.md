# Firmware updates

Fwupdate validates release metadata, hardware family, flash geometry, version direction, checksum, and overlay policy before destructive work.

## Candidate states

- Newer: normal update
- Same or older: requires force
- Untested model: warning acknowledgement, no force required solely for test status
- Known-incompatible: rejected normally
- Missing/unsupported metadata: rejected normally, force available for deliberate development

## Lifecycle

```text
idle → receiving → verifying → ready → waiting-for-client-acknowledgement
     → starting → erasing → writing → verifying-flash
     → persisting-result → rebooting → complete/failed
```

A new web upload receives a unique token and purges all previous staged images. Verification does not start flashing. The browser must acknowledge `ready_to_flash` before configd launches the destructive phase.

Live status and logs are written under `/run/fwupdate`. A bounded result and log are persisted under `/config/postmerkos/update-history` before reboot and finalized on the next boot. Interrupted updates are reported as interrupted or unknown rather than success.

Progress is sent to the browser while available, directly to `/dev/console`, and to model-capable port/PoE LEDs. Both interfaces explain the detected LED behavior before starting.

Firmware repositories use JSON manifests and can be managed through the web UI or console-supported updater paths.
