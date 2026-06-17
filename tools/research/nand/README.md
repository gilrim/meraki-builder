# NAND and UBI research tools

## Purpose

These scripts inspect historical Meraki NAND firmware volumes and can extract their embedded ramdisk, management programs, and binary kernel modules. They are retained for firmware research and donor analysis; the supported postmerkOS build does not modify vendor NAND images.

## Inputs

`extract.sh [DUMP]` expects an extracted NAND firmware volume such as `part1`, `part2`, `mtd12`, or `mtd13`. It locates the gzip ramdisk, writes extracted pieces under `bin/`, and expands the ramdisk under `ramdisk/`.

## Destructive utility

`make.sh` modifies an already extracted vendor ramdisk and creates a replacement SquashFS image. It is a research utility, not a supported installation or update path. Never run it against the only copy of a firmware dump.

## Dependencies

- POSIX host tools including `dd`, `file`, `gzip`, `cpio`, and `mksquashfs`
- a C compiler for the local `find_hdr` helper, or a prebuilt `find_hdr` in `PATH`

See `../firmware-analysis/README.md` for the header locator.
