# Vitesse switch hardware access

All UART headers described here use 3.3 V signaling. Never connect the VCC pin. Connect TX to RX, RX to TX, and GND to GND at 115200 8N1.

## MS220-8P

The UART header is near the front of the board and exposes, in order, no-connect, serial TX, serial RX, and GND. The SOIC16 SPI flash is accessible from the top of the board. Verify clip wiring against the SOIC16 mapping in the [flashing guide](../installation/hardware-flashing.md).

## MS220-24P

UART is header J3 beside the 12 V power connector. Pin 1 is VCC and must remain disconnected; the remaining pins are TX, RX, and GND. The SPI flash is near the blower. Pads below the flash provide an alternate programming header; some board revisions require the RS1 pads to be bridged for programmer VCC continuity.

## MS22P

UART is right-angle header J4 beneath the PoE injection board: pin 1 VCC, pin 2 TX, pin 3 RX, pin 4 GND. The PoE board does not need to be removed for normal UART access.

## MS220-48LP/FP

UART header J1 is near the fan connector at the rear of the PCB: VCC, TX, RX, GND. The SOIC16 SPI flash is accessible on the main PCB.

## MS42/MS42P

UART header J2 uses the standard VCC, TX, RX, GND order. The SOIC16 flash is on the underside of the PCB and is inconvenient to clip directly. Unpopulated header JY1 provides these useful points:

| JY1 pin | Signal | SOIC16 pin | SOIC8 pin |
|---:|---|---:|---:|
| 1 | GND | 10 | 4 |
| 6 | CS# | 7 | 1 |
| 7 | CLK | 16 | 6 |
| 8 | DO | 8 | 2 |
| 9 | DI | 15 | 5 |
| 10 | 3.3 V | 2 | 8 |

## MS320-24/P

UART header J2 is below the Cisco/560-20050 marking and may be partly obscured by a heatsink. It uses VCC, TX, RX, GND. The SPI flash is on the top side near the central/rear portion of the board.

## MS320-48 variants

UART header J1 uses pins 5–8 for VCC, TX, RX, and GND. The SPI flash is near the rear of the PCB.

## Disassembly

Compact models generally use bottom screws. One-rack-unit models open by removing rear screws and sliding the top cover toward the rear before lifting it. Forcing the cover vertically can damage its retention tabs.
