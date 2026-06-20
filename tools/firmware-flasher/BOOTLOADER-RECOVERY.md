# meraki-redboot UART firmware recovery

This workflow uses the source-built meraki-redboot v0.7 fixed-RAM boot menu and
its embedded `PMOSPKG2` recovery stage. It does not require Linux, SSH, TFTP,
networking, or an external recovery payload.

```sh
./tools/firmware-flasher/firmware-flasher.sh \
  --bootloader-recovery \
  --recovery-path embedded \
  --firmware artifacts/<full-image>.bin \
  --target-model MS42P \
  --serial-device /dev/ttyUSB0
```

The default `embedded` path waits for `PMOSBOOT MENU-PROBE`, sends carriage
return as the discarded trigger, selects option `2`, validates each reported
menu/recovery PASS marker, and requires `PMOSREC READY 2` from the expected SoC
family. Both `luton26` and `jaguar1` are supported targets.
`auto` behaves the same on v0.7 and can fall back to a directly exposed
`PMOSRAM READY 2` listener when a matching `--recovery-payload` is supplied.
`ram-upload` explicitly selects menu option `1` and uploads that external
payload before starting `PMOSPKG2`.

## Local validation

Before serial access, the flasher checks:

- exact 16 MiB image and release-manifest SHA-256;
- source-built meraki-redboot v7 capability record and loader digest;
- boot-menu option map and embedded family recovery digest;
- exact target model, compatibility status, and boot-family mapping;
- SPIM magic, load/entry addresses, reserved words, 32-byte alignment, slot
  boundary, payload SHA-256, and CRC-32;
- SquashFS location;
- recovery payload descriptor and digest when an external path is requested;
- flash geometry and JEDEC allow-list.

## Operations

- **Verify** performs local validation and never opens the serial port.
- **Dry run** uploads the image and manifest and performs target-side flash
  identification and policy checks without erase/program.
- **Flash** requires complete-image authorization and a target-generated nonce.
- **Force flash** acknowledges an `untested` model status; it does not bypass
  family, geometry, digest, CRC, JEDEC, protection, or hard-boundary checks.

## Protocol sequence

1. Reset or power-cycle the target.
2. Receive `PMOSBOOT MENU-PROBE`, send `0x0d`, require
   `PASS-MENU-TRIGGER`, receive the menu and `PMOSBOOT MENU-READY`, then send
   `2`.
3. Require `PASS-MENU-CHOICE`, `INFO-RECOVERY` with the model-matched SoC,
   `PASS-RECOVERY-SIZE`, `PASS-RECOVERY-COPY`, and `PASS-RECOVERY-EXEC`.
4. Receive `PMOSREC READY 2` and verify `SOC=luton26` or `SOC=jaguar1` against
   the selected model.
5. Send the package header and await acceptance.
6. Send image and manifest with acknowledged `PKF2` frames.
7. Require whole-object CRC-32/SHA-256 and manifest validation.
8. For dry-run, require `PMOSREC RESULT DRY-RUN-OK`.
9. For flash, return the exact `ERASEFLASH <nonce>` only after local
   confirmation, then require `PMOSREC RESULT SUCCESS`.

Pre-kernel flash rewrites loader, kernel, SquashFS, and JFFS2. Keep a verified
external SPI backup and programmer available.
