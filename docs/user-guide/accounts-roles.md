# Accounts, roles, sessions, and SSH keys

postmerkOS uses Linux accounts and group-derived roles.

| Action | Administrator | Operator | Viewer |
|---|---:|---:|---:|
| Read status and logs | Yes | Yes | Yes |
| Read desired configuration | Yes | Yes | Yes |
| Configure ports and switching | Yes | Yes | No |
| Create configuration backups | Yes | Yes | No |
| Reboot | Yes | Yes | No |
| Change management addressing | Yes | No | No |
| Firmware update or full restore | Yes | No | No |
| Manage users, SSH keys, system identity, time, telemetry, and services | Yes | No | No |
| Run the bounded/unrestricted terminal operation | Yes | No | No |
| Power off or factory reset | Yes | No | No |

Groups are `postmerkos-admin`, `postmerkos-operator`, and `postmerkos-viewer`. Root always has administrator privileges and is protected from deletion and demotion.

Both console and browser hide unavailable actions, while configd validates every request against a named capability. Browser tokens and live connections are revalidated before privileged operations. Deleting an account or changing its password or role revokes affected sessions immediately.

## SSH keys

Administrators can list, add, and remove public keys and view their fingerprints. Accepted keys must have a supported type, strict Base64 decoding, and a valid matching SSH binary wire structure. Malformed, truncated, mismatched, or trailing-data keys are rejected.

The saved SSH-key configuration and root `authorized_keys` file are one transaction. A write or render failure leaves the previous configuration and active key file in place. Key-only mode cannot be enabled without at least one valid authorized key.

Private keys and password hashes are excluded from status, configuration backups, and browser responses.

See [Security, roles, sessions, and SSH keys](../architecture/security-and-sessions.md) for the implementation contract.
