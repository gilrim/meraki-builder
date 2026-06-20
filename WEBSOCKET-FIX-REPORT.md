# WebSocket and management-path fix report

Date: 2026-06-20

## Hardware evidence

The affected firmware reported:

```text
core: enabled
unix-socket: enabled
websocket: disabled
```

No listener existed on TCP port 4001 and `/run/postmerkos/websocket.log` was absent. This confirms that the web image contained a WebSocket-disabled `configd`; it was not a runtime WebSocket handshake failure. The repeated local process/socket health passes also do not support a configd reboot loop.

## Corrected defects

1. Buildroot now detects changes between base and web image modes and cleans stale output when necessary.
2. The synchronized local `configd` package and its feature selection are fingerprinted. A changed fingerprint triggers `configd-dirclean` so fixed Buildroot package stamps cannot preserve an older daemon.
3. `CLEAN_BUILDROOT` is forwarded through top-level and direct Distrobox transitions.
4. Web images carry `/etc/postmerkos/features/web-ui`. Their configd init script fails closed when the daemon reports WebSocket support disabled.
5. Final web-image validation requires both `websocket: enabled` and a `libwebsockets` dynamic dependency in `/bin/configd`.
6. Configd and `postmerkosctl` now share bounded newline-framed socket I/O. Requests are sent atomically, responses are read to a complete frame, all writes are checked, and disconnected peers cannot terminate either process through SIGPIPE.
7. Service policy reconciliation avoids duplicate start/stop operations for uhttpd, chronyd, and Dropbear.
8. Regression tests cover fragmented frames, oversized frames, closed peers, fail-closed WebSocket startup, service idempotence, and Buildroot cache contracts.

## Repository scope

- `meraki-builder`: changed.
- `postmerkos-ui`: reviewed; no change required. The UI already uses subprotocol `configd-ws` and protocol-2 `hello`, matching the current backend.
- `meraki-redboot`: not involved in this fault and unchanged.

## Validation completed

- Full configd host test suite passed.
- Configd built successfully with WebSocket disabled under `-Wall -Wextra -Werror`.
- Socket framing and SIGPIPE regression tests passed.
- Configd supervisor and WebSocket-required init tests passed.
- Build cache/image contract tests passed.
- UI/configd contract passed for all 28 UI request methods.
- Board identity, PoE initialization, and postmerkos-hardware host tests passed.
- Documentation link validation passed.

A complete Buildroot web firmware build was not performed in this review environment. The corrected final-image validator will reject a web artifact unless its actual target `configd` is WebSocket-enabled and linked to libwebsockets.

## First rebuild

Use an explicit clean rebuild for the first corrected image:

```sh
CLEAN_BUILDROOT=1 make web
```

The corrected scripts preserve `CLEAN_BUILDROOT=1` when entering Ubuntu Distrobox.

## Post-flash verification

```sh
/bin/configd --features
grep -i ':0FA1' /proc/net/tcp /proc/net/tcp6
postmerkosctl management-health
cat /run/postmerkos/websocket.log
```

Expected feature result:

```text
core: enabled
unix-socket: enabled
websocket: enabled
websocket-port: 4001
websocket-protocol: configd-ws
```
