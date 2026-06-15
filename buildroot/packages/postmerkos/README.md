# libpostmerkos

`libpostmerkos` contains low-level helpers shared by configd and status utilities.

## Responsibilities

- read and write Click handler files;
- construct `switch_port_table` handler paths safely;
- return negative `errno` values instead of terminating the caller;
- parse whitespace-separated handler rows into caller-owned buffers;
- provide common string and timestamp helpers.

## Important behavior

All functions are best-effort primitives. Callers decide whether a missing handler is fatal, a warning, or an omitted status field. No helper calls `exit()`.

```c
char value[64];
int rc = click_read_line("/click/uplinkstate/dhcp_state", value, sizeof(value));
if (rc != 0) fprintf(stderr, "read failed: %s\n", strerror(-rc));

rc = click_write("/click/configure_igmp_snoop/run", "true");
```

Never pass untrusted text as a format string. Paths are bounded and checked for truncation.
