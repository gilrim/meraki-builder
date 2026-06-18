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

ROLE_TMP=$(mktemp -d)
trap 'rm -rf "$ROLE_TMP"' EXIT HUP INT TERM
cat >"$ROLE_TMP/passwd" <<'EOF_PASSWD'
root:x:0:0:root:/root:/bin/sh
alice:x:1001:1001:Alice:/home/alice:/bin/sh
bob:x:1002:1002:Bob:/home/bob:/bin/sh
carol:x:1003:1003:Carol:/home/carol:/bin/sh
nobody:x:1004:1004:Nobody:/home/nobody:/bin/sh
EOF_PASSWD
cat >"$ROLE_TMP/group" <<'EOF_GROUP'
root:x:0:root
postmerkos-admin:x:2001:alice
postmerkos-operator:x:2002:bob
postmerkos-viewer:x:2003:carol
EOF_GROUP
ROLE_OUT=${TMPDIR:-/tmp}/configd-test-roles
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" $(pkg-config --cflags json-c) \
  -o "$ROLE_OUT" "$HERE/test_roles.c" "$PKG/roles.c" \
  $(pkg-config --libs json-c)
"$ROLE_OUT" "$ROLE_TMP/passwd" "$ROLE_TMP/group"

AUTH_OUT=${TMPDIR:-/tmp}/configd-test-auth
cc -std=gnu11 -Wall -Wextra -Werror \
  -I"$PKG" $(pkg-config --cflags json-c) \
  -o "$AUTH_OUT" "$HERE/test_auth.c" "$PKG/auth.c" "$PKG/roles.c" \
  $(pkg-config --libs json-c) -lcrypt
"$AUTH_OUT" "$ROLE_TMP/passwd" "$ROLE_TMP/group" "$ROLE_TMP/shadow"
