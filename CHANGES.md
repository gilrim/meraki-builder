# UART recovery ACK and negotiation corrections

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
- Add regression tests for CRLF-expanded ACKs, missing bytes, conservative baud
  ordering, diagnostic refinement, and branch-selection policy.

## meraki-redboot

- Separate human-readable CRLF console output from byte-transparent UART output.
- Send compact ACK records and target-to-host deterministic test streams through
  the raw writer so binary `0x0a` bytes are never expanded.
- Correct `ERROR_PPM` calculation without 32-bit multiplication saturation.
- Add source-contract tests for all binary output paths.

## postmerkos-ui

No source changes. `meraki-builder` continues to consume `ms42p-dev` by default.
