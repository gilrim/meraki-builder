# Web interface

The web UI is optional at build time and uses the same configd validation and permission backend as the console.

The top bar contains:

- **Legend** — port-state and speed reference
- **Update** — configuration backup/restore and firmware operations
- **Configuration** — network, ports, STP, LACP/multicast, accounts, SSH, time, services, terminal, and system information
- **Logout**

The graphical front panel is the normal view. Selecting a port opens a focused editor; **All Ports** opens an independently scrolling table. Port cloning can copy selected configuration categories to one or more compatible targets after a dry-run capability check.

Changes that require application use the persistent Apply/Discard prompt. Authorization is enforced server-side even when controls are hidden for a lower role.
