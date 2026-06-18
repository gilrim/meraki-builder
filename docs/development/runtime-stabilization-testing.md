# Runtime stabilization hardware testing

This checklist validates the access, WebSocket, reset-button, and LED changes introduced by the Phase 7 runtime-stabilization revision. Run it first on serial console with a known hardware-recovery method available.

## 1. Configd and WebSocket readiness

```sh
configd --features
/etc/init.d/S15configd status && echo ready
ls -l /run/postmerkos/configd.sock
grep -i ':0FA1' /proc/net/tcp /proc/net/tcp6
cat /run/postmerkos/configd.log
cat /run/postmerkos/websocket.log
```

A web build must report WebSocket support, protocol `configd-ws`, and a listener on TCP port 4001 (`0FA1` hexadecimal).

Open the web UI and confirm the login page progresses through transport connection, protocol validation, authentication required, and authenticated role. No status/configuration request should occur before login.

## 2. Role repeatability

From root serial and SSH sessions, run repeatedly:

```sh
for n in 1 2 3 4 5 6 7 8 9 10; do
    postmerkosctl session
    postmerkosctl has users.manage && echo admin
    postmerkosctl has system.reboot && echo reboot-ok
done
```

The role and capability results must not alternate. Repeat with administrator, operator, viewer, and roleless test accounts.

## 3. Serial lifecycle

1. Press Enter at the `pmc:` prompt.
2. Enter and leave several menus.
3. Select Shell, then run `exit`; the console should return.
4. Select Shell, run `pmc`, then exit the nested console and shell.
5. Select Logout; the serial `pmc:` prompt should return.
6. Re-enter without rebooting.

The default-password warning should appear at console entry while `/config/postmerkos/default-password-active` exists.

## 4. SSH lifecycle

- Interactive root/admin login should enter the console.
- Operator and viewer accounts should receive role-filtered consoles.
- A roleless account should not receive an unrestricted shell.
- Administrator Shell should work.
- Logout should close the SSH session.
- Non-interactive commands must remain usable:

```sh
ssh root@SWITCH 'postmerkosctl session'
```

## 5. Service menu

Open Service Management and confirm it begins with a compact table rather than raw JSON. Raw policy output should appear only when explicitly requested.

## 6. Reset-button discovery

Do not enable a guessed reset input.

```sh
postmerkos-hwprobe reset-button
postmerkos-hwprobe reset-button --watch 30
cat /run/postmerkos/hardware-controls.json
cat /run/postmerkos/button-status.json
cat /run/postmerkos/buttond.log
```

Press and release the reset button several times during watch mode. Record any GPIO, LED-class, input-device, platform, or Click state that changes. If only an evdev node is suspected, record `/proc/bus/input/devices` and the corresponding `/dev/input/event*` node for a follow-up profile.

The shipped profile intentionally reports `unidentified` and will not erase JFFS2 until a model mapping is marked verified.

## 7. LED discovery

```sh
postmerkos-hwprobe leds
postmerkos-hwprobe leds --watch 30
postmerkos-ledctl capabilities
```

Capture observations:

- before Click initialization;
- while the chassis status LED is orange;
- when it changes green;
- after all startup scripts complete.

Search candidate paths without writing first. A status LED must be registered and marked verified before `postmerkos-ledctl test-status` is used.

The known `/click/sw0_ctrl/poe_led_state` port method is registered as binary per-port control and is used when the handler is writable.

## 8. Verified reset test

Only after a verified model profile has been added:

1. Short press: detected, no reset.
2. Hold and release before ten seconds: countdown cancels.
3. Hold for ten seconds: JFFS2 is erased and the switch reboots.
4. Hold before daemon startup: current-state detection starts the countdown once the daemon opens the input.
5. Repeat with LED output unavailable; serial/status reporting must still work.

## 9. Firmware LED behavior

Before an update, the UI and console should report the selected LED method:

- binary port progress;
- binary status-LED accelerated blink;
- no LED output.

When a verified status LED is the fallback, its cycle accelerates from 2.0 seconds at 0% to 0.8 seconds at 100%. Firmware failure uses a bounded error pattern before reboot. The RAM-resident `fwflash` helper owns this fallback after normal userspace is stopped.
