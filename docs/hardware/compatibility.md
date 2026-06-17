# Hardware compatibility

Compatibility is recorded by model and capability rather than by a single global supported flag.

- **Confirmed:** a successful runtime report exists.
- **Untested:** the platform appears compatible but requires an explicit warning acknowledgement.
- **Known-incompatible:** architecture or flash geometry is known not to match; normal updater validation blocks it.

| Model | Family | Ports | Copper + uplink | PoE ports | Status |
|---|---|---:|---:|---:|---|
| MS220-8 | VCore-III Luton | 10 | 8 + 2 | 0 | Untested |
| MS220-8P | VCore-III Luton | 10 | 8 + 2 | 8 | Untested |
| MS220-24 | VCore-III Luton | 24 | 24 + 0 | 0 | Untested |
| MS220-24P | VCore-III Luton | 24 | 24 + 0 | 24 | Untested |
| MS22 / MS22P | VCore-III Luton | 24 | 24 + 0 | 0 / 24 | Untested |
| MS220-48 variants | VCore-III Jaguar | 52 | 48 + 4 | model dependent | Untested |
| MS42 | VCore-III dual Jaguar | 52 | 48 + 4 | 0 | Untested |
| MS42P | VCore-III dual Jaguar | 52 | 48 + 4 | 48 | **Confirmed** |
| MS320-24 | VCore-III Jaguar | 28 | 24 + 4 | 0 | Untested |
| MS320-24P | VCore-III Jaguar | 28 | 24 + 4 | 24 | **Confirmed** |
| MS320-48 variants | VCore-III Jaguar | 52 | 48 + 4 | model dependent | Untested |
| MX80 | PowerPC | appliance | — | — | Untested build target |
| MX84 | Cavium/Vitesse | appliance | — | — | Untested, incomplete build inputs |

Confirmation can be granular. A model may be confirmed for boot, management, port mapping, and PoE while destructive firmware-update behavior remains untested.

Untested devices are allowed to proceed after a warning. Users should keep a direct SPI recovery method and submit the generated compatibility report after testing.
