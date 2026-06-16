#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PKG=$(CDPATH= cd -- "$HERE/.." && pwd)
OUT=${TMPDIR:-/tmp}/configd-test-network
OPS_OUT=${TMPDIR:-/tmp}/configd-test-system-ops
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" -I"$PKG/../postmerkos" -I"$PKG/../pd690xx" \
  $(pkg-config --cflags json-c) \
  -o "$OUT" "$HERE/test_network.c" "$PKG/network.c" "$PKG/result.c" \
  "$PKG/../postmerkos/libpostmerkos.c" $(pkg-config --libs json-c)
"$OUT"

cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" $(pkg-config --cflags json-c) \
  -o "$OPS_OUT" "$HERE/test_system_ops.c" "$PKG/system_ops.c" \
  $(pkg-config --libs json-c)
"$OPS_OUT"
