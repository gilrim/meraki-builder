# SNMP and Prometheus telemetry

Telemetry is optional and disabled by default. It is configured in the main switch configuration under `telemetry` and participates in the same validate/apply/rollback transaction as network and switching changes.

## SNMP

The SNMP service supports:

- enable/disable;
- community string;
- location and contact text;
- management-interface-only binding, enabled by default.

SNMP starts only after configd has decoded and atomically published a valid, nonempty port-statistics snapshot. If the Click counter handler is missing, malformed, or unsupported, SNMP enablement fails instead of publishing invented or empty IF-MIB rows. This is a deliberate fail-closed boundary while proprietary counter layouts remain exact-platform evidence dependent.

## Prometheus

The built-in Prometheus endpoint supports:

- enable/disable;
- configurable TCP port from 1 through 65535, excluding reserved service ports used by the image;
- management-interface-only binding, enabled by default.

Metrics clients are nonblocking, bounded, and processed incrementally in configd’s event loop. Slow or incomplete HTTP clients cannot indefinitely block WebSocket, local-socket, network, or firmware-management work.

## Port snapshots

Configd polls the validated Click counter source, enriches rows with current port identity/link information, and atomically writes `/run/postmerkos/portstats.v1`. Row mapping uses the reported physical port number rather than assuming contiguous output order. Snapshot TTL is derived from the polling interval.

## Security

Management-only binding restricts the listener to the configured management interface/device. Disabling that option exposes the service on additional Linux interfaces and should be combined with an upstream firewall or isolated management network. SNMP community strings are secrets and are excluded from general status output.

## Evidence collection

Hardware validation of a new counter layout uses [`tools/research/capture-portstats-evidence.sh`](../../tools/research/capture-portstats-evidence.sh) and the [telemetry counter validation guide](../research/telemetry-counter-validation.md). Research captures do not grant production support until the exact-model decoder is reviewed and promoted.
