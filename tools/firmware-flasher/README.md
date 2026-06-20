# Firmware flasher

`firmware-flasher.sh` validates a selected artifact and starts either the normal
Linux updater or meraki-redboot pre-kernel UART recovery.

## Artifact contracts

Manifest-aware mode requires the image, image SHA-256, JSON release manifest,
and manifest SHA-256. `--checksum-only` uses the current Linux updater with an
image and SHA-256 sidecar. `--legacy` targets older direct `fw_update_tftp`
installations and cannot perform a complete-NOR update.

## Linux updater mode

The flasher can publish an image through a private read-only TFTP server and
control the target over SSH or hardware serial. Linux UART transport is
available with `--control serial --transport uart`. System scope updates the
normal system regions; full scope requires an exact 16 MiB image and the
independent full-flash acknowledgements.

## Pre-kernel mode

```sh
./tools/firmware-flasher/firmware-flasher.sh \
  --bootloader-recovery \
  --firmware artifacts/<full-image>.bin \
  --target-model MS42P \
  --serial-device /dev/ttyUSB0
```

The default `--recovery-path embedded` drives meraki-redboot menu option 2 and
uses the recovery stage already embedded in the boot region. No standalone
payload is required. `--recovery-path ram-upload --recovery-payload FILE`
retains option-1 compatibility for external payload testing or older loaders.
`--recovery-path auto` prefers embedded recovery and permits the direct
RAM-loader fallback only when a valid external payload is supplied.

Verify is host-only, dry-run performs target validation without writes, and
flash uses the nonce-gated complete-NOR sequence documented in
[`BOOTLOADER-RECOVERY.md`](BOOTLOADER-RECOVERY.md).

## Examples

```sh
./tools/firmware-flasher/firmware-flasher.sh
./tools/firmware-flasher/firmware-flasher.sh --checksum-only --version 2026.06.20
./tools/firmware-flasher/firmware-flasher.sh --control serial --transport uart
./tools/firmware-flasher/firmware-flasher.sh --bootloader-recovery --target-model MS42P
./tools/firmware-flasher/firmware-flasher.sh --bootloader-recovery --target-model MS220-8P
./tools/firmware-flasher/firmware-flasher.sh --bootloader-recovery --recovery-path ram-upload \
  --recovery-payload artifacts/recovery/recovery-jaguar1.bin --target-model MS42P
./tools/firmware-flasher/firmware-flasher.sh --self-test
```
