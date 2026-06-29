# Web interface

The web interface is optional at build time and uses configd as its only validation, authorization, configuration, and status backend. It connects on TCP port 4001 with WebSocket subprotocol `configd-ws`, validates protocol version with `hello`, authenticates a local Linux account, and then subscribes to role-filtered configuration and status updates.

## Main navigation

- **Legend** — port-state and link-speed reference.
- **Update** — configuration backup/restore and firmware management.
- **Configuration** — ports, switching, network, accounts, SSH, time, services, terminal, telemetry, and System Information.
- **Logout** — revoke the browser session and return to login.

Selecting a front-panel port opens a focused editor. **All Ports** opens the complete table with filtering and port cloning. Changes remain local until Apply; Discard restores the last configuration broadcast by configd. Configd revalidates all requests even when the UI hides an action for the current role.

## Layout behavior

Configuration tabs use bounded Accounts-style card stacks and grow only as wide as the task requires. Terminal and All Ports receive wider task-specific limits. The All Ports floating header uses measured row height with overlap so content cannot show through between sticky sections.

The graphical panel is divided into twelve-port copper banks plus one grouped SFP bank. Flex wrapping moves the SFP bank first, then the highest-numbered copper bank, then preceding banks. When a bank no longer fits as a normal twelve-port face, it changes to a two-column stack rather than creating a horizontal scrollbar.

Mobile layouts use near-full-screen dialogs, touch-sized controls, compact/wrapping navigation, one-column forms and cards, stacked actions, usable terminal controls, and non-sticky port tables. The mobile layout changes interaction patterns rather than scaling the desktop layout down unchanged.

## System and time

System Information exposes the best-effort inventory described in [System Information](system-information.md). Missing optional data does not hide the remaining report.

The Time panel provides NTP policy, grouped common timezone presets, editable compact UTC/DST rules, synchronization status, and administrator-only manual local time in `HH:MM:SS - DD:MM:YYYY` format.

## Firmware workflow

Browser upload is tokenized and resumable at the validation/status level. The browser preserves the candidate filename and matching release manifest, starts asynchronous server-side verification, and polls status across reconnects or page reloads. A slow validation does not require the firmware to be uploaded again. Exact-model incompatibility remains blocked; an `untested` artifact requires the explicit acknowledgement defined by the updater.

See [Firmware updates](firmware-updates.md) for the complete lifecycle and safety contract.
