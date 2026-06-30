# Management console

Run `pmc` from SSH or serial to open the hierarchical postmerkOS console. The serial getty can launch it directly according to authentication policy.

```text
1) Status                         5) Users & Access
2) Ports & Switching              6) Services, Time & Monitoring
3) Firmware Update                7) Power Control
4) Backup & Restore               8) Shell
```

Available choices follow the authenticated role. Administrators receive full management, operators receive switching/reboot capability, and viewers receive read-only status.

## Parity with the browser

Switch-level options added to the browser are also exposed through PMC/configd rather than direct file edits. Current parity includes:

- detailed System Information and service/monitoring state;
- individual port editing and port cloning;
- accounts, roles, passwords, and SSH public keys;
- firmware sources, repository policy, repository checks, validation, installation, and history;
- hostname, default/unique name actions, `.local` discovery, and mDNS service policy;
- common timezone catalogue, manual local time, NTP, and advanced offset/DST policy;
- SNMP and Prometheus configuration.

Browser-only presentation preferences—responsive mode, front-page All Ports, expanded port names, and Remember me—are deliberately excluded from PMC. The machine-readable management feature manifest and host test enforce this distinction.

Port status is paged in groups of twelve copper ports followed by the uplink/SFP page. `Enter` advances, `p` moves back, `c` opens a port, and `b` returns. Routine kernel/DHCP/Click/chrony chatter is suppressed while PMC owns serial display; critical firmware/reset messages remain visible.
