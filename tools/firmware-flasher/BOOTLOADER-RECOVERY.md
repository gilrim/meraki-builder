# meraki-redboot UART firmware recovery

The flasher supports two pre-kernel stages:

- **RAM upload (menu option 1):** uploads the model-matched corrected recovery
  binary to `0x81000000`, executes it, then transfers the full firmware image.
  This is the default and is required for a switch still running the original
  meraki-redboot v0.7.0 build.
- **Embedded recovery (menu option 2):** executes the recovery binary embedded
  in the installed loader. Use this only after the loader has been rebuilt with
  the `flat-binary-byte-zero-v1` entry correction.

The original v0.7.0 recovery ELF recorded `_start` as its ELF entry but emitted
MIPS metadata before `.text` in the raw binary. Stage 1 always jumps to byte zero
at `0x81000000`, so the old embedded stage can log `PASS-RECOVERY-EXEC` and then
remain silent. The corrected build uses an assembly entry veneer at byte zero,
clears BSS, initializes GP and the stack, and calls `recovery_main`.

## Recommended recovery of a switch running original v0.7.0

First rebuild the complete firmware and recovery artifacts with the corrected
builder. Then run the flasher interactively and select:

1. the newly rebuilt full 16 MiB image;
2. flash or force-flash;
3. `meraki-redboot UART recovery`;
4. `Upload corrected recovery utility through RAM loader`;
5. the exact model and serial device.

Equivalent command-line use:

```sh
./tools/firmware-flasher/firmware-flasher.sh \
  --bootloader-recovery \
  --recovery-path ram-upload \
  --firmware artifacts/<new-full-image>.bin \
  --target-model MS42P \
  --serial-device /dev/ttyUSB0
```

The flasher automatically selects the matching artifact:

- Luton26: `artifacts/recovery/recovery-luton26.bin`
- Jaguar1: `artifacts/recovery/recovery-jaguar1.bin`

The adjacent `.descriptor.json` must declare:

- load and entry address `0x81000000`;
- `entry_contract: flat-binary-byte-zero-v1`;
- `manifest_lookup_contract: direct-object-members-v1`;
- exact payload size and SHA-256;
- matching SoC family, SPI register and accepted models.

## Embedded and automatic modes

After a corrected loader has been flashed, `--recovery-path embedded` selects
menu option 2 and requires `PMOSREC READY 2` after the loader's
`PASS-RECOVERY-EXEC` marker.

`--recovery-path auto` tries embedded recovery first. If an affected v0.7.0
loader reports `PASS-RECOVERY-EXEC` but never emits `PMOSREC READY 2`, the host
identifies the entry-offset defect, asks for a reset or power cycle, waits for
the menu again, and falls back to option 1 with the corrected external payload.

## Local validation

Before serial access, the flasher checks:

- exact 16 MiB image and release-manifest SHA-256;
- source-built meraki-redboot v7 capability record and loader digest;
- corrected entry contract in both embedded and external recovery metadata;
- exact target model and Luton26/Jaguar1 mapping;
- SPIM load/entry addresses, 32-byte alignment, slot boundary and CRC-32;
- SquashFS location, flash geometry and JEDEC allow-list;
- external payload size, digest, family and entry contract.

A firmware image built before this correction is intentionally rejected. Even
if a corrected external stage could write it, that image would reinstall the
broken embedded recovery payload.

## Protocol sequence for RAM upload

1. Reset or power-cycle the target.
2. Receive `PMOSBOOT MENU-PROBE`, send carriage return, and validate the menu.
3. Select option `1` and require `PMOSRAM READY 2` for the expected SoC.
4. Upload the corrected recovery payload with acknowledged `PMOSRAM2` frames.
5. Require `PMOSREC READY 2` from the uploaded stage.
6. Send the package header, firmware image and manifest.
7. Require target-side CRC-32, SHA-256, manifest and hardware validation.
8. For flash, return the exact target-generated `ERASEFLASH <nonce>` challenge.
9. Require `PMOSREC RESULT SUCCESS`.

Pre-kernel flashing rewrites loader, kernel, SquashFS and JFFS2. Keep a verified
external SPI backup and programmer available.

## Recovery descriptor handoff

`PMOSREC READY 2` is followed by a descriptor line. The host must wait for and
validate the complete descriptor before sending the binary package header. The
recovery stage uses a polling UART while printing startup text; transmitting at
the first READY line can overrun its receive FIFO and produce
`PMOSREC RESULT ERROR PACKAGE-HEADER-TIMEOUT` even though no firmware upload has
started. The descriptor newline is the protocol's safe host-to-target handoff.

Current host tooling accepts both Jaguar1 and Luton26 descriptors and validates
the family ID and SPI software-mode register before transmitting. Future-built
recovery payloads also allow 30 seconds for the initial package header; frame
interbyte limits remain unchanged.
## Manifest digest shadowing correction

Recovery payloads built before this correction used an unscoped minimal JSON key
search. In a sorted artifact object, `kernel_payload.sha256` appears before the
direct `artifact.sha256` member. The old recovery stage therefore compared the
kernel payload digest with the full-image digest and reported
`PMOSREC RESULT ERROR MANIFEST-IMAGE-DIGEST` even though both transferred objects
had already passed transport CRC-32 and SHA-256 verification.

Corrected payloads declare `direct-object-members-v1` and limit every JSON lookup
to direct members of the object currently being validated. The flasher rejects
external payloads and firmware loader records that lack this contract.

