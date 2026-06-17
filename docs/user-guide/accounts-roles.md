# Accounts and roles

PostmerkOS uses ordinary Linux accounts and group-derived roles.

| Action | Administrator | Operator | Viewer |
|---|---:|---:|---:|
| Read status and logs | Yes | Yes | Yes |
| Configure switching and PoE | Yes | Yes | No |
| Reboot | Yes | Yes | No |
| Change management addressing | Yes | No | No |
| Firmware update or full restore | Yes | No | No |
| Manage users, SSH, and services | Yes | No | No |
| Unrestricted terminal | Yes | No | No |
| Power off/factory reset | Yes | No | No |

Groups are `postmerkos-admin`, `postmerkos-operator`, and `postmerkos-viewer`. Root always has administrator privileges and is the only account protected from deletion or demotion.

Both interfaces hide unavailable actions, while configd validates every request against the authenticated capabilities.
