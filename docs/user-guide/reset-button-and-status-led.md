# Reset button and chassis status LED

Physical controls are capability driven. Persistent configuration stores policy only; GPIO numbers, MMIO addresses, polarity, and Click handler paths come from exact-model, read-only hardware profiles.

## MS42P reset button

The MS42P front-panel reset input is hardware verified as primary Jaguar1 GPIO13, active low. The runtime samples `DEVCPU_GCB.GPIO_IN` at physical address `0x60010074` through read-only `/dev/mem` access and tests mask `0x00002000`.

Factory-reset handling starts only when all of these conditions are true:

1. immutable identity reports exact model `MS42P`;
2. the hardware profile is marked verified and destructive-enabled;
3. the verified MMIO input is readable;
4. `/config/postmerkos/security.json` explicitly enables the reset button and sets `action` to `factory-reset`.

Missing, malformed, incomplete, or mismatched policy fails closed.

### Hold behavior

- The button must remain released for 500 ms after the daemon starts before it arms.
- Input changes are debounced for 150 ms.
- The default hold is 10 seconds; supported policy values are 3 through 60 seconds.
- Press, release, arming, countdown, and cancellation are published immediately after debounce.
- Releasing the button during countdown cancels the action.
- Firmware activity inhibits and cancels reset countdown.
- Factory reset and firmware installation share the same destructive-operation lock.
- The System Information panel polls the dedicated reset status every 500 ms. It shows the physical `Pressed` or `Released` state, arming state, countdown percentage, LED availability, and last button event.
- The detailed runtime state remains available at `/run/postmerkos/button-status.json`.

Only MS42P currently has destructive reset-button authority in the shipped profiles.

## Factory-reset transaction

Factory reset does not leave the JFFS2 partition blank. The reset process:

1. creates a valid 5 MiB JFFS2 image in RAM;
2. seeds the required `.upper/etc`, `.work/etc`, `.upper/root`, and `.work/root` overlayfs layout;
3. stages the static flash helper in RAM;
4. stages its own working directory and static helper in RAM;
5. quiesces all userspace except PID 1, the reset process, and the verified LED animator, preventing configd or another supervisor from respawning writers;
6. unmounts `/root`, `/etc`, and `/overlay` and captures a complete raw rollback image of the unmounted JFFS2 partition;
7. transfers the verified LED paths to the static helper, which continues the rapid orange indication;
8. erases, writes, reads back, and verifies the seeded factory-default image;
9. restores and verifies the previous raw image automatically if programming fails;
10. reboots only after verification or rollback completes.

On the next boot, normal first-boot initialization recreates default service, security, time, and repository policy. The root password is returned to the device serial number, the default-password warning is restored, and a new Dropbear ECDSA host key is generated.

## Writable-overlay recovery

`S01postmerkos-overlay` runs before account, SSH-key, password, and management-service initialization. It re-executes itself from `/run` so a partially mounted `/etc` cannot keep itself busy, verifies that `/overlay` is writable, creates any missing upper/work directories, and mounts writable overlays on `/etc` and `/root`.

If persistent JFFS2 cannot be mounted or written, the switch uses a temporary RAM-backed recovery overlay instead of continuing with read-only `/etc` and `/root`. SSH and browser management can therefore start for diagnosis and recovery. System Information reports **persistent overlay recovery mode**, and changes made in that mode do not survive reboot.

## MS42P chassis LED

The verified MS42P status output uses primary Jaguar1 GPIO22 for active-high green and GPIO23 for active-high orange. Orange is dominant when both are asserted. The vendor Click handlers accept plain `0` or `1` values:

```text
/click/sw0_ctrl/power_led_green
/click/sw0_ctrl/power_led_orange
```

## Indication patterns

- **Firmware installation:** alternating green/orange, accelerating with progress.
- **Failure or rollback:** repeating triple-orange pulse.
- **Successful flash verification:** solid green until reboot.
- **Reset countdown:** orange blinking at approximately 2 Hz when the hold begins, accelerating to approximately 8 Hz near the destructive threshold.
- **Factory-reset handoff:** rapid orange continues while services stop and the seeded JFFS2 image is written.
- **Normal operation or cancelled reset:** restore the exact green/orange state captured before temporary ownership.

The reset daemon acquires the LED once and then updates the animator through atomic runtime state writes. LED updates cannot block GPIO sampling or delay release detection. If the status LED cannot be acquired, the countdown remains active but System Information displays a warning; releasing the button still cancels it.

Checksum checks, platform checks, and verify-only operations do not acquire the chassis indicator. Ownership is serialized; firmware indication has priority over reset indication. The RAM-resident flasher continues the firmware pattern after normal userspace has been quiesced. Verified port LEDs are fallback-only on models without a verified chassis indicator.

The LED pattern is advisory. Runtime status, updater logs, and the hardware console remain authoritative.
