# Switch counter telemetry validation

configd reads raw switch-port counters from the Click handler
`/click/switch_port_table/switch_port_protobuf`. The decoder currently expects the
wire layout implemented in `buildroot/packages/configd/portstats.c`.

That handler and schema are not yet hardware-proven by the supplied MS42P runtime
evidence. The management plane therefore treats the source as required evidence:
SNMP cannot be enabled unless configd can read and decode a nonempty snapshot and
atomically publish `/run/postmerkos/portstats.v1`. A missing handler, malformed
payload, or zero-port result fails the configuration transaction instead of
starting an exporter with invented or empty IF-MIB rows. Prometheus remains
available for device-health metrics and reports `postmerkos_scrape_success 0`
until a valid port snapshot exists.

## Capture procedure on an MS42P

Run as root on a test switch:

```sh
cd /tmp
/path/to/capture-portstats-evidence.sh
```

The helper is located at
`tools/research/capture-portstats-evidence.sh`. It records:

- three raw protobuf samples taken several seconds apart;
- hexadecimal renderings and SHA-256 checksums;
- `dump_pports` output for port/link mapping; and
- firmware, host, and kernel metadata when available.

Do not transform or paste the raw binary through a terminal before preserving it.
Archive the entire evidence directory. A valid promotion test should then:

1. add the raw captures as golden fixtures;
2. decode each capture with `portstats_decode`;
3. confirm the number and identity of ports against `dump_pports`;
4. create controlled traffic on selected ports and confirm the matching counters
   increase without unrelated ports changing unexpectedly; and
5. reboot with SNMP enabled and verify all expected IF-MIB rows exist immediately.

If the Click handler does not exist, retain the generated `ERROR.txt` and capture
an inventory of `/click/switch_port_table/`. The next implementation should then
use a proven handler or add an OpenVTSS export surface rather than guessing at a
binary schema.
