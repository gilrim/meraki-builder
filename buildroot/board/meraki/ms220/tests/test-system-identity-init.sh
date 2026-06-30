#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SCRIPT="$ROOT/overlay/etc/init.d/S08postmerkos-identity"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP/bin" "$TMP/etc" "$TMP/run"
cat >"$TMP/bin/hostname" <<'MOCK'
#!/bin/sh
printf '%s\n' "$1" >"$POSTMERKOS_HOSTNAME_CAPTURE"
MOCK
chmod +x "$TMP/bin/hostname"
cat >"$TMP/default.json" <<'JSON'
{"hostname":"postmerkos"}
JSON
cat >"$TMP/system.json" <<'JSON'
{"hostname":"Core-Switch"}
JSON
POSTMERKOS_PATH="$TMP/bin:$PATH" \
POSTMERKOS_SYSTEM_POLICY="$TMP/system.json" \
POSTMERKOS_SYSTEM_DEFAULT="$TMP/default.json" \
POSTMERKOS_HOSTNAME_FILE="$TMP/etc/hostname" \
POSTMERKOS_AVAHI_CONF="$TMP/etc/avahi-daemon.conf" \
POSTMERKOS_RUN_DIR="$TMP/run" \
POSTMERKOS_HOSTNAME_CAPTURE="$TMP/hostname.capture" \
  "$SCRIPT" start
[ "$(cat "$TMP/hostname.capture")" = core-switch ]
[ "$(cat "$TMP/etc/hostname")" = core-switch ]
[ "$(cat "$TMP/run/effective-hostname")" = core-switch ]
grep -Fxq 'host-name=core-switch' "$TMP/etc/avahi-daemon.conf"
grep -Fxq 'allow-interfaces=linux_mgmt' "$TMP/etc/avahi-daemon.conf"
grep -Fxq 'enable-dbus=no' "$TMP/etc/avahi-daemon.conf"
# Invalid persistent values fail closed to the shipped default.
printf '%s\n' '{"hostname":"bad.name"}' >"$TMP/system.json"
POSTMERKOS_PATH="$TMP/bin:$PATH" \
POSTMERKOS_SYSTEM_POLICY="$TMP/system.json" \
POSTMERKOS_SYSTEM_DEFAULT="$TMP/default.json" \
POSTMERKOS_HOSTNAME_FILE="$TMP/etc/hostname" \
POSTMERKOS_AVAHI_CONF="$TMP/etc/avahi-daemon.conf" \
POSTMERKOS_RUN_DIR="$TMP/run" \
POSTMERKOS_HOSTNAME_CAPTURE="$TMP/hostname.capture" \
  "$SCRIPT" restart
[ "$(cat "$TMP/hostname.capture")" = postmerkos ]
grep -Fxq 'host-name=postmerkos' "$TMP/etc/avahi-daemon.conf"
printf '%s\n' 'system identity and management-only mDNS init tests passed'
