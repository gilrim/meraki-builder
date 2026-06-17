# Management console

Run `pmc` from SSH or serial to open the hierarchical postmerkOS console. The serial getty can launch it directly according to the configured authentication policy.

The main menu is numbered top-to-bottom before moving to the second column:

```text
1) Status                         5) User Management
2) Port Configuration             6) Service Management
3) Firmware Update                7) Power Control
4) Backup & Restore               8) Shell
```

Available choices follow the authenticated role. Administrators receive full management, operators can configure switching and reboot, and viewers receive read-only status.

Port status is paged in groups of twelve copper ports followed by the uplink/SFP page. `Enter` advances, `p` moves back, `c` opens a port, and `b` returns.

Routine kernel, DHCP, Click, and chrony chatter is suppressed while the menu owns the serial display. Critical firmware/reset messages remain visible. The raw shell prints a reminder that `pmc` returns to the console.
