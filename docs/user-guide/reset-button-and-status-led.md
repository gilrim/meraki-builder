# Reset button and chassis status LED

Physical controls are capability driven. Persistent configuration stores policy only; GPIO numbers, MMIO addresses, polarity, and Click handler paths come from exact-model, read-only hardware profiles.

## MS42P reset button

The MS42P front-panel reset input is hardware verified as primary Jaguar1 GPIO13, active low. The runtime reads `DEVCPU_GCB.GPIO_IN` at physical address `0x60010074` through a read-only `/dev/mem` mapping and tests mask `0x00002000`.

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
- Releasing the button during countdown cancels the action.
- Firmware activity inhibits and cancels reset countdown.
- Factory reset and firmware installation share the same destructive-operation lock.
- Live state is written to `/run/postmerkos/button-status.json` and displayed in System Information.

Only MS42P currently has destructive reset-button authority in the shipped profiles.

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
- **Reset countdown and factory reset:** accelerating orange pulse.
- **Normal operation:** restore the exact green/orange state captured before temporary ownership.

Checksum checks, platform checks, and verify-only operations do not acquire the chassis indicator. Ownership is serialized; firmware indication has priority over reset indication. The RAM-resident flasher continues the firmware pattern after normal userspace has been quiesced. Verified port LEDs are fallback-only on models without a verified chassis indicator.

The LED pattern is advisory. Firmware status files, updater logs, and the hardware console remain authoritative.
