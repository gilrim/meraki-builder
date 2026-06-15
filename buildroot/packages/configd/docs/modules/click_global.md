# click_global module

`click_global.c` handles global desired settings.

- Bridge STP timers and priority → `/click/stp/set_params`.
- LACP on individual ports → `/click/switch_port_table/enable_lacp_on_single_ports`.
- IGMP/MLD snooping and querier intervals → Click scripts and element handlers.

Missing read handlers produce defaults and warnings. Writes return warnings rather than terminating the daemon.
