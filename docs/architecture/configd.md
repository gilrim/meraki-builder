# Configd

Configd is the always-running privileged management core. It owns persistent desired switch state, exact-model capability discovery, Click and PoE application, management networking, telemetry, accounts and roles, SSH keys, service and time policy, system inventory, firmware staging, the local Unix socket, and the optional WebSocket API.

## Transaction contract

A configuration delta is deep-merged into a copy of the current configuration, validated as a complete document, and applied to required runtime subsystems before persistence. Click, PoE, network, and telemetry failures reject the transaction. Configd attempts to restore the previous runtime state and leaves the previous primary/backup files authoritative. Only a validated, successfully applied state is saved atomically and promoted to the known-good backup.

Complete replacements and external edits use the same validation/apply/rollback contract. Results distinguish applied operations, warnings, pending work, unsupported requests, hard failures, and runtime degradation.

## Interfaces

- `/run/postmerkos/configd.sock` — bounded newline-framed JSON for the console and `postmerkosctl`, authenticated with `SO_PEERCRED`.
- TCP 4001 / `configd-ws` — optional authenticated WebSocket transport in web builds.
- One-shot CLI operations — recovery and automation paths that call the same core handlers where applicable.

WebSocket startup performs transport negotiation, protocol `hello`, account authentication, and role/capability resolution. Privileged requests revalidate token expiry, account existence, and current role. Account, password, and role changes revoke affected sessions.

See the package [README](../../buildroot/packages/configd/README.md), [protocol reference](../../buildroot/packages/configd/docs/PROTOCOL.md), and [security architecture](security-and-sessions.md).

## Event-loop safety

Long-running firmware candidate validation runs as an asynchronous child job. Prometheus clients are nonblocking, bounded, and incrementally serviced. Authentication throttling does not sleep in the event loop. These boundaries keep local management, WebSocket requests, network polling, and status broadcasts responsive.

## Status collection

Status is observed, best-effort state. It includes ports, management networking, compatibility, services, time, telemetry health, temperatures, hardware controls, and the read-only [System Information](../user-guide/system-information.md) inventory. Missing sensors, procfs fields, filesystems, or Click handlers produce omissions/errors without suppressing unrelated status.

Known models use immutable `/run/postmerkos/boardinfo`; editable files and stale port-count data cannot override an exact profile.

## Runtime supervision

`S15configd` starts configd through a bounded supervisor. Readiness requires the Unix socket, a valid root local-session request, an actual WebSocket upgrade in web images, `configd-ws` selection, and a protocol-2 `hello` response. Exit details are recorded in `/run/postmerkos/configd.exit`, daemon output in `/run/postmerkos/configd.log`, and WebSocket events in `/run/postmerkos/websocket.log`.

Use `postmerkosctl management-health` for the complete readiness probe. Boot network initialization emits concise serial PASS/WARN/FAIL lines and retains the full result at `/run/postmerkos/network-bootstrap.json`.
