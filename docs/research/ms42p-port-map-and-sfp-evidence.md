# MS42P port-map and SFP runtime evidence

This page records research evidence from the OpenVTSS v0.8 `port-map-boundaries` run. It is not an operating procedure. Administration guidance derived from these results is kept in [Ports, VLANs, STP, and PoE](../user-guide/ports-and-poe.md).

## Evidence identity

| Item | Value |
|---|---|
| Platform | Meraki MS42P |
| Run ID | `20260628-021242` |
| Test suite | `port-map-boundaries` |
| Target result | return code 0 |
| Capture mode | segmented |
| Progressive segments | 34 |
| Normalized events | 45,270 |
| Dropped records | 0 |
| Overflow segments | none |
| Highest negotiated UART rate | 921600 baud |
| Final UART rate | 115200 baud |
| Firmware image SHA-256 | `f12ee0c0f115d4c5558e971b7d0a08a2fce61af93b717166641c406aaad8c96c` |
| Source evidence archive SHA-256 | `e31371c9118f3ce18a8baf98d236dc11e4ce471350f447a673c2d7adb8972e7e` |

The run selected no PoE GPIO candidate, performed no unverified PoE GPIO writes, and left secondary-CPU investigation deferred.

## Representative copper mapping

The following physical ports changed from Click `up=0` to `up=1`, `1000 Mb/s`, full duplex when connected and returned to down after disconnection. The chip-local and MIIM values agree with the recovered MS42 static board table.

| Front port | Click logical index | ASIC | ASIC port | MIIM controller | PHY address | Evidence |
|---:|---:|---:|---:|---:|---:|---|
| 2 | 1 | 0 | 1 | 0 | 1 | runtime-proven |
| 12 | 11 | 0 | 11 | 0 | 11 | runtime-proven |
| 13 | 12 | 0 | 12 | 0 | 12 | runtime-proven |
| 24 | 23 | 0 | 23 | 0 | 23 | runtime-proven |
| 25 | 24 | 1 | 0 | 0 | 0 | runtime-proven |
| 36 | 35 | 1 | 11 | 0 | 11 | runtime-proven |
| 37 | 36 | 1 | 12 | 0 | 12 | runtime-proven |
| 48 | 47 | 1 | 23 | 0 | 23 | runtime-proven |

This proves the representative boundaries of the four 12-port copper banks and the 24-port split between chip 0 and chip 1. It does not elevate every untested intermediate port from static correlation to exact per-port runtime proof.

## SFP/SFP+ mapping and observations

Insertion and removal events were observed on all four cages. The physical, logical, and chip-local mapping is:

| Front port | Click logical index | ASIC | ASIC port | Observed connected Click state | Observed disconnected Click state |
|---:|---:|---:|---:|---|---|
| 49 | 48 | 0 | 29 | up, 1000/full | down, 0 |
| 50 | 49 | 0 | 30 | down, 0 | down, 0 |
| 51 | 50 | 1 | 29 | up, 10000/full | down with 10000/full retained |
| 52 | 51 | 1 | 30 | up, 10000/full | down with 10000/full retained |

Port 52 logged one module-EEPROM I2C read failure, then reported successful insertion 1.393 seconds later. This is evidence that a transient read failure can recover without proving the cause or declaring a particular module compatible.

The operator used AOI/ADI `A7EL-SN85-ADMA` and HP `AJ715A` modules, but module-to-port assignment was not recorded. The operator did not observe front-panel SFP LED activity. The run also omitted EEPROM payloads, RxLOS, TxFault, TxDisable, SERDES/PCS state, and LED callbacks. Therefore:

- no per-module compatibility claim is established;
- Click `up` observations are not independently corroborated by captured PCS/optical-control state;
- no SFP front-panel LED behavior is established;
- port 50 remaining down is a run observation, not a general incompatibility result.

## Reset/customization probe contract

Five compact probes registered before vendor initialization at an offset of eight bytes from the exported symbol:

- `jr1_ms42_post_reset`;
- `port_custom_pre_reset`;
- `port_custom_reset`;
- `post_port_custom_reset`;
- `port_custom_conf`.

The trace reported `preinit_pass=1`, `required_failed=0`, and `required_not_preinit=0`. The bounded `symbol+8` registration fallback is therefore hardware-proven for this module build. The five function bodies did not execute during this run, so their internal reset and customization sequence remains unresolved.

## Trace coverage

| Event class | Count |
|---|---:|
| MIIM events | 36,630 |
| I2C message events | 784 |
| Direct Jaguar register writes | 0 |
| Direct Jaguar register reads | 0 |
| GPIO events | 0 |

The capture used boot, quiet, PoE-observation, per-port connect, per-port disconnect, and final-idle segments. All 34 segments passed integrity checks without overflow or dropped records. The absence of a GPIO event in this trace does not reverse the separate hardware proof for the chassis reset and status-LED mappings; it only describes this capture profile.

The trace observed I2C traffic on adapter 1 at addresses `0x30`, `0x31`, `0x33`, and `0x35`. Each address produced 49 entry writes, 49 entry reads, 49 return writes, and 49 return reads. The collected result values were `0` and `2`; this run did not establish a new PoE controller identity or authorize a new reset/enable GPIO mapping. The OpenVTSS run flag `POE_GPIO_VERIFIED` remained false for this research path.

## Prescribed next SFP run

The evidence package recommends code revision `v13.2` with `candidate=none`. The preferred first test is one AOI/ADI `A7EL-SN85-ADMA` on port 51 or 52 with an identical module at the peer. The HP `AJ715A` is reserved for a later comparison after the primary Ethernet-optic pass and is not a primary compatibility reference.

## Remaining evidence gaps

The next SFP validation should use one labeled module and one labeled peer on one cage per run. It should capture, in order:

1. module insertion with fibre disconnected;
2. link attempt;
3. an explicitly controlled polarity retry when justified;
4. fibre-only disconnection;
5. module removal.

The capture should include module EEPROM identity, read-only Click SFP handlers, RxLOS, TxFault, TxDisable, SFP/10G PHY and SERDES state, PCS lock/status, and LED update callbacks.

Other unresolved MS42P boundaries remain:

- inter-chip fabric ports and VStaX UPSID/UPSPN assignments;
- secondary-chip interrupt routing;
- exact mappings for copper ports not individually exercised;
- the complete secondary MIIM/PHY topology beyond tested representative addresses;
- reset/customization function-body execution;
- PoE GPIO reset mapping and live power-delivery proof within this OpenVTSS evidence stream.
