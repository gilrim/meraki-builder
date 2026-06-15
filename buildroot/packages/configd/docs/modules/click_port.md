# click_port module

`click_port.c` translates per-port JSON into Click handler commands and PD690xx operations.

## Fields

- `enabled`, `speed`, `flow_control`, `eee` → `set_port_phy_cfgs`.
- `storm_control` → `set_port_storm_control`.
- `vlan` → `set_vlan_allports_conf`.
- `stp` → `/click/stp/set_many_port_cfgs`.
- `poe.enabled`, `poe.mode` → PD690xx library.

Delta application uses the complete saved port object while selecting operations from changed keys. This prevents a one-field update from resetting sibling fields.

## Readback limits

PHY and VLAN defaults are reconstructed from available dump handlers. STP per-port defaults are currently desired defaults. Storm control is write-only and always comes from `/etc/switch.json`. PoE readback is attempted only on supported ports and only when controllers are available.
