# Hardware flashing

## Back up the original flash

Examples use a CH341A and the common Macronix MX25L12805D. Confirm the actual chip marking before forcing a chip model.

```sh
flashrom -p ch341a_spi -c MX25L12805D -r original-1.bin
flashrom -p ch341a_spi -c MX25L12805D -r original-2.bin
sha256sum original-1.bin original-2.bin
cmp original-1.bin original-2.bin
```

If the files differ, stop and correct the connection. A single successful read is not a trustworthy backup.

The host `tools/Flasher/firmware-flasher.sh` utility automates repeated reads, comparisons, serial/SSH handoff, firmware selection, and monitoring.

## Write a complete image

With the switch unpowered and all network/SFP connections removed:

```sh
flashrom -p ch341a_spi -c MX25L12805D -w postmerkos-switch.bin
```

Wait for erase, write, and verification to finish. Do not interrupt power to the programmer or host.

## SOIC16 to SOIC8 mapping

| Signal | SOIC16 pin | SOIC8/programmer pin |
|---|---:|---:|
| GND | 10 | 4 |
| CS# | 7 | 1 |
| CLK | 16 | 6 |
| DO | 8 | 2 |
| DI | 15 | 5 |
| 3.3 V | 2 | 8 |

Many SOIC16 clips ship with straight-through ribbon cables that do not match an SOIC8 programmer. Verify every connection with the programmer disconnected.

## UART

Connect adapter TX to switch RX, adapter RX to switch TX, and GND to GND. Never connect VCC. UART inputs are not tolerant of voltages above 3.3 V.

See [Vitesse switch hardware](../hardware/vitesse-switches.md) for model-specific access points and [Recovery](recovery.md) for restoring a complete backup.
