# Compatibility testing

Minimum report:

- firmware version and Git revision
- exact model
- successful boot and detected model/family
- correct copper/uplink count and mapping
- management DHCP/static behavior
- serial and SSH console
- optional web UI
- VLAN/STP/port configuration
- PoE detection/power on PoE models
- reboot and persistent configuration
- updater behavior only if deliberately tested
- observed errors and recovery method

The generated report masks sensitive network identity and excludes passwords, hashes, private keys, and full configuration. A model can be confirmed for runtime while updater or LED capabilities remain untested.
