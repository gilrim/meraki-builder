# Firmware research tools

These utilities support inspection, extraction, and reconstruction of vendor firmware images. They are not used by the normal postmerkOS build and may operate on raw flash dumps.

- `nand/` inspects and modifies historical NAND/UBI firmware volumes.
- `nor/` extracts and reconstructs the 16 MiB NOR layout used by Vitesse-based switches.
- `firmware-analysis/` locates embedded compression headers and footers.

Work on copies of all source images. Scripts that reconstruct firmware can destroy data if their output is flashed to incompatible hardware.
