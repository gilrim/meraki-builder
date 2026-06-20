#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
IDENTITY="$ROOT/overlay/usr/sbin/postmerkos-board-identity"
PROFILE="$ROOT/overlay/usr/sbin/postmerkos-board-profile"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cat > "$TMP/board_data" <<'BD'
#!/bin/sh
case "$1" in
 product_number) echo 600-32010 ;;
 model_exact) echo MS42P ;;
 serial) echo Q2XX-ABCD-1234 ;;
 mac) echo 00:18:0a:12:34:56 ;;
 *) exit 1 ;;
esac
BD
chmod +x "$TMP/board_data"
POSTMERKOS_RUN_DIR="$TMP/run" POSTMERKOS_BOARDINFO="$TMP/run/boardinfo" \
POSTMERKOS_BOARD_DATA="$TMP/board_data" POSTMERKOS_BOARD_PROFILE="$PROFILE" \
  "$IDENTITY" >/dev/null
[ "$(stat -c %a "$TMP/run/boardinfo")" = 444 ]
grep -q '^MODEL=MS42P$' "$TMP/run/boardinfo"
grep -q '^MODULE_FAMILY=jaguar_dual$' "$TMP/run/boardinfo"
grep -q '^LOGICAL_PORTS=52$' "$TMP/run/boardinfo"
grep -q '^IDENTITY_EXACT=1$' "$TMP/run/boardinfo"
# Unknown identity must remove any stale record and fail closed.
cat > "$TMP/board_data" <<'BD'
#!/bin/sh
exit 1
BD
chmod +x "$TMP/board_data"
if POSTMERKOS_RUN_DIR="$TMP/run" POSTMERKOS_BOARDINFO="$TMP/run/boardinfo" \
   POSTMERKOS_BOARD_DATA="$TMP/board_data" POSTMERKOS_BOARD_PROFILE="$PROFILE" \
   "$IDENTITY" >/dev/null 2>&1; then
  echo 'identity unexpectedly accepted missing EEPROM data' >&2; exit 1
fi
[ ! -e "$TMP/run/boardinfo" ]
# Production profiles must not enable candidate raw PoE GPIO mappings.
for model in MS22 MS22P MS220-8 MS220-8P MS220-24 MS220-24P MS220-48 MS220-48P MS220-48LP MS220-48FP MS320-24 MS320-24P MS320-48 MS320-48P MS320-48LP MS320-48FP MS42 MS42P; do
  ! "$PROFILE" "$model" | grep -q '^POE_GPIO_VERIFIED=1$'
done
printf '%s\n' 'board identity tests passed'
