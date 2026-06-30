# Web interface

The optional browser interface uses configd as its validation, authorization, configuration, and status backend. It connects on TCP port 4001 with WebSocket subprotocol `configd-ws`, validates protocol version with `hello`, authenticates a local Linux account, and subscribes to role-filtered status and configuration updates.

## Login

The login page uses one bounded authentication card containing credentials, the **Remember me on this device** session-token preference, Sign In, and a bounded connection-status panel. Passwords are never persisted. Routine transport/protocol state uses a polite live region; blocking errors use alert semantics, and long diagnostics are placed under **Details**.

A remembered token is stored in browser local storage; a non-remembered token is stored only for the current browser session. Logout revokes the active server session and removes both forms of local token storage.

## Header and navigation

The top display is composed of eight logical cards: Branding/Firmware, Device, Address, Time, Temperatures, Session, Tools, and Account/Logout. Desktop mode uses one eight-column row when space permits and wraps by complete cards. Tablet mode uses four columns. Phone mode uses one centered vertical column with enlarged text, icons, and touch targets.

- **Legend** — port-state and speed reference.
- **Update** — backup/restore, firmware repositories, upload, validation, and installation.
- **Configuration** — System, Display, Network/Identity, Ports, Switching, Accounts/SSH keys, Time, Services, Monitoring, and Terminal.
- **Logout** — revoke the browser session and return to login.

## Graphical ports

Copper ports are outlined and labeled in complete banks such as `1–12`. Uplinks are outlined and labeled from exact-model metadata as `SFP`, `SFP+`, or `SFP/SFP+`. On desktop and tablet, copper banks use a 2-row by 6-column switch-face layout. The MS42P 52-port presentation fits four copper banks and the four-port SFP+ bank in one row on a normal 1920-pixel desktop viewport.

Complete groups wrap before any member starts clipping, and each resulting row is centered. Phone mode divides each copper bank into six-port 2-row by 3-column groups and scales the graphics to fill the usable width. SFP ports remain atomic horizontal pairs; four phone SFP graphics occupy the width of three copper graphics while preserving aspect ratio. Only vertical tile borders are collapsed; horizontal row borders remain distinct.

## Focused port editing and All Ports

Selecting a graphical port opens a focused editor. Desktop/tablet mode uses the detailed grid. Phone mode uses vertical collapsible Overview, Basic Settings, PoE, VLAN, Spanning Tree, and Clients sections; each option is an individual full-width card rather than a horizontal table cell.

The front-page **All Ports** panel is disabled by default and is always suppressed on phone layouts without erasing the saved desktop preference. Configuration → Ports becomes a numbered selector on phones and opens the focused accordion editor. On larger layouts, All Ports is a rounded outlined panel with filters, row selection, **Clone selected port**, and an optional port-name column.

Port names are collapsed by default and can be kept expanded through the browser-local Display preference. Only the port-number column remains pinned during horizontal scrolling. The sticky desktop header uses measured row height and a one-pixel overlap to prevent content showing through between header bands.

## System, identity, time, and services

System Information exposes the bounded inventory described in [System Information](system-information.md), including reset status and persistent-overlay recovery warnings. Network includes the [System identity and local discovery](system-identity-and-discovery.md) controls. Time uses the target-provided common timezone catalogue, compact offset/DST policy, NTP state, and administrator-only local clock input in `HH:MM:SS - DD:MM:YYYY` format.

See [Firmware updates](firmware-updates.md) for the complete asynchronous candidate-validation and installation lifecycle.
