# Adding a switch model

1. Add a hardware record with model, family, total/copper/uplink ports, PoE ports, ASIC instances, and initial `untested` state.
2. Add or confirm board detection and model strings.
3. Confirm switch graph and kernel-module selection.
4. Add front-panel rendering data and console paging fixtures.
5. Validate PoE controllers and LED behavior only when applicable.
6. Confirm flash geometry before normal updater use.
7. Boot with direct SPI and serial recovery available.
8. Submit a compatibility report and promote only the capabilities actually tested.

Do not mark a model known-incompatible without concrete architecture, geometry, or runtime evidence.
