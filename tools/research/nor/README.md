# NOR firmware research tools

## Purpose

These scripts document and inspect the 16 MiB NOR layout used by Vitesse-based MS22, MS42, MS220, and MS320 switches. They are retained for recovery analysis and low-level image research. Normal postmerkOS images are produced by the integrated Buildroot post-image process.

## `extract.sh`

Splits a complete vendor NOR dump into loader, boot, UBI, configuration, stack-configuration, and syslog regions. It also extracts the first boot header and kernel payload.

## `make.sh`

Reconstructs a complete research image from manually prepared components. It is not used by the supported build and assumes specific files and offsets. Use only with verified backups and a direct hardware recovery method.

## Layout references

The `layout-*.txt` files preserve known historical region layouts. They are documentation and flashrom layout inputs, not model autodetection.
