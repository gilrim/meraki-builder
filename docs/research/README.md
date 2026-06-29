# Research material

Raw-image utilities live under `tools/research`:

- NAND/UBI extraction and reconstruction
- NOR layout extraction and manual reconstruction
- compression-header analysis

These utilities are intentionally separated from supported build/install paths. See their local READMEs before operating on flash dumps.

## Imported platform insight

The applied platform-boundary findings from the supplied OpenVTSS v0.8 archive are documented in [OpenVTSS runtime boundaries](openvtss-runtime-boundaries.md). The exact MS42P copper-boundary, SFP, and probe-registration evidence is kept separately in [MS42P port-map and SFP runtime evidence](ms42p-port-map-and-sfp-evidence.md).

The OpenVTSS archive is research reference material, not a firmware build input. Operating procedures remain under [`docs/user-guide/`](../user-guide/) and contain only the conclusions needed to administer the current firmware.
