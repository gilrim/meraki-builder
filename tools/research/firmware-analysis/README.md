# Firmware header locator

`find_hdr.c` scans a firmware image for gzip or XZ markers and prints their byte offset. It is the preferred host research implementation.

Build and use it with:

```sh
cc -O2 -Wall -Wextra find_hdr.c -o find_hdr
./find_hdr -g firmware.bin
./find_hdr -x firmware.bin
./find_hdr -x -f firmware.bin
```

`find_hdr.py` is retained as a compact reference implementation for historical analysis. The target firmware uses the independently packaged C implementation under `buildroot/packages/findhdr`.
