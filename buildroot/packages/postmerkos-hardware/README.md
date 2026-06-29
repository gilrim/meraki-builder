# postmerkOS hardware policy

## Purpose

This package provides capability-driven chassis controls outside the normal Click port-configuration path. It owns the physical reset-button policy, chassis/status LED ownership, firmware/reset indication, and the optional PoE boot-prune policy.

## Installed components

- `postmerkos-hwprobe` combines immutable board identity with source-controlled hardware evidence and renders fail-closed runtime capabilities.
- `postmerkos-buttond` monitors only an exact-model, hardware-verified reset input and performs a cancellable hold-to-reset sequence.
- `postmerkos-ledctl` arbitrates chassis LED ownership, captures/restores normal state, and renders firmware/reset/failure patterns.
- `postmerkos-poe-prune` performs the one-time boot observation for ports configured with the `boot-prune` PoE policy.
- `S16postmerkos-hardware` renders capabilities and starts `postmerkos-buttond` only when destructive handling is explicitly verified and enabled.

## Hardware controls

Physical wiring and control methods are registered in read-only profiles under:

```text
/usr/share/postmerkos/hardware-controls/
```

The boot-validated result is written to:

```text
/run/postmerkos/hardware-controls.json
/run/postmerkos/led-capabilities.env
/run/postmerkos/button-status.json
```

Persistent JFFS2 configuration stores policy only. It never stores GPIO numbers, register addresses, event-device paths, or Click handlers, so restoring one model's configuration cannot authorize another model's wiring.

## MS42P reset button

OpenVTSS hardware evidence identifies the front-panel button as primary Jaguar1 GPIO13, active low. Production reads the read-only `DEVCPU_GCB.GPIO_IN` register at physical address `0x60010074` and tests mask `0x00002000`; it does not assume that Linux GPIO number 13 maps to the ASIC pin.

The shipped policy requires all of the following before destructive handling starts:

1. immutable board identity reports exact `MS42P`;
2. the profile is marked hardware verified;
3. `/dev/mem` can be opened read-only and mapped at the verified register;
4. the runtime capability retains `destructive_enabled=true`;
5. `/config/postmerkos/security.json` explicitly sets `reset_button.enabled=true` and `reset_button.action="factory-reset"`; a missing, malformed, or incomplete policy disables the button.

Safety behavior:

- the button must be continuously released for 500 ms after daemon start before arming;
- transitions are debounced for 150 ms;
- the default reset hold is 10 seconds and may be set only from 3 through 60 seconds;
- releasing during countdown cancels the action and restores the prior LED state;
- the common firmware/factory-reset lock inhibits the button during every flash operation;
- failure to hand off to the factory-reset executable reports an error and releases LED ownership rather than leaving a stale reset pattern;
- factory reset acquires the same lock before erasing JFFS2, closing the final check/start race.

All other shipped model profiles remain destructive-disabled.

## Chassis status LED

On MS42P, OpenVTSS verifies primary GPIO22 as active-high green and GPIO23 as active-high orange-dominant. The vendor Click handlers are:

```text
/click/sw0_ctrl/power_led_green
/click/sw0_ctrl/power_led_orange
```

They accept plain `0` or `1` values. `STATE 0|1` is not used for these handlers.

Ownership and indications:

Status callbacks are owner-gated. Checksum/platform validation and verify-only runs do not acquire or alter the chassis indicator. Ownership changes are serialized with a stale-safe process lock, so simultaneous reset and firmware requests cannot bypass the priority policy.

- **accepted firmware update:** alternating green and orange, accelerating with progress;
- **failure or rollback:** repeating triple orange pulse;
- **successful flash verification:** solid green until reboot;
- **reset-button countdown/factory reset:** accelerating orange pulse;
- **normal operation:** the captured green/orange pair is restored when temporary ownership is released.

The chassis status LED is preferred for upgrades. Verified port LEDs are only a fallback on models without a verified chassis output.

## Diagnostics

```sh
postmerkos-hwprobe reset-button
postmerkos-hwprobe leds
postmerkos-ledctl capabilities
postmerkos-ledctl test-status
cat /run/postmerkos/button-status.json
```

`postmerkos-hwprobe reset-button --watch` remains useful for unverified models, but a discovered transition must not be promoted to destructive authority without exact-model hardware evidence and review.
