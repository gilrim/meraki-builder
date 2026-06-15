# status module

`status.c` aggregates observed state without making any individual data source mandatory.

## Output

- device model and timestamp;
- hardware and PoE capabilities;
- management network state;
- CPU and PoE temperatures;
- per-port link speed and PoE power;
- structured `errors` array.

If `/click/switch_port_table/dump_pports`, thermal sysfs, a PoE controller, or the configuration file is unavailable, the affected section is partial and an error is appended. Other sections are still returned.

Status must not be used as the source of persistent desired settings when Click provides no readback. Storm control is the principal example.
