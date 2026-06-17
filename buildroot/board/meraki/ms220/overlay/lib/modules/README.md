# Vitesse switch kernel modules

This directory is populated during the integrated build with binary modules required by the vendor Linux 3.18.123 switch platform:

- `elts_meraki.ko`
- `merakiclick.ko`
- `proclikefs.ko`
- family-specific `vc_click.ko`
- family-specific `vtss_core.ko`

Family directories are selected from detected Luton, Jaguar, or dual-Jaguar hardware. The modules must match the running kernel ABI. The build verifies and stages donor inputs before creating the root filesystem; source archives do not need to contain generated module binaries.
