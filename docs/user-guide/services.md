# Service management

Persistent service policy is stored in `/config/postmerkos/services.json`. Administrators can view observed state, enable/disable, set autostart, and start/stop/reconfigure supported management services.

Managed services are:

- **SSH** — Dropbear enablement, startup, port, and password/key policy;
- **Web** — optional static web frontend;
- **Chrony** — NTP synchronization, additionally gated by time policy;
- **mDNS** — management-interface-only `.local` host discovery through Avahi;
- **SNMP/Prometheus** — configured through validated telemetry policy rather than the generic service file.

Buildroot's Avahi init script provides `reload` rather than `restart`; configd maps the generic reconfigure operation to reload when running and start when stopped. A disabled mDNS policy is not started by hostname changes.

Critical forwarding, management networking, configd core, and Click initialization cannot be permanently disabled through normal management interfaces. Configd readiness performs a real local-session request and WebSocket protocol-2 `hello`, not merely a TCP-listen test. Use `postmerkosctl management-health` and the PMC Status/Logs views for diagnostics.
