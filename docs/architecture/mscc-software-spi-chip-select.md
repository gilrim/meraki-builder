# MSCC software-SPI chip-select contract

The `ICPU_CFG:SPI_MST:SW_MODE.SW_SPI_CS` field is an **active mask**. For the boot NOR on chip select zero:

- `SW_SPI_CS = BIT(0)` asserts CS0;
- `SW_SPI_CS = 0` deselects every chip select;
- `SW_SPI_CS_OE = BIT(0)` enables the CS0 output driver.

Recovery uses the MSCC mode-0 sequence:

1. assert CS0 with the active mask and start SCK high;
2. drive data with SCK low;
3. raise SCK and sample SDI near the following falling edge;
4. clear the CS field to deselect;
5. release SCK output-enable and clear `SW_MODE`.

The target reports the selected contract before its JEDEC probe:

```text
PMOSREC SPI-CS-CONTRACT ACTIVE-MASK CS0=00000001 NONE=00000000
```

Current payloads require `PREFLIGHT=4` and `spi-nor-scratch-rw-restore-loader-crc-v4`. The builder and host flasher reject payloads that do not declare this active-mask contract, preventing incompatible cached recovery stages from being used.
