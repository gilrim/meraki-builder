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

## SFP/SFP+ evidence runs

A compatibility claim for an optical module requires one cage per run and must record the exact module label, exact peer module/device, fibre type, and tested speed. Capture these stages separately: insertion without fibre, link attempt, fibre-only disconnection, and module removal. Record EEPROM identity, RxLOS, TxFault, TxDisable, SERDES/PCS status, Click link state, and front-panel LED behavior.

An insertion event or Click `up` value alone proves neither optical-control correctness nor module-model compatibility. If a polarity retry is performed, record it as an explicit controlled step rather than folding it into the initial result.
