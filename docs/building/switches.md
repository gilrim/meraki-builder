# Building VCore-III switch firmware

Run `make base` for a console-focused image or `make web` for the optional web
interface. The supported host path remains Ubuntu 22.04 through Distrobox when
building from an Arch-derived workstation.

The build prepares:

- the pinned kernel/OpenWrt source used for Linux 3.18 and headers;
- the latest tagged `Gadorach/meraki-redboot` source by default;
- meraki-redboot's checksum-pinned GCC 4.7.3/binutils toolchain and 256 KiB boot
  region;
- the Luton26 and Jaguar1 recovery stages embedded by that loader;
- the watchmysys donor image, retained only for proprietary `.ko` extraction;
- Buildroot 2023.02.4, generated overlays, optional UI, SquashFS, and the
  complete 16 MiB NOR image.

## Loader source selection

```sh
make sources                     # LOADER_REF=latest
LOADER_REF=0.7.0 make loader      # exact release tag
LOADER_REF=<commit> make loader   # exact revision
```

`latest` prefers the highest stable remote version tag and falls back to the
highest prerelease tag only when no stable version tag exists. The resolved commit and VERSION
are persisted in `artifacts/`; cached loader output is rebuilt when it no longer
matches the selected source revision. The builder does not download or consume
Hal Martin's patched RedBoot binary.

`LOADER_VARIANT=development` is the default for this project integration. It
retains the loader's warn-and-continue compatibility policy where continuation
is safe. Use `LOADER_VARIANT=strict` only after completing hardware acceptance. The
source project also exposes `permissive` for diagnostics; release images should
normally use `development` during bring-up or `strict` after validation. The
builder keeps the UART recovery menu enabled for every selected policy.

## Kernel image generation

The selected meraki-redboot checkout supplies `tools/mkvcoreiii_payload.py`.
The post-image stage uses it to pad `vmlinuz.bin` to 32 bytes, generate the
32-byte SPIM header, calculate CRC-32, and enforce a maximum payload of
`0x002bffe0`. A different hand-built header is not accepted.

## Donor boundary

The donor image is still downloaded from watchmysys when authorized. Its
purpose is currently limited to extracting the complete Vitesse/Click module
matrix and required runtime files. The build does not copy the donor loader or
use it to construct the boot region.

## Useful commands

```sh
make doctor
make sources
make loader
make donor
make base
make web
make test-all
```
