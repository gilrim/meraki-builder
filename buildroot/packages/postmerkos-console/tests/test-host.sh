#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CONSOLE=$(CDPATH= cd -- "$HERE/../files" && pwd)/postmerkos-console
TMP=$(mktemp -d)
OLD_NUM_PORTS=
[ -e /tmp/NUM_PORTS ] && OLD_NUM_PORTS=$(cat /tmp/NUM_PORTS 2>/dev/null || true)
cleanup() {
    rm -rf "$TMP"
    if [ -n "$OLD_NUM_PORTS" ]; then printf '%s\n' "$OLD_NUM_PORTS" >/tmp/NUM_PORTS; else rm -f /tmp/NUM_PORTS; fi
}
trap cleanup EXIT HUP INT TERM
printf '52\n' >/tmp/NUM_PORTS
cat >"$TMP/postmerkosctl" <<'MOCK'
#!/bin/sh
printf '%s\n' "$*" >>"$POSTMERKOS_TEST_LOG"
role=${POSTMERKOS_TEST_ROLE:-administrator}
case "$1" in
  role) echo "$role" ;;
  has)
    case "$role:$2" in
      administrator:*) exit 0 ;;
      operator:status.read|operator:config.read|operator:ports.write|operator:switching.write|operator:backup.create|operator:system.reboot) exit 0 ;;
      viewer:status.read|viewer:config.read|viewer:firmware.history.read) exit 0 ;;
      *) exit 1 ;;
    esac ;;
  ports) printf 'PORT LINK SPEED ADMIN POE VLAN MODE NAME\n1 down 0 enabled at 10 access test\n' ;;
  port)
    if [ "$2" = 49 ]; then printf 'Port 49\n  Administrative: enabled\n  PoE: not supported\n';
    else printf 'Port %s\n  Administrative: enabled\n  PoE: enabled (at)\n' "$2"; fi ;;
  get)
    case "$2" in
      ports.49.poe.*) exit 2 ;;
      ports.*.enabled|ports.*.poe.enabled) echo true ;;
      ports.*.poe.mode) echo at ;;
      ports.*.vlan.mode) echo access ;;
      ports.*.vlan.pvid) echo 1 ;;
      ports.*.vlan.allowed) echo '' ;;
      ports.*.vlan.untagged_vid) echo 0 ;;
      ports.*.vlan.ingress_filter) echo true ;;
      *) echo false ;;
    esac ;;
  set|set-string|apply-json) echo 'Configuration accepted' ;;
  summary) echo 'Model: MS42P' ;;
  config) echo '{}' ;;
  *) echo '{}' ;;
esac
MOCK
chmod +x "$TMP/postmerkosctl"
export POSTMERKOS_TEST_LOG="$TMP/calls"
export TERM=dumb
# Administrator: Port Configuration -> Copper -> first section -> port 1 -> toggle admin.
printf '2\n1\n1\n1\n1\n\nb\nb\nb\n0\n' |
  POSTMERKOSCTL="$TMP/postmerkosctl" "$CONSOLE" menu >"$TMP/copper.out"
grep -q 'Copper RJ45 port sections' "$TMP/copper.out"
grep -q 'Ports 1-12' "$TMP/copper.out"
grep -q -- 'set ports.1.enabled false' "$TMP/calls"
# SFP port has no PoE menu.
: >"$TMP/calls"
printf '2\n2\n49\nb\nb\nb\n0\n' |
  POSTMERKOSCTL="$TMP/postmerkosctl" "$CONSOLE" menu >"$TMP/sfp.out"
grep -q 'Ports 49-52' "$TMP/sfp.out"
grep -q 'Port 49 configuration' "$TMP/sfp.out"
! grep -q '9) Power over Ethernet' "$TMP/sfp.out"
# Viewer gets read-only port screen and no privileged top-level entries.
POSTMERKOS_TEST_ROLE=viewer printf '2\n1\n1\n1\nb\nb\nb\nb\n0\n' |
  POSTMERKOS_TEST_ROLE=viewer POSTMERKOSCTL="$TMP/postmerkosctl" "$CONSOLE" menu >"$TMP/viewer.out"
grep -q 'Read-only access' "$TMP/viewer.out"
! grep -q '3) Firmware Update' "$TMP/viewer.out"
! grep -q '8) Shell' "$TMP/viewer.out"
printf 'postmerkOS console role/navigation tests passed\n'
