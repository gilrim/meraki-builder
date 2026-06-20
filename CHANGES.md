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
- Add regression tests for CRLF-expanded ACKs, missing bytes, conservative baud
  ordering, diagnostic refinement, safe window selection, guarded multi-frame
  output, terminal-status preservation, and branch-selection policy.

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
