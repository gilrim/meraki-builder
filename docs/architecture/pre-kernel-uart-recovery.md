# Pre-kernel UART recovery

The pre-kernel path is supplied by the source-built `Gadorach/meraki-redboot`
boot region. Version 0.7 adds a fixed-RAM stage-1 menu before the flash kernel is
entered. It does not require Linux, networking, SSH, or a working rootfs.

| Path | Runtime | Transport | Flash engine |
|---|---|---|---|
| Normal UART update | Linux userspace | `PMOSUART/1` Base64 frames | `fw_update` through MTD |
| Embedded pre-kernel recovery | meraki-redboot menu option 2 | `PMOSPKG2` binary frames | embedded family recovery stage |
| External compatibility path | meraki-redboot menu option 1 | `PMOSRAM2`, then `PMOSPKG2` | uploaded family recovery stage |

## Source and build contract

`make sources` resolves `LOADER_REF=latest` to the highest tagged
`Gadorach/meraki-redboot` release. Set an exact tag or commit for a reproducible
build. `make loader` invokes that repository's own build system and pinned
legacy toolchain; no downloaded RedBoot/LinuxLoader binary is accepted as a
build input.

The loader is built with the postmerkOS boundaries:

- 256 KiB boot region;
- kernel slot end `0x00300000`;
- maximum SPIM payload `0x002bffe0` bytes;
- UART RAM-loader protocol v2;
- 3-second menu probe and 5-second explicit selection window by default;
- Luton26 and Jaguar1 embedded recovery stages.

The builder requires manifest format
`postmerkos.vcoreiii-linuxloader-build.v7`, verifies the source revision, boot
region digest, menu markers, policy geometry, structured diagnostic capability,
and both embedded recovery descriptors. The final release manifest binds those
records to the complete 16 MiB image.

## Boot menu

After reset, stage 1 reports `PMOSBOOT MENU-PROBE`. The postmerkOS host sends
a carriage return (`0x0d`) as the discarded trigger byte, verifies
`PASS-MENU-TRIGGER`, waits for the menu and `PMOSBOOT MENU-READY`, then sends
an explicit choice:

1. `UART-RAMLOADER` — persistent `PMOSRAM READY 2` listener for an external
   executable payload.
2. `FW-RECOVERY` — copy and execute the family-matched recovery stage already
   embedded in the boot region.

No input continues normal boot. Invalid input or a selection timeout also
returns to normal boot. The postmerkOS flasher uses option 2 by default and
requires the complete `PASS-MENU-CHOICE`, `INFO-RECOVERY`,
`PASS-RECOVERY-SIZE`, `PASS-RECOVERY-COPY`, and `PASS-RECOVERY-EXEC` sequence
before accepting `PMOSREC READY 2`.

## Image diagnostics and development policy

The loader emits `PASS-*`, `WARN-*`, `FAIL-*`, and `SKIP-*` records with the
actual and expected values where applicable. The provided development variant
uses warning policies for legacy CRC/size compatibility and continues when a
safe continuation exists. Hard boundaries, unsafe addresses, truncated data,
and other conditions that cannot be continued safely remain fatal and enter
recovery.

This policy does not relax host image generation. Every postmerkOS kernel is
packed with the canonical `mkvcoreiii_payload.py` from the selected loader
source, padded to 32 bytes, limited to the kernel slot, and assigned an IEEE
CRC-32 over the zero-CRC header plus the complete padded payload.

## `PMOSPKG2`

The embedded recovery stage reports `PMOSREC READY 2 SOC=<family>`. Both
`SOC=luton26` and `SOC=jaguar1` are valid; the host requires the one mapped to
the exact selected model and sends:

1. the exact 16 MiB firmware image;
2. the matching JSON release manifest.

Frames carry sequence numbers and CRC-32. Whole objects carry CRC-32 and
SHA-256. Before any erase, host and target validate the image digest, exact
model and compatibility status, loader digest and source capability, SPIM
header/alignment/CRC, SquashFS placement, recovery family and SPI register,
flash geometry, JEDEC allow-list, and protection/status state.

`verify` is host-only. `dry-run` transfers the bundle and performs target
preflight without erase/program. `flash` additionally requires the flasher's
full-image authorization and the target-generated `ERASEFLASH <nonce>` response.

## Family naming boundary

The boot/recovery layer calls the MS42/MS42P hardware family `jaguar1` because
it selects the Jaguar1 SPI software-mode register at `0x70000068`. This must not
be confused with the Linux runtime module profile: MS42/MS42P remain
`jaguar_dual`, use two Jaguar1 devices, and load the donor `jaguar_dual`
`vtss_core`/`vc_click` objects. The builder deliberately keeps these namespaces
separate.
