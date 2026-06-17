# Firmware updater architecture

Fwupdate separates transport, candidate validation, flash operations, logging, and reboot finalization.

Candidate sources include local files, TFTP, HTTP/HTTPS, SFTP, and browser upload. All sources feed the same checksum, metadata, hardware, version, partition, and overlay validation.

A browser upload has a unique token and one active slot. Starting another upload cancels and removes the previous candidate. Verification produces a ready state; a separate acknowledged command begins the write.

The updater writes RAM status/logs, direct serial messages, capability-aware LED progress, and a bounded persistent record. Post-boot finalization compares the installed release identity with the pending record and publishes success, interruption, or failure.

See the [administrator guide](../user-guide/firmware-updates.md) for workflow and the package documentation for command options.
