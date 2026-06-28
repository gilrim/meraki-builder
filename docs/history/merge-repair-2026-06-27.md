# Third-party merge repair — 2026-06-27

This repair set resolves the production build regressions and management-plane
issues found while vetting the merged `ms42p-dev` trees.

Highlights:

- restored configd and UI production builds;
- made telemetry configuration transactional and rollback-safe;
- primed a validated port snapshot before SNMP startup;
- replaced blocking Prometheus handling with bounded nonblocking clients;
- scoped telemetry listeners to the management interface by default;
- made firmware validation asynchronous and recoverable after reconnect/reload;
- revalidated live WebSocket sessions and revoked them on account changes;
- replaced blocking, per-connection authentication delays with global monotonic
  backoff buckets;
- made SSH-key parsing strict and add/remove persistence rollback-safe;
- corrected SNMP enable detection, sparse port mapping, and PoE allocation error
  handling; and
- added host and CI gates for configd, the UI, and the existing image/tooling
  contracts.

The proprietary switch-counter protobuf source remains hardware-unverified. The
runtime now fails closed when SNMP requires it, and
`tools/research/capture-portstats-evidence.sh` provides the evidence collection
procedure needed to close that gap.
