# Resolved issues

This file records behavior that is not part of current operation.

## Early kernel-module loading

Some releases required manual model correction before the Jaguar/Luton modules loaded correctly. Current hardware detection and module selection use explicit model/capability records.

## Link and uplink indication

Early images had incomplete link-light and MS320 uplink/PoE behavior. Current runtime uses the integrated Click/PD690xx paths and capability reporting; individual models may still be marked untested until reported.

## Static management addressing

Initial firmware provided DHCP only. Current configd supports DHCP/fallback and static IPv4 through console and web interfaces.

## Web and CLI prototypes

The original browser and flat command wrapper were replaced by the current configd-backed optional web UI and hierarchical role-aware console.

## PAM size regression

A PAM-based authentication experiment exceeded the 8 MiB SquashFS region. Current authentication uses standard Linux account/shadow data and lightweight group-based capabilities.

## Updater status and cached uploads

The updater now uses one tokenized candidate slot, explicit ready/acknowledge/start transitions, RAM status, direct serial output, and persistent reboot history.
