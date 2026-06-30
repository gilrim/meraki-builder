# Factory-reset overlay soft-brick correction — 2026-06-30

This document records a resolved implementation defect. Current operation is documented in [Reset button and chassis status LED](../user-guide/reset-button-and-status-led.md).

## Observed failure

A verified MS42P reset-button hold completed and rebooted, but the resulting switch exposed only the static landing/sign-in page. WebSocket authentication did not become usable, SSH rejected or refused connections, and recovery required UART or external SPI flashing. The chassis LED also failed to show the expected rapid countdown, and button release was not reliably reflected in the browser.

## Root cause

The former `/bin/fw_factory_reset` used `flash_erase` to leave the complete JFFS2 partition blank. The boot filesystem contract mounts:

- `/overlay` from JFFS2;
- `/etc` with `upperdir=/overlay/.upper/etc` and `workdir=/overlay/.work/etc`;
- `/root` with `upperdir=/overlay/.upper/root` and `workdir=/overlay/.work/root`.

A blank JFFS2 filesystem did not contain those upper/work directories. The `/etc` and `/root` overlay mounts therefore failed before first-boot initialization. `/etc` remained read-only, which prevented password replacement and Dropbear host-key generation. Persistent defaults and root SSH state could not be created correctly. Static uHTTPd content remained readable from SquashFS, explaining why the landing page survived while authenticated management did not.

The release-reporting issue had a separate scheduling cause. The button daemon synchronously executed the LED-control shell command for each progress change. Hardware probing and ownership locking could block the GPIO sampling loop, so a release transition could be observed late or missed before the hold threshold.

## Correction

- Factory reset now creates and verifies a seeded JFFS2 image instead of performing a raw erase.
- The existing static `fwflash` engine gained an overlay-only factory-reset mode with full readback verification and raw-image rollback.
- The reset path quiesces the configd supervisor and all other userspace writers before unmounting; the rollback image is captured only after `/etc`, `/root`, and `/overlay` are detached.
- An early `S01postmerkos-overlay` init stage re-executes from `/run`, repairs missing directory layout, safely detaches partial mounts, and provides a temporary RAM-backed recovery overlay when persistent JFFS2 is unavailable.
- Configd reports recovery-overlay mode as a management warning.
- Reset GPIO sampling uses direct `pread()` samples and publishes debounced press/release transitions.
- The browser uses a dedicated 500 ms reset-status request rather than waiting for the full status interval.
- The LED controller is started once; subsequent countdown changes are atomic state-file updates that do not block GPIO polling.
- The reset indication was changed to a clearly visible 2–8 Hz accelerating orange pattern and is transferred to the RAM-resident flasher after userspace quiescence.

## Safety result

A programming failure restores and verifies the previous JFFS2 image before reboot. A blank or damaged persistent overlay no longer prevents the management plane from starting; the switch enters an explicitly reported nonpersistent recovery mode instead.
