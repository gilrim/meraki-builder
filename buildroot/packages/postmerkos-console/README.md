# postmerkOS console

`postmerkos-console` is the primary interactive management interface for
postmerkOS. It is installed in every build and starts automatically for root
sessions on SSH and the hardware TTL console. The optional browser interface is
not required.

The console is a lightweight BusyBox `sh` menu. It does not contain a second
switch-configuration implementation: reads and changes are delegated to the
one-shot `configd` commands, which use the same persistent JSON schema,
validation, Click handlers, PoE backend, network manager, and firmware updater
as the browser UI.

## Main menu

```text
1) Status                         2) Port Configuration
3) Firmware Update                4) Backup & Restore
5) User Management                6) Service Management
7) Power Control                  8) Shell
0) Log out
```

Port configuration is organized by hardware layout. MS42/MS42P presents four
12-port copper sections and a separate ports 49-52 SFP/SFP+ section. Smaller
models use their corresponding copper and uplink ranges. Each port exposes
administrative state, name, PHY speed, flow control, EEE, storm control, VLAN,
STP, and PoE where supported. Desired settings can be changed while link is
down.

Global menus expose management IPv4, global STP, LACP, multicast, firmware
update transports, plain JSON backup/restore, local users, services, reboot,
and the raw BusyBox shell.

## Login behavior

`/etc/profile.d/50-postmerkos-console.sh` starts the menu only for an
interactive root terminal. Non-interactive SSH commands, SCP/SFTP, and scripts
are not intercepted. Set `POSTMERKOS_NO_CONSOLE=1` to suppress automatic launch.
The raw-shell menu option sets `POSTMERKOS_RAW_SHELL=1`, preventing recursive
menu startup.

`postmerkos-cli` remains a compatibility symlink to `postmerkos-console` and
the script retains non-interactive subcommands for automation.

## Optional web compatibility

The console package selects only the `configd` configuration core. When
`INCLUDE_UI=1`, Buildroot additionally enables `BR2_PACKAGE_CONFIGD_WEBSOCKET`,
libwebsockets, uhttpd, the existing WebSocket authentication/firmware/terminal
protocol, and the browser assets. Console-only builds omit those components.
