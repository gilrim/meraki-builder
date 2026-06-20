# Pre-boot UART hardware preflight

The preflight exists to find controller, wiring, flash-command and write-path
faults before transmitting a complete 16 MiB firmware package.

## Why it is required

A recovery attempt can validate the UART RAM loader, recovery binary and both
package objects but still fail only when the target first contacts SPI NOR. An
all-high JEDEC value (`ffffff`) is a bus-no-response indication, not a real flash
identifier. On Jaguar1, `IF_MASTER_SPI_ENA` in `GENERAL_CTRL` must remain enabled
when leaving the boot stage.

## Sequence

1. Enter meraki-redboot menu option 1 or corrected option 2.
2. Validate the SoC-specific recovery descriptor.
3. Preserve `GENERAL_CTRL` and enable the SPI master where required.
4. Read JEDEC ID, status registers and optional SFDP signature.
5. Reject unsupported, busy, protected or error-latched parts.
6. CRC-32 the complete 256 KiB bootloader region.
7. Back up one non-loader 64 KiB sector into RAM.
8. Erase and verify the sector.
9. Program a deterministic pattern through every page.
10. Read back and verify every byte.
11. Erase, restore and verify the original sector.
12. CRC-32 the bootloader again and require an exact match.
13. Emit a terminal pass result and host JSON receipt.

The target rejects any scratch address below `0x00040000`, any unaligned range,
any range other than exactly one erase block, and any request that does not require
restoration. The default is the final flash sector, `0x00ff0000`.

This test is destructive during execution. A power loss during the test can leave
the chosen scratch sector erased or partially programmed. It never intentionally
modifies the bootloader region, and a successful result additionally proves that
its complete before/after CRC-32 is unchanged. The normal successful path restores
the exact original scratch-sector contents.
