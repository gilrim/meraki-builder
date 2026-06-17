# postmerkOS console

## Purpose

`postmerkos-console` is the primary interactive management interface. It runs over hardware serial and SSH, uses configd’s local Unix socket, and exposes only actions permitted by the authenticated administrator, operator, or viewer role.

## Session behavior

The hardware serial path uses getty and `postmerkos-serial-login`. JFFS2 security policy selects direct console, root-only login, or normal role-aware user login. SSH users can run `pmc` to open the same interface.

Aliases:

```text
pmc -> postmerkos-console
postmerkos-cli -> postmerkos-console
```

Non-interactive SSH, SCP/SFTP, and automation are not intercepted. Entering the raw shell prints instructions to run `pmc` to return.

## Main menu

```text
1) Status                         5) User Management
2) Port Configuration             6) Service Management
3) Firmware Update                7) Power Control
4) Backup & Restore               8) Shell
```

Choices are filtered by configd capabilities. Operators can manage switching and reboot; viewers receive read-only status.

## Port handling

Status is paged in twelve-port copper groups followed by detected uplink/SFP ports. Port configuration includes VLAN, STP, speed, flow control, EEE, storm control, and capability-dependent PoE. Boot-pruned PoE ports can be re-enabled for the current boot or changed to Normal policy.

## Updates and backups

Before firmware installation, the console recommends a TFTP configuration backup, offers a copyable JSON display with SHA-256, allows an explicit skip, or cancels. Update history is read from the persistent post-reboot record rather than a transient process check.

## Console output

While the menu owns hardware serial, routine kernel and service chatter is reduced and restored on exit. Critical factory-reset and firmware-update messages remain visible.

## Build integration

The package is included in every switch build. The optional browser interface is independent and shares the same configd backend.
