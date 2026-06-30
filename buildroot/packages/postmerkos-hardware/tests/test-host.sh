#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
BUTTON_PID=
cleanup() {
  [ -z "$BUTTON_PID" ] || kill "$BUTTON_PID" >/dev/null 2>&1 || true
  rm -rf "$TMP"
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$TMP/run" "$TMP/click/sw0_ctrl" "$TMP/sys/class/gpio" "$TMP/dev/input" "$TMP/profiles" "$TMP/bin"
cp "$ROOT/files/hardware-controls-default.conf" "$TMP/profiles/default.conf"
cp "$ROOT"/files/profiles/*.conf "$TMP/profiles/"
for profile in "$TMP"/profiles/*.conf; do sed -i "s#=/click/#=$TMP/click/#g" "$profile"; done
truncate -s 8192 "$TMP/mmio"
sed -i "s#RESET_DEVICE=/dev/mem#RESET_DEVICE=$TMP/mmio#; s/RESET_ADDRESS=0x60010074/RESET_ADDRESS=0x00001074/" "$TMP/profiles/MS42P.conf"
printf '0\n' > "$TMP/click/sw0_ctrl/power_led_green"
printf '0\n' > "$TMP/click/sw0_ctrl/power_led_orange"
printf 'link\n' > "$TMP/click/sw0_ctrl/led_mode"
printf '0\n' > "$TMP/click/sw0_ctrl/poe_led_state"
chmod 666 "$TMP/click/sw0_ctrl/"* "$TMP/mmio"
write_mmio() {
  python3 - "$TMP/mmio" "$1" <<'PY'
import struct, sys
with open(sys.argv[1], 'r+b', buffering=0) as f:
    f.seek(0x1074)
    f.write(struct.pack('=I', int(sys.argv[2], 0)))
PY
}
write_mmio 0x00002000
run_probe() {
  model=$1 exact=${2:-1}
  printf 'MODEL=%s\nIDENTITY_EXACT=%s\n' "$model" "$exact" > "$TMP/boardinfo"
  POSTMERKOS_RUN_DIR="$TMP/run" \
  POSTMERKOS_HARDWARE_PROFILE_DIR="$TMP/profiles" \
  POSTMERKOS_BOARDINFO="$TMP/boardinfo" \
  POSTMERKOS_CLICK_ROOT="$TMP/click" \
  POSTMERKOS_SYS_ROOT="$TMP/sys" \
  POSTMERKOS_PROC_INPUT="$TMP/proc-input" \
  POSTMERKOS_DEV_INPUT="$TMP/dev/input" \
    "$ROOT/files/postmerkos-hwprobe" refresh
}

# The MS42P profile is the only currently hardware-verified destructive reset
# path.  It uses the primary Jaguar1 GPIO input register, not an assumed Linux
# GPIO number.
run_probe MS42P
python3 - "$TMP/run/hardware-controls.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['schema_version'] == 3
assert p['profile_exact'] is True
r=p['reset_button']
assert r['backend'] == 'jaguar1-mmio'
assert r['gpio'] == 13 and r['active_low'] is True
assert r['address'] == 0x60010074 or r['address'] == 0x1074
assert r['mask'] == 0x2000
assert r['available'] is True and r['verified'] is True
assert r['destructive_enabled'] is True
led=p['leds']['status']
assert led['backend'] == 'click-dual-state'
assert led['available'] is True and led['verified'] is True
assert led['protocol'] == 'plain-bool'
assert led['green']['gpio'] == 22 and led['orange']['gpio'] == 23
assert led['confidence'] == 'hardware-verified'
assert p['leds']['panel']['automatic_use'] is False
PY

# Non-MS42P platforms remain non-destructive even when an exact profile exists.
run_probe MS220-8P
python3 - "$TMP/run/hardware-controls.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['schema_version'] == 3
assert p['profile_exact'] is True
assert p['leds']['status']['available'] is True
assert p['leds']['status']['protocol'] == 'plain-bool'
assert p['leds']['ports']['backend'] == 'click-poe-led-state'
assert p['leds']['ports']['available'] is True
assert p['reset_button']['destructive_enabled'] is False
PY
run_probe MS220-24P
python3 - "$TMP/run/hardware-controls.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['profile_exact'] is True
assert p['leds']['ports']['available'] is False
assert p['reset_button']['destructive_enabled'] is False
PY

# A model string without immutable exact identity may not inherit write/destructive
# authority from its named profile.
run_probe MS42P 0
python3 - "$TMP/run/hardware-controls.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['profile_exact'] is False
assert p['leds']['status']['available'] is False
assert p['leds']['status']['verified'] is False
assert p['reset_button']['available'] is False
assert p['reset_button']['verified'] is False
assert p['reset_button']['destructive_enabled'] is False
PY
printf 'MODEL=UNKNOWN\nIDENTITY_EXACT=1\n' > "$TMP/boardinfo"
POSTMERKOS_RUN_DIR="$TMP/run" POSTMERKOS_HARDWARE_PROFILE_DIR="$TMP/profiles" \
POSTMERKOS_BOARDINFO="$TMP/boardinfo" POSTMERKOS_CLICK_ROOT="$TMP/click" \
POSTMERKOS_SYS_ROOT="$TMP/sys" POSTMERKOS_PROC_INPUT="$TMP/proc-input" \
POSTMERKOS_DEV_INPUT="$TMP/dev/input" "$ROOT/files/postmerkos-hwprobe" refresh
python3 - "$TMP/run/hardware-controls.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['profile_exact'] is False
assert p['leds']['status']['available'] is False
assert p['leds']['ports']['available'] is False
assert p['reset_button']['destructive_enabled'] is False
PY

# Confirm the Click dual-state handlers receive the proven plain boolean syntax
# and that captured normal state is restored after ownership release.
run_probe MS42P
cat > "$TMP/bin/postmerkosctl" <<'EOF'
#!/bin/sh
echo '{"capabilities":{"copper_ports":48},"copper_ports":48}'
EOF
chmod +x "$TMP/bin/postmerkosctl"
cat > "$TMP/bin/fast-hwprobe" <<EOF
#!/bin/sh
# led-capabilities.env was generated by run_probe above. LED hot paths must use
# that cache instead of repeatedly invoking board discovery.
printf '%s\n' called >> "$TMP/hwprobe-hotpath-called"
exit 0
EOF
chmod +x "$TMP/bin/fast-hwprobe"
LED_ENV="POSTMERKOS_RUN_DIR=$TMP/run POSTMERKOS_HWPROBE=$TMP/bin/fast-hwprobe POSTMERKOSCTL=$TMP/bin/postmerkosctl POSTMERKOS_HARDWARE_PROFILE_DIR=$TMP/profiles POSTMERKOS_BOARDINFO=$TMP/boardinfo POSTMERKOS_CLICK_ROOT=$TMP/click POSTMERKOS_SYS_ROOT=$TMP/sys POSTMERKOS_PROC_INPUT=$TMP/proc-input POSTMERKOS_DEV_INPUT=$TMP/dev/input"
# Status events emitted during checksum/platform validation occur before the
# updater owns the indicator. They must be harmless and preserve normal state.
printf '0\n' > "$TMP/click/sw0_ctrl/power_led_green"
printf '1\n' > "$TMP/click/sw0_ctrl/power_led_orange"
env $LED_ENV "$ROOT/files/postmerkos-ledctl" firmware-progress 10
sleep 0.2
grep -qx '0' "$TMP/click/sw0_ctrl/power_led_green"
grep -qx '1' "$TMP/click/sw0_ctrl/power_led_orange"
[ ! -e "$TMP/run/led-state" ]

env $LED_ENV "$ROOT/files/postmerkos-ledctl" acquire firmware
env $LED_ENV "$ROOT/files/postmerkos-ledctl" firmware-progress 50
sleep 0.25
env $LED_ENV "$ROOT/files/postmerkos-ledctl" success
grep -qx '1' "$TMP/click/sw0_ctrl/power_led_green"
grep -qx '0' "$TMP/click/sw0_ctrl/power_led_orange"
env $LED_ENV "$ROOT/files/postmerkos-ledctl" release firmware
[ ! -e "$TMP/hwprobe-hotpath-called" ]
# A contended LED control lock must honor the short attempt budget used by the
# physical-button daemon instead of consuming the destructive hold interval.
ln -s "$(command -v sleep)" "$TMP/bin/postmerkos-ledctl-holder"
"$TMP/bin/postmerkos-ledctl-holder" 30 & holder_pid=$!
mkdir "$TMP/run/led-control.lock"
printf '%s\n' "$holder_pid" > "$TMP/run/led-control.lock/pid"
if timeout 2 env $LED_ENV POSTMERKOS_LED_LOCK_ATTEMPTS=2 POSTMERKOS_LED_SKIP_REFRESH=1 \
  "$ROOT/files/postmerkos-ledctl" acquire reset >/dev/null 2>&1; then
  echo 'contended LED lock unexpectedly acquired' >&2
  exit 1
fi
kill "$holder_pid" >/dev/null 2>&1 || true
wait "$holder_pid" 2>/dev/null || true
rm -f "$TMP/run/led-control.lock/pid"
rmdir "$TMP/run/led-control.lock"
# Original values at acquisition were green=0 and orange=1. The animator must
# be fully stopped before restoration or its EXIT trap can race and clear them.
grep -qx '0' "$TMP/click/sw0_ctrl/power_led_green"
grep -qx '1' "$TMP/click/sw0_ctrl/power_led_orange"
! grep -q 'STATE' "$TMP/click/sw0_ctrl/power_led_green"
# A stale animator PID must never kill an unrelated process after PID reuse.
sleep 30 &
unrelated_pid=$!
printf '%s\n' "$unrelated_pid" > "$TMP/run/led-animator.pid"
env $LED_ENV "$ROOT/files/postmerkos-ledctl" release
kill -0 "$unrelated_pid"
kill "$unrelated_pid" >/dev/null 2>&1 || true
wait "$unrelated_pid" 2>/dev/null || true

# Concurrent ownership requests must be serialized. Firmware has higher priority
# than reset, so it must remain the final owner regardless of process ordering.
env $LED_ENV "$ROOT/files/postmerkos-ledctl" release >/dev/null 2>&1 || true
i=0
while [ "$i" -lt 3 ]; do
  (env $LED_ENV "$ROOT/files/postmerkos-ledctl" acquire reset >/dev/null 2>&1 || true) &
  (env $LED_ENV "$ROOT/files/postmerkos-ledctl" acquire firmware >/dev/null 2>&1 || true) &
  i=$((i + 1))
done
wait
[ "$(env $LED_ENV "$ROOT/files/postmerkos-ledctl" status)" = firmware ]
env $LED_ENV "$ROOT/files/postmerkos-ledctl" release firmware

# Strictly compile the daemon and exercise held-at-boot protection, release-to-arm,
# active-low decoding, countdown, LED calls, and factory-reset handoff.
cc -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags json-c) \
  -o "$TMP/bin/postmerkos-buttond" "$ROOT/buttond.c" $(pkg-config --libs json-c)
# Missing, malformed, or incomplete destructive policy must fail closed even when
# the hardware capability itself is exact and verified.
run_probe MS42P
for case_name in missing malformed missing-action wrong-action; do
  policy="$TMP/security-$case_name.json"
  case "$case_name" in
    missing) rm -f "$policy" ;;
    malformed) printf '{malformed\n' > "$policy" ;;
    missing-action) printf '%s\n' '{"reset_button":{"enabled":true,"hold_seconds":3}}' > "$policy" ;;
    wrong-action) printf '%s\n' '{"reset_button":{"enabled":true,"hold_seconds":3,"action":"reboot"}}' > "$policy" ;;
  esac
  status="$TMP/run/button-status-$case_name.json"
  POSTMERKOS_SECURITY_FILE="$policy" \
  POSTMERKOS_HARDWARE_CONTROLS="$TMP/run/hardware-controls.json" \
  POSTMERKOS_BUTTON_STATUS="$status" \
  POSTMERKOS_LEDCTL="$TMP/bin/nonexistent-ledctl" \
  POSTMERKOS_FACTORY_RESET="$TMP/bin/nonexistent-factory-reset" \
  POSTMERKOS_FWUPDATE_LOCK="$TMP/fwupdate.lock" \
    "$TMP/bin/postmerkos-buttond" >"$TMP/buttond-$case_name.log" 2>&1 &
  policy_pid=$!
  i=0
  while [ "$i" -lt 20 ] && [ ! -s "$status" ]; do sleep 0.05; i=$((i+1)); done
  python3 - "$status" <<'PY_STATUS'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'disabled', p
assert p['enabled'] is False, p
PY_STATUS
  kill "$policy_pid" >/dev/null 2>&1 || true
  wait "$policy_pid" 2>/dev/null || true
done
cat > "$TMP/security.json" <<'EOF'
{"reset_button":{"enabled":true,"hold_seconds":10,"action":"factory-reset"}}
EOF
cat > "$TMP/bin/ledctl" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> "$TMP/led-calls"
exit 0
EOF
cat > "$TMP/bin/factory-reset" <<EOF
#!/bin/sh
printf '%s\n' "\$*" > "$TMP/factory-reset-called"
exit 0
EOF
chmod +x "$TMP/bin/ledctl" "$TMP/bin/factory-reset"
run_probe MS42P
# Start while pressed; a stuck/held button must never reset the device.
write_mmio 0x00000000
POSTMERKOS_SECURITY_FILE="$TMP/security.json" \
POSTMERKOS_HARDWARE_CONTROLS="$TMP/run/hardware-controls.json" \
POSTMERKOS_BUTTON_STATUS="$TMP/run/button-status.json" \
POSTMERKOS_LED_STATE="$TMP/run/led-state" \
POSTMERKOS_LEDCTL="$TMP/bin/ledctl" \
POSTMERKOS_FACTORY_RESET="$TMP/bin/factory-reset" \
POSTMERKOS_FWUPDATE_LOCK="$TMP/fwupdate.lock" \
POSTMERKOS_FACTORY_RESET_SKIP_SYNC=1 \
  "$TMP/bin/postmerkos-buttond" >"$TMP/buttond.log" 2>&1 &
BUTTON_PID=$!
sleep 3.4
[ ! -e "$TMP/factory-reset-called" ]
python3 - "$TMP/run/button-status.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'waiting-release'
assert p['gpio'] == 13 and p['active_low'] is True
assert p['pressed'] is True and p['armed'] is False
PY
# Release long enough to arm. The host test uses a long threshold so scheduler stalls cannot turn the partial-press check into a destructive hold.
write_mmio 0x00002000
sleep 0.9
python3 - "$TMP/run/button-status.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'ready'
assert p['pressed'] is False and p['armed'] is True
assert p['last_event'] == 'released-and-armed'
PY
# A partial hold must be reported live and a release must cancel it promptly.
write_mmio 0x00000000
sleep 0.4
python3 - "$TMP/run/button-status.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'countdown', p
assert p['pressed'] is True and p['countdown_active'] is True
PY
write_mmio 0x00002000
sleep 0.5
python3 - "$TMP/run/button-status.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'ready', p
assert p['pressed'] is False and p['countdown_active'] is False
assert p['last_event'] == 'released'
PY
grep -q '^release reset$' "$TMP/led-calls"
# Now hold continuously through the 10-second host-test policy.
write_mmio 0x00000000
i=0
while [ "$i" -lt 120 ] && [ ! -e "$TMP/factory-reset-called" ]; do sleep 0.1; i=$((i+1)); done
[ -e "$TMP/factory-reset-called" ]
wait "$BUTTON_PID" || true
BUTTON_PID=
grep -qx -- '--yes' "$TMP/factory-reset-called"
grep -q '^acquire reset$' "$TMP/led-calls"
grep -q '^reset-progress 0$' "$TMP/led-calls"
grep -q '^progress=100$' "$TMP/run/led-state"
python3 - "$TMP/run/button-status.json" <<'PY'
import json, sys
p=json.load(open(sys.argv[1]))
assert p['state'] == 'triggered' and p['progress'] == 100
PY

# Reset indication must be visibly faster than the normal heartbeat and accelerate.
python3 - "$ROOT/files/postmerkos-ledctl" "$ROOT/buttond.c" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text(); button=Path(sys.argv[2]).read_text()
assert 'cycle=$((500 - 380*pct/100))' in s
assert '[ "$cycle" -lt 120 ] && cycle=120' in s
assert 'POSTMERKOS_LED_LOCK_ATTEMPTS' in s
assert 'POSTMERKOS_LED_SKIP_REFRESH' in s
assert 'POSTMERKOS_LED_LOCK_ATTEMPTS", "10"' in button
assert 'POSTMERKOS_LED_SKIP_REFRESH", "1"' in button
PY

# Only the two static-proven 8-port profiles may expose poe_led_state.
for profile in "$ROOT"/files/profiles/*.conf; do
  case "${profile##*/}" in
    MS220-8.conf|MS220-8P.conf) grep -q '^PORT_LED_BACKEND=click-poe-led-state$' "$profile" ;;
    *) ! grep -q '^PORT_LED_BACKEND=click-poe-led-state$' "$profile" ;;
  esac
done
printf '%s\n' 'postmerkos-hardware host tests passed'
