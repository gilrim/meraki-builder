# validation module

`validation.c` validates the complete post-merge configuration. Unknown keys are rejected to catch spelling mistakes and unsupported operations.

## Key constraints

- IPv4 mode: `dhcp` or `static`; CIDR notation is mandatory.
- MTU: 576–9216.
- VLAN IDs: 1–4094; allowed lists use comma-separated IDs and ranges.
- Port numbers must exist on detected hardware.
- PoE is accepted only on detected PoE-capable ports.
- PoE schema is exactly `enabled: boolean` and `mode: "af" | "at"`.
- Speed values are limited to modes supported by the Click translation table.

Validation errors are intended for both WebSocket and CLI clients and should describe a stable JSON path.
