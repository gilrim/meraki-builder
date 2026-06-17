# Ports, VLANs, STP, and PoE

Per-port configuration includes administrative state, name, PHY speed, flow control, Energy Efficient Ethernet, storm control, VLAN behavior, spanning tree, and PoE where detected.

## VLAN modes

- Access: one untagged access/PVID VLAN
- Trunk: tagged allowed list and PVID
- Hybrid: tagged allowed list plus native/untagged VLAN

## PoE

Power and standard are independent:

- Power: enabled or disabled
- Standard: IEEE 802.3af or 802.3at
- Policy: normal or boot-prune

Normal policy uses the PD690xx controller’s powered-device detection. Boot-prune enables the configured ports at startup, observes controller state and power for the configured window (300 seconds by default), and runtime-disables ports that remain unused. It does not rewrite desired configuration and does not periodically probe. Equipment connected after pruning requires manual re-enablement or changing the policy to Normal.

Update/reset LED behavior is capability-driven. Binary per-port control through the Click `poe_led_state` handler is the minimum supported indication; alternate colours are used only when verified for the model.
