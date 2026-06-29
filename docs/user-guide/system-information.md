# System Information

The web interface and configd status API expose a bounded, best-effort inventory of the running switch. Collection is read-only. Failure to read one source does not suppress unrelated sections; unavailable values are omitted or shown as unavailable, and collection errors are returned separately.

## Identity

When available, the inventory includes:

- exact hardware model;
- chassis serial number;
- product number;
- base MAC address;
- hostname.

Exact identity comes from the immutable boot-generated board-information record rather than editable configuration.

## Firmware and management software

The software section includes the running postmerkOS version, build date, Git/source revision, image format, configuration schema, management API version, compatibility state, and release-manifest evidence available to the image.

## Processor and kernel

Processor information is collected from `/proc/cpuinfo` and the running kernel and can include:

- SoC/system type and machine description;
- CPU model, hardware revision, and logical processor count;
- BogoMIPS, implemented ASEs, wait-instruction support, and TLB entries;
- kernel name, release, build version, architecture, node name, and command line.

Fields vary by kernel and SoC and are displayed only when reported by the target.

## Runtime

Runtime information includes:

- uptime and calculated boot time;
- 1-, 5-, and 15-minute load averages;
- running/scheduled process counts and observed process-directory count;
- current UTC/local time and synchronization state where available.

## Memory

Memory values are reported in bytes and include total, free, available, buffers, cache, reclaimable slab, total slab, and swap totals/free space when present in `/proc/meminfo`.

## Filesystems and flash

The inventory reports capacity, used/free space, source, filesystem type, and mount options for relevant mounts, including the read-only root, writable overlay/config storage, and temporary filesystems. It also lists MTD partition name, size, erase size, and aggregate flash capacity from the running kernel’s MTD table.

Filesystem free space is live status. It is not an estimate of how much data a future firmware image can contain; image partition limits remain defined by the [flash layout](../architecture/flash-layout.md).

## Hardware and service health

Where the platform exposes them, System Information also includes temperatures, management-service readiness, network state, reset-button state/countdown, chassis LED ownership and backend, hardware capability evidence, and exact-model compatibility information.

## Privacy and diagnostics

The inventory does not return account password hashes, private keys, firmware repository credentials, or configuration secrets. Administrators can use the JSON `get_status` response for diagnostics; lower roles receive only fields allowed by the status capability and server-side protocol enforcement.
