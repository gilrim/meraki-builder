# OpenVTSS findings applied to postmerkOS tooling

The supplied OpenVTSS v0.8 reverse-engineering and runtime-validation material is used as a platform-boundary check. It does not replace the donor modules required by the current firmware. Exact test-run evidence is isolated in [MS42P port-map and SFP runtime evidence](ms42p-port-map-and-sfp-evidence.md) so this page can remain focused on integration rules.

## MS42/MS42P naming

meraki-redboot recovery classifies MS42/MS42P as `jaguar1` because pre-kernel SPI access uses the Jaguar1 software-mode register at `0x70000068`. The Linux switching stack classifies the same hardware as `jaguar_dual`: two Jaguar1 switch devices joined by a VStaX fabric. These identifiers belong to different layers and must not be collapsed into one name.

The release manifest stores recovery-family data independently from the runtime module profile. Flasher model selection uses `jaguar1`; boot module selection remains `jaguar_dual` for MS42/MS42P.

## Runtime module order

The recovered linkage confirms this dependency direction:

```text
vtss_core.ko -> merakiclick.ko -> elts_meraki.ko / vc_click.ko
```

The runtime keeps `vtss_core` first and `vc_click` last. MS42/MS42P use the dual-chip VTSS/Click objects and their 52-port board profile.

## Current hardware boundary

OpenVTSS hardware runs now establish all of the following on MS42P:

- both Jaguar1 ASIC identities and the primary PI-master enable needed to access chip 1;
- the active-low chassis reset input and green/orange chassis indicator GPIOs;
- representative copper mappings at the first, middle-boundary, and last ports of each 12-port bank on both ASICs;
- the logical and chip-local mapping of SFP ports 49 through 52;
- insertion/removal detection on every SFP cage;
- pre-initialization registration of five reset/customization probes at `symbol+8`.

These facts tighten model profiles and diagnostics. They do not prove the remaining VStaX fabric topology, every individual copper-port mapping, SFP module compatibility, optical-control polarity, PCS lock, SFP LED behavior, or a complete open replacement for the proprietary switching stack.

## Donor-module boundary

The donor image remains the authoritative binary source until open replacements cover register access, MIIM/PHY, FDMA, interrupts, GPIO/LED/reset, dual-chip VStaX topology, SFP/10G PHY handling, and the Click-facing controller contract. Hardware evidence may validate a mapping or call boundary without making the corresponding proprietary execution body replaceable.

## Integration rule

Bootloader and recovery changes may validate image format, flash geometry, model identity, and safe SPI access. They must not silently change Linux module-family selection, logical port count, Click graph assumptions, port numbering, or proprietary module load order.
