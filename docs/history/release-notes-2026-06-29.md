> **Historical record:** This document describes a completed incident, migration, or release snapshot. It is not the current operating guide. Follow the active documentation linked from [`docs/README.md`](../README.md).

# Responsive management UI, system inventory, and local clock control

## meraki-builder

- Expand configd status with bounded, best-effort system inventory: immutable board identity, serial/product/base-MAC data, kernel and CPU details, uptime/load/process counts, memory usage, mounted filesystem capacity, overlay/config/tmp free space, and MTD partition layout.
- Keep status collection resilient: an unavailable procfs field, board-data key, filesystem, or flash table does not prevent the rest of the status payload from being returned.
- Extend the shared time operation so both the local management socket and authenticated WebSocket clients can set either an epoch value or an exact local time in `HH:MM:SS - DD:MM:YYYY` form.
- Persist a selected common timezone identifier alongside the existing compact UTC-offset and recurring-DST policy without adding a runtime tzdata dependency.
- Validate calendar boundaries, reject nonexistent spring-forward wall times, require the `services.manage` capability for browser clock changes, and retain a dry-run mode for host tests.
- Add host, sanitizer, static-analysis, UI/backend contract, and development-mock coverage for the new system and time operations.

## postmerkos-ui

- Expand System Information into bounded identity, software, processor/kernel, runtime, temperature, service-health, memory, storage, MTD, hardware-control, and compatibility sections.
- Normalize configuration tabs around the Accounts-style card stack and impose task-appropriate maximum widths instead of stretching every dialog to the viewport.
- Add a grouped common-timezone selector and exact manual local-time control while retaining advanced offset/DST editing.
- Render the graphical front panel as independently wrapping twelve-port banks, keep all SFP ports together, and switch each bank to a two-column layout on narrow screens.
- Remove the sticky All Ports header seam and provide mobile-specific dialog, navigation, form, table, terminal, filter, and action layouts.

---

# MS42P verified reset button and chassis upgrade indication

## meraki-builder

- Promote the OpenVTSS hardware evidence for MS42P primary GPIO13 active-low reset input and GPIO22/GPIO23 chassis status output into capability schema v3.
- Read reset state from the verified Jaguar1 `DEVCPU_GCB.GPIO_IN` physical register through a read-only mapping instead of assuming Linux GPIO numbering.
- Build and start `postmerkos-buttond` only for an exact, hardware-verified destructive profile; add debounce, release-to-arm, continuous-hold, live status, firmware-lock interlock, and cancellable reset countdown behavior.
- Fail closed when the persistent reset policy is missing, malformed, incomplete, or names any action other than explicit `factory-reset`.
- Make factory reset share the firmware lock so reset and upgrade erase operations cannot overlap.
- Clean up reset indication and publish an error if the button daemon cannot hand off to the factory-reset executable.
- Correct the Click status handlers to plain `0`/`1` writes, enforce owner-gated state changes, capture/restore normal state, and prefer the chassis indicator over port LEDs.
- Serialize LED ownership changes with stale-lock recovery so concurrent reset and firmware callbacks preserve the configured priority and cannot leave competing animators.
- Add accelerating green/orange firmware progress, triple-orange failure/rollback, solid-green verified completion, and orange reset-countdown patterns in both userspace and the RAM-resident flasher.
- Add host regression coverage for immutable identity gating, direct-MMIO active-low decoding, held-at-boot protection, release-to-arm, hold completion, LED protocol, and reset handoff.

## postmerkos-ui

- Show reset-button state, GPIO/polarity, destructive safety status, hold duration, and current LED owner in System Information.
- Document the exact chassis LED behavior shown after the management interface disconnects for flashing.

---

# Revision 3: deterministic configd rebuild and pipefail-safe validation

## meraki-builder

- Fix a false WebSocket-disabled result in `validate-image.sh`. The scripts run
  with `set -o pipefail`; `strings ... | grep -q` could find the valid marker,
  exit early, cause `strings` to receive SIGPIPE, and make the successful probe
  return status 141. Binary feature and dependency probes now consume all input.
- Rebuild the fixed-version local `configd` package on every rootfs build after
  package synchronization. The build performs `configd-dirclean`, removes stale
  installed tools, runs the explicit Buildroot `configd` target, and validates
  the freshly installed binary before filesystem finalization.
- Keep the source/config fingerprint as diagnostic state, but no longer trust it
  as the sole cache-invalidation mechanism after a failed image validation.
- Report the actual discovered WebSocket feature marker when final validation
  fails, instead of always describing the binary as disabled.
- Add a regression test with enough trailing binary strings to reproduce the
  former SIGPIPE/141 validator failure.

## postmerkos-ui

No source changes were required.

---

# WebSocket build-cache, local socket, and service-policy corrections

## meraki-builder

- Preserve configd's required feature macros when Buildroot supplies `CPPFLAGS`
  as a command-line variable. The package Makefile now uses `override CPPFLAGS +=`
  for the POSIX/default-source flags and `CONFIGD_ENABLE_WEBSOCKET`, preventing a
  binary that links `websocket.c`/libwebsockets but reports and behaves as
  WebSocket-disabled.
- Extend the Buildroot contract test to execute a dry-run configd build with
  command-line `CPPFLAGS` and verify that all required package macros survive.
- Detect and clean reused Buildroot output when switching between base and web
  images, including one-time cleanup of output created before UI-mode tracking.
- Fingerprint the synchronized configd package and its WebSocket feature choice;
  invalidate `configd-0.2` automatically when either changes.
- Forward `CLEAN_BUILDROOT` through top-level Distrobox execution.
- Mark web images explicitly and fail configd startup when a web image contains a
  WebSocket-disabled daemon.
- Validate the final configd binary for `websocket: enabled` and a
  `libwebsockets` dependency before publishing a web artifact.
- Replace split local-socket writes and one-shot reads with shared bounded,
  newline-framed I/O; suppress SIGPIPE in configd and `postmerkosctl`.
- Make service start/stop reconciliation idempotent to avoid duplicate chrony,
  uhttpd, and Dropbear start failures during the late service-policy pass.
- Add host tests for fragmented socket traffic, closed-peer writes, oversized
  frames, WebSocket-required init failure, and build-cache contracts.

## postmerkos-ui

No source changes were required. The current UI already requests the `configd-ws`
subprotocol and protocol-2 `hello` expected by the backend.

---

# UART recovery ACK, negotiation, and receive-window corrections

## meraki-builder

- Keep `UI_REF=ms42p-dev` as the postmerkos-ui default and correct all active
  documentation and source-contract tests that incorrectly claimed `main`.
- Replace the default broad UART divisor search with one descending pass over
  921600, 460800, and 230400 baud, stopping at the first bidirectional pass.
- Retain the former broad scan and midpoint refinement behind
  `--diagnostic-baud-scan`.
- Resynchronize compact ACK parsing by scanning for the binary ACK magic instead
  of repeatedly consuming fixed 28-byte blocks after corruption.
- Wait for a failed feature test to reach a terminal target state before another
  command is sent, and fail the session when fundamental raw ACK qualification
  cannot be cleanly recovered.
- Default framed transport to a one-frame production window because the VCore-III
  recovery UART has no RTS/CTS flow control and cannot safely absorb a continuous
  multi-frame burst while CRC/decode/copy work is performed between frames.
- Retain larger windows behind `--diagnostic-window-scan`, with `tcdrain()` and a
  conservative inter-frame idle guard.
- Preserve textual `FEATURE-FAIL` output consumed during compact-ACK scanning so
  command-level recovery can resynchronize cleanly.
- Return the host UART to 115200 immediately after `PMOSREC REBOOT NOW`, clear
  stale negotiated-rate bytes, and monitor several early loader/kernel markers.
- Classify serial input before echoing it: only complete ASCII protocol lines are
  written to the operator console, while deterministic baud-test and compact-ACK
  binary bytes stay silent. This prevents random XOFF and terminal escape bytes
  from freezing or corrupting live progress output while the transfer continues.
- Add regression tests for CRLF-expanded ACKs, missing bytes, conservative baud
  ordering, diagnostic refinement, safe window selection, guarded multi-frame
  output, reboot baud handoff, binary-safe console output, terminal-status
  preservation, and branch-selection policy.

## meraki-redboot

- Separate human-readable CRLF console output from byte-transparent UART output.
- Send compact ACK records and target-to-host deterministic test streams through
  the raw writer so binary `0x0a` bytes are never expanded.
- Correct `ERROR_PPM` calculation without 32-bit multiplication saturation.
- Drain the remainder of a failed feature-test burst before returning to the
  command parser, preventing binary frame data from becoming `UNKNOWN-COMMAND`
  floods.
- Make malformed headers retryable for the safe one-frame window, bound target
  frame timeout below the host ACK deadline, and report structured frame error,
  expected sequence, UART status, and drained-byte data.
- Add source-contract tests for binary output and framed-stream recovery paths.

## postmerkos-ui

No source changes. `meraki-builder` continues to consume `ms42p-dev` by default.
