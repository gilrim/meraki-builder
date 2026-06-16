#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PKG=$(CDPATH= cd -- "$HERE/.." && pwd)
OUT=${TMPDIR:-/tmp}/configd-test-network
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" -I"$PKG/../postmerkos" -I"$PKG/../pd690xx" \
  $(pkg-config --cflags json-c) \
  -o "$OUT" "$HERE/test_network.c" "$PKG/network.c" "$PKG/result.c" \
  "$PKG/../postmerkos/libpostmerkos.c" $(pkg-config --libs json-c)
"$OUT"

CONSOLE_OUT=${TMPDIR:-/tmp}/configd-test-console
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" -I"$PKG/../postmerkos" -I"$PKG/../pd690xx" \
  $(pkg-config --cflags json-c) \
  -o "$CONSOLE_OUT" "$HERE/test_console_cli.c" "$PKG/console_cli.c" \
  $(pkg-config --libs json-c)
"$CONSOLE_OUT"
