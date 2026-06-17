# Flash layout and persistence

The Vitesse switch image occupies a 16 MiB NOR device:

| Region | Start | End | Size | Purpose |
|---|---:|---:|---:|---|
| RedBoot | `0x00000000` | `0x0003ffff` | 256 KiB | Loader |
| Kernel | `0x00040000` | `0x002fffff` | 2816 KiB | Compressed Linux kernel |
| SquashFS | `0x00300000` | `0x00afffff` | 8 MiB | Read-only root filesystem |
| JFFS2 | `0x00b00000` | `0x00ffffff` | 5 MiB | Persistent configuration/state |

The build accepts any SquashFS that fits within exactly 8,388,608 bytes. No artificial reserve is enforced.

JFFS2 stores switch, service, security, time, account, key, repository, update-history, and compatibility-acknowledgement state. Factory reset erases this region and leaves the kernel/rootfs intact. Overlay-preserve, migrate, reset, and image policies determine updater treatment of JFFS2.
