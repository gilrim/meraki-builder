# Configuration and persistent state

Persistent policy files are initialized from compact read-only defaults. Desired switch configuration is stored at `/etc/switch.json` on the JFFS2-backed overlay; the remaining policy files are under `/config/postmerkos`:

- `/etc/switch.json` — ports, VLAN, STP, LACP, multicast, and management network
- `services.json` — SSH, web, chrony, and optional-service policy
- `security.json` — serial authentication, reset input, and related security policy
- `time.json` — UTC offset, recurring DST rules, and NTP servers
- `firmware-repositories.json` — repository/channel definitions
- `update-history/` — bounded update records and logs

Configd applies typed path updates, validates complete replacements, and emits structured results. Passwords and private keys are not part of configuration backup JSON.
