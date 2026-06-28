#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
SCRIPT="$ROOT/board/meraki/ms220/overlay/etc/init.d/S16snmpd"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP/bin"
cat > "$TMP/bin/start-stop-daemon" <<'EOS'
#!/bin/sh
printf '%s\n' "$*" >> "$SNMP_TEST_LOG"
EOS
chmod +x "$TMP/bin/start-stop-daemon"
cat > "$TMP/mini-snmpd" <<'EOS'
#!/bin/sh
exit 0
EOS
chmod +x "$TMP/mini-snmpd"
export PATH="$TMP/bin:$PATH" SNMP_TEST_LOG="$TMP/calls" SNMPD_DAEMON="$TMP/mini-snmpd"
export CONFIGD_SNMPD_ENV="$TMP/snmpd.env" SNMPD_PIDFILE="$TMP/snmpd.pid"

cat > "$CONFIGD_SNMPD_ENV" <<'EOS'
SNMP_ENABLED='0'
SNMP_COMMUNITY='private'
SNMP_LOCATION='lab'
SNMP_CONTACT='ops'
SNMP_BIND_DEVICE='linux_mgmt'
EOS
"$SCRIPT" start >/dev/null
[ ! -e "$SNMP_TEST_LOG" ] || { echo "disabled sentinel still started SNMP" >&2; exit 1; }

cat > "$CONFIGD_SNMPD_ENV" <<'EOS'
SNMP_ENABLED='1'
SNMP_COMMUNITY='private'
SNMP_LOCATION='lab'
SNMP_CONTACT='ops'
SNMP_BIND_DEVICE='linux_mgmt'
EOS
"$SCRIPT" start >/dev/null
grep -q -- "-I linux_mgmt" "$SNMP_TEST_LOG"
grep -q -- "-c private" "$SNMP_TEST_LOG"

: > "$SNMP_TEST_LOG"
cat > "$CONFIGD_SNMPD_ENV" <<'EOS'
SNMP_ENABLED='1'
SNMP_COMMUNITY='private'
SNMP_LOCATION='lab'
SNMP_CONTACT='ops'
SNMP_BIND_DEVICE=''
EOS
"$SCRIPT" start >/dev/null
if grep -q -- "-I " "$SNMP_TEST_LOG"; then
  echo "all-interface policy unexpectedly emitted -I" >&2
  exit 1
fi

echo "snmpd init policy tests passed"
