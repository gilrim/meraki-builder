# Ports, VLANs, STP, and PoE

Per-port configuration includes administrative state, name, PHY speed, flow control, Energy Efficient Ethernet, storm control, VLAN behavior, spanning tree, and PoE where the exact model exposes a verified controller path.

## VLAN modes

- Access: one untagged access/PVID VLAN
- Trunk: tagged allowed list and PVID
- Hybrid: tagged allowed list plus native/untagged VLAN

Required Click/PoE programming occurs before configuration is persisted. A missing mandatory handler rejects the transaction and triggers rollback rather than saving an unapplied desired state.

## MS42P uplink status interpretation

MS42P ports 49 through 52 are the four SFP/SFP+ cages. Runtime testing confirms insertion/removal detection on every cage and confirms their front-panel-to-Click numbering.

Use the reported `up` field as the link decision. A 10 Gb/s cage can remain `down` while retaining `10000/full` as its configured or most recently selected mode. A nonzero speed therefore does not by itself mean that optical link is established.

The firmware has observed 1 Gb/s and 10 Gb/s Click link states on these cages, but that evidence does not certify a particular transceiver model. Exact module identity, peer, RxLOS/TxFault/TxDisable, PCS lock, and front-panel SFP LED behavior were not captured together. Treat an insertion-time EEPROM I2C error as a diagnostic event; one hardware run recovered on its own shortly afterward, but recurring errors require module, cage, and peer-specific testing.

Detailed test evidence and remaining research boundaries are kept in [MS42P port-map and SFP runtime evidence](../research/ms42p-port-map-and-sfp-evidence.md).

## PoE

Power and standard are independent:

- Power: enabled or disabled
- Standard: IEEE 802.3af or 802.3at
- Policy: normal or boot-prune

The PD690xx controller implements power control. Boot-prune enables configured ports, observes controller state/power for the configured window, and runtime-disables ports that remain unused without rewriting desired configuration. Reconnect after pruning requires manual re-enablement or Normal policy.

`/click/sw0_ctrl/poe_led_state` controls port LED indication only; it does **not** switch PoE power. It is registered only for MS220-8/MS220-8P.

The original PD690xx reset/enable GPIO initialization has been hardware-verified across the supported Luton26, Jaguar1 single-core, and Jaguar1 dual-core PoE models. Exact model profiles own the verified GPIO pair, and `S11poe` performs writes only when immutable board identity is exact and the model is PoE-capable. Non-PoE and unidentified systems remain write-disabled.
