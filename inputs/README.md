# Local binary inputs

This directory is for untracked donor and bootloader inputs used by the MS42P build.

Accepted donor names include:

```text
postmerkOS-20240818.bin   complete 16 MiB donor firmware
donor-firmware.bin       complete 16 MiB donor firmware
good-rootfs.squashfs     standalone donor SquashFS
loader1.bin               standalone 256 KiB RedBoot loader
```

The build can download the known PostmerkOS donor and RedBoot loader when the required files are absent, but keeping verified local copies makes repeat builds independent of those download locations.

Do not commit donor firmware, proprietary modules, bootloader binaries, or generated images to this repository unless redistribution is explicitly permitted.
