# Building Vitesse switch firmware

## Common targets

```sh
make doctor
make deps
make base       # console-only image
make web        # image with the optional browser UI
make validate
```

`make all` prompts for the web option. `make distrobox` runs the build in Ubuntu 22.04 through Distrobox, which is the recommended compatibility environment on CachyOS/Arch hosts.

The integrated build prepares pinned kernel/OpenWrt sources, donor binary modules and loader inputs, Buildroot 2023.02.4, generated overlays, the optional UI, SquashFS, and the complete 16 MiB NOR image.

Local binary inputs belong under `inputs/`; generated work is under `.work/`; published files are under `artifacts/`.

Every final image reports exact SquashFS bytes used, the 8 MiB maximum, percentage, remaining bytes, largest packages, and largest files. The build fails only when the physical region is exceeded.
