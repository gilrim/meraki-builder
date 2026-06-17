# Meraki Click graph reference for MS220/MS42-family firmware

This document describes the Click graph shipped as `buildroot/board/meraki/ms220/overlay/etc/switch.template.gz`, how the graph is instantiated, which interfaces are used by postmerkOS, and how to safely interpret the data exposed under `/click`.

The graph is derived from Meraki switch firmware and contains elements whose implementations live in binary kernel modules. The template shows topology, element names, scripts, and handler calls, but it does not provide the implementation of proprietary elements such as `SwitchPortTable`, `VitesseController`, or `UplinkState`.

## 1. Runtime architecture

```text
Linux userspace
  shell scripts / configd / diagnostic tools
                    |
                    v
              /click handlers
                    |
                    v
Meraki Click graph + merakiclick/vc_click modules
                    |
                    v
Vitesse switch ASIC, host interfaces, PD690xx PoE hardware
```

`S09clickinit` mounts the Click filesystem and expands the compressed template:

```sh
mount -t click none /click
zcat /etc/switch.template.gz \
  | sed -e "s/__MY_MAC_ADDRESS__/$MAC/g" \
        -e "s/__NUM_SWITCHPORTS__/$NUM_PORTS/g" \
        -e "s/__PORT_POE_MW__/$PORT_POE_MW/g" \
        -e "s/__MTUN_IP__/$MTUN_IP/g" \
  > /click/config
```

Writing a complete graph to `/click/config` creates the named elements and starts active scripts. The files that appear below `/click/<element>/` are Click handlers, not ordinary persistent files.

## 2. Handler semantics

A handler can be:

- **read-only**: `cat` returns current or calculated state;
- **write-only**: writing invokes an operation but there is no matching readback;
- **read/write**: the same handler supports both;
- **script entry point**: usually a `run`, `set`, or similar handler on a `Script` element;
- **proxy**: a `HandlerProxy` exposes another element handler under a stable name.

Examples:

```sh
cat /click/switch_port_table/dump_pports
printf '%s\n' 'PORT 1, FC_OBEY false, EEE_ADV_ENABLED true, MODE aneg' \
  > /click/switch_port_table/set_port_phy_cfgs
printf '%s\n' '192.168.1.20 24 192.168.1.1 1500 192.168.1.255 1' \
  > /click/set_host_ip/run
```

A successful shell write only proves the handler accepted the bytes at the filesystem layer. It does not always prove the ASIC accepted or retained the requested setting. Use a matching dump handler when one exists, status counters where available, and persistent desired state for write-only operations.

## 3. Major graph regions

The template is large because it retains much of the original Meraki control plane. The important regions are:

### Host and switch interfaces

- `to_sw0` / `from_sw0`: data path between Click and the Vitesse ASIC.
- `sw0_ctrl`: `VitesseController`, the main hardware-control element.
- `to_wired0` / `from_wired0`: internal Linux host interface.
- `to_linux_mgmt` / `from_linux_mgmt`: Linux management path.
- `to_sockproxy0` / `from_sockproxy0_dev`: userspace socket proxy path.

### Switching and port control

- `switch_port_table`: per-port PHY, VLAN, storm-control, LACP, and switch-port metadata.
- `switch_table`: learned switching table.
- `switch_intf_table`: routed interface state.
- `switch_auth_port_table`: authentication policy per port.
- `stp`: bridge spanning-tree state and configuration.
- `lacp`: aggregation protocol engine.
- `sfp_mgr`: SFP module manager.

### Management address and uplink discovery

- `uplinkstate`: DHCP/static uplink state database.
- `uplink_dhcp_client`: DHCP client integrated with the switch graph.
- `uplink_gw_arp_prober`: IPv4 gateway reachability.
- `wan0_resolver`: DNS target state.
- `set_host_ip`: public management IPv4 update script.
- `set_internal_ip`: internal Linux/Click address update script.
- `announce_uplink_ip`: callback used when `UplinkState` changes.

### Multicast

- `igmp_table`, `igmp_querier`, `igmp_snoop`, `igmp_client`.
- `mld_table`, `mld_querier`, `mld_snoop`, `mld_client`.
- `configure_igmp_snoop` and `configure_mld_snoop` script entry points.

### Client tracking and services

- `client_ip_table`, `client_ip_tracker`, `dhcp_tracker`.
- `mdns_services`, `dns_painter`, HTTP/SSL painters.
- LLDP and CDP source/tracking elements.
- Event and syslog elements.

### Routing and tunnel infrastructure

The graph contains OSPF, VRRP, route-state, multicast routing, Meraki tunnel, RPC, ACL, NAT, and stack-management components. postmerkOS currently initializes many of these to safe defaults but does not expose all of them as supported configuration features.

## 4. Management IPv4

### DHCP state

```sh
cat /click/uplinkstate/dhcp_state
```

Typical output:

```text
vlan added_by active state disc_ago offer_ago req_ago ack_delay renew_at exp_at              ip         gw        bcast             dns mtu
   1      C|T   true bound  1506512   1506511   16010    0.0049     6017  27189 192.168.0.15/22 192.168.0.1 192.168.3.255 192.168.0.1 0.0.0.0 1500
```

Fields used by `configd`:

| Field | Meaning |
|---|---|
| `vlan` | Management VLAN |
| `active` | Lease is selected as active |
| `state` | Must be `bound` before application |
| `renew_at` | Seconds until the next useful lease check |
| `exp_at` | Seconds until lease expiry |
| `ip` | Address and CIDR prefix |
| `gw` | Gateway |
| `bcast` | Broadcast address |
| `dns` | Up to two DNS values in this table format |
| `mtu` | Lease MTU; invalid values fall back to 1500 |

`configd` clamps `renew_at` to a 5–300 second polling window so a link change is not ignored for hours.

### Brain-compatible state

```sh
cat /click/uplinkstate/dhcpc_state_for_brain
```

This output uses `key=value` lines and is used to confirm `ip`, `router`, and `broadcast`. It is treated as a secondary source because firmware builds can vary in exact formatting.

### Applying the management address

```sh
printf '%s\n' 'ADDRESS PREFIX GATEWAY MTU BROADCAST VLAN' > /click/set_host_ip/run
```

Example:

```sh
printf '%s\n' '192.168.1.20 24 192.168.1.1 1500 192.168.1.255 1' \
  > /click/set_host_ip/run
```

Important: the second value is the numeric CIDR prefix length (`24`), not `255.255.255.0` and not the subnet address.

After a successful update, postmerkOS also attempts to update:

```text
/click/syslog_event_log/src_ip
/click/wan0_resolver/dst
/click/switch_port_table/have_network_connection
```

These secondary writes are nonfatal. The principal operation is `set_host_ip.run`.

## 5. Port status and PHY configuration

### Port status

```sh
cat /click/switch_port_table/dump_pports
```

The exact header depends on the binary module version. postmerkOS currently interprets the second and third data fields used by the known donor graph as link-established and speed values. Always inspect the header before writing a new parser.

Safe diagnostic workflow:

```sh
head -n 3 /click/switch_port_table/dump_pports
sed -n '1,10p' /click/switch_port_table/dump_pports
```

Do not assume that a field index from one donor version is portable to another.

### PHY changes

```sh
printf '%s\n' \
  'PORT 1, FC_OBEY false, EEE_ADV_ENABLED true, MODE aneg' \
  > /click/switch_port_table/set_port_phy_cfgs
```

Known mode translations used by `configd`:

| JSON | Click |
|---|---|
| `auto` | `aneg` |
| `10half` | `10hdx` |
| `10full` | `10fdx` |
| `100half` | `100hdx` |
| `100full` | `100fdx` |
| `1000full` | `1000fdx` |
| disabled port | `off` |

The full PHY command is regenerated whenever `enabled`, `speed`, `flow_control`, or `eee` changes so sibling values are not reset accidentally.

## 6. VLAN configuration

Per-port VLAN state is applied through:

```text
/click/switch_port_table/set_vlan_allports_conf
```

Example access port:

```sh
printf '%s\n' \
 'PORT 5, ALLOWED_VLANS 20, ALLOW_TAGGED_IN false, ALLOW_UNTAGGED_IN true, INGRESS_FILTER true, RADIUS_CAN_ASSIGN_VLAN false, PVID 20, UNTAGGED_VID 20' \
 > /click/switch_port_table/set_vlan_allports_conf
```

Example hybrid port:

```sh
printf '%s\n' \
 'PORT 24, ALLOWED_VLANS 10,20,30-40, ALLOW_TAGGED_IN true, ALLOW_UNTAGGED_IN true, INGRESS_FILTER true, RADIUS_CAN_ASSIGN_VLAN false, PVID 10, UNTAGGED_VID 10' \
 > /click/switch_port_table/set_vlan_allports_conf
```

The persistent schema distinguishes `access`, `trunk`, and `hybrid`. Allowed VLAN text is validated before it reaches Click.

## 7. Storm control

Write handler:

```text
/click/switch_port_table/set_port_storm_control
```

Example:

```sh
printf '%s\n' 'PORT 7, ENABLED true' \
  > /click/switch_port_table/set_port_storm_control
```

The binary `SwitchPortTable` used by this firmware does not expose a dependable matching dump handler. Therefore:

- `/etc/switch.json` is the authoritative desired state;
- `configd` replays storm control for every port at startup;
- UI and CLI clients display the saved desired value;
- status must not claim hardware verification.

This is intentionally different from readable link or DHCP state.

## 8. Spanning Tree Protocol

Global settings:

```sh
printf '%s\n' \
 'PRIORITY 32768, HELLO_TIME 2, FORWARD_DELAY 15, MAX_AGE 20, HOLDCOUNT 6' \
 > /click/stp/set_params
```

Per-port settings:

```sh
printf '%s\n' \
 'PORT aa:bb:cc:dd:ee:ff/5, ENABLED true, AUTOEDGE true, EDGE false, AUTOPTP true, PTP false, PRI 128, COST 0' \
 > /click/stp/set_many_port_cfgs
```

The port identifier contains the switch base MAC and logical port number. If the base MAC is unavailable, postmerkOS skips the STP write and returns a warning.

The current backend does not promise complete STP role/state readback for every graph version. Treat absent status fields as unavailable, not as disabled.

## 9. LACP

Global single-port LACP behavior is written to:

```text
/click/switch_port_table/enable_lacp_on_single_ports
```

```sh
printf '%s\n' true > /click/switch_port_table/enable_lacp_on_single_ports
```

The graph contains a complete `LACP` element, but postmerkOS currently exposes only this global policy. Creating and managing aggregation groups requires additional reverse engineering and should not be inferred from this one handler.

## 10. IGMP and MLD snooping

Enable/disable scripts:

```sh
printf '%s\n' true > /click/configure_igmp_snoop/run
printf '%s\n' true > /click/configure_mld_snoop/run
```

Intervals:

```text
/click/igmp_snoop/default_querier_interval_msec
/click/igmp_querier/query_interval
/click/mld_snoop/default_querier_interval_msec
/click/mld_querier/query_interval
```

The snoop table interval is written in milliseconds; the querier interval is written in seconds. `configd` performs both writes for each protocol.

## 11. PoE is not a Click subsystem

The Click graph contains a `__PORT_POE_MW__` template parameter and switch metadata, but actual power mode and enable control in postmerkOS is performed over I2C through the PD690xx library.

Persistent schema:

```json
{"poe":{"enabled":true,"mode":"af"}}
```

or:

```json
{"poe":{"enabled":true,"mode":"at"}}
```

PoE configuration is accepted only for copper ports marked PoE-capable by detected model. Non-PoE switches and SFP/SFP+ ports omit the object. Temporary controller absence produces warnings while retaining desired state.

## 12. Inspecting available handlers safely

List all graph elements:

```sh
find /click -mindepth 1 -maxdepth 1 -type d | sort
```

List handlers on an element:

```sh
find /click/switch_port_table -maxdepth 1 -type f -printf '%f\n' | sort
```

Read only before writing:

```sh
for file in /click/uplinkstate/*; do
  printf '\n== %s ==\n' "$file"
  cat "$file" 2>/dev/null || true
done
```

Some handlers are destructive or trigger scripts. Do not bulk-write guessed values. Capture the current graph, handler list, serial console, and a hardware recovery path before experimenting.

## 13. Interpreting failures

| Failure | Interpretation |
|---|---|
| `ENOENT` opening a handler | Element/handler is absent in this graph version |
| write succeeds but no state change | Command syntax may be accepted by VFS but rejected internally, or state may be write-only |
| configd warning | Desired state was valid, but runtime application/readback was incomplete |
| `Bad Request` | JSON or complete merged configuration failed validation; nothing was saved |
| empty status subsection | Data source unavailable; inspect `status.errors` |

Never convert a missing read handler into a daemon exit. The graph differs across models and donor versions, so best-effort partial data is a design requirement.

## 14. Persistent configuration versus live state

| Setting | Persistent desired source | Live readback |
|---|---|---|
| Management IPv4 mode/static values | `/etc/switch.json` | `network` status plus DHCP handlers |
| DHCP lease | No | `uplinkstate` handlers |
| PHY configuration | `/etc/switch.json` | partial Click dump handlers |
| VLAN configuration | `/etc/switch.json` | partial Click dump handlers |
| Storm control | `/etc/switch.json` | not reliably available |
| STP desired settings | `/etc/switch.json` | partial/version-dependent |
| PoE desired mode/enable | `/etc/switch.json` | PD690xx state when controller is available |
| Link speed/state | No | `dump_pports` |
| PoE power/temperature | No | PD690xx telemetry |

This distinction is the central rule for extending configd: configuration handlers express intent; status handlers report observation; neither should invent the other.
