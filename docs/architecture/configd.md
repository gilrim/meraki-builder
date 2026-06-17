# Configd

Configd is the always-running privileged management core.

Responsibilities:

- persistent configuration and schema validation
- hardware/capability discovery
- Click and PoE application
- management-network observation and reconfiguration
- roles and capabilities
- user, service, time, SSH, and compatibility operations
- firmware staging and event coordination
- local Unix-socket API
- optional WebSocket API

The console communicates through `/run/postmerkos/configd.sock`. One-shot CLI commands remain for recovery and automation. Web builds add libwebsockets and uhttpd without changing the configuration core.

See the package [README](../../buildroot/packages/configd/README.md) and [protocol](../../buildroot/packages/configd/docs/PROTOCOL.md) for request details.
