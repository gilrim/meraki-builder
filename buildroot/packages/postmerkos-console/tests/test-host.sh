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
cat >"$TMP/configd" <<'MOCK'
#!/bin/sh
printf '%s\n' "$*" >>"$POSTMERKOS_TEST_LOG"
case "$1" in
  --show-ports) printf 'PORT LINK SPEED ADMIN POE VLAN MODE NAME\n1 down 0 enabled at 10 access test\n' ;;
  --show-port)
    if [ "$2" = 49 ]; then
      printf 'Port 49\n  Administrative: enabled\n  PoE: not supported\n'
    else
      printf 'Port %s\n  Administrative: enabled\n  PoE: enabled (at)\n' "$2"
    fi ;;
  --get-path)
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
  --set-path|--set-string|--apply-json) printf '%s\n' '{"type":"ack","data":{"message":"ok"}}' ;;
  *) printf '%s\n' '{}' ;;
esac
MOCK
chmod +x "$TMP/configd"
export POSTMERKOS_TEST_LOG="$TMP/calls"
export TERM=dumb
# Port Configuration -> Copper -> first 12-port section -> port 1 -> toggle admin.
printf '2\n1\n1\n1\n1\n\nb\nb\nb\n0\n' |
  CONFIGD="$TMP/configd" "$CONSOLE" menu >"$TMP/copper.out"
grep -q 'Copper RJ45 port sections' "$TMP/copper.out"
grep -q 'Ports 1-12' "$TMP/copper.out"
grep -q -- '--set-path ports.1.enabled false' "$TMP/calls"
# Port Configuration -> SFP/SFP+ -> port 49 -> PoE must not be offered.
: >"$TMP/calls"
printf '2\n2\n49\nb\nb\nb\n0\n' |
  CONFIGD="$TMP/configd" "$CONSOLE" menu >"$TMP/sfp.out"
grep -q 'Ports 49-52' "$TMP/sfp.out"
grep -q 'Port 49 configuration' "$TMP/sfp.out"
! grep -q '9) Power over Ethernet' "$TMP/sfp.out"
printf 'postmerkOS console navigation tests passed\n'
