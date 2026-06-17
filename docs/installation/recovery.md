# Recovery

## Configuration reset

Holding the configured reset button for ten seconds invokes the JFFS2-only factory reset. Controllable copper/PoE LEDs show the countdown when supported. Releasing the button early cancels the operation.

A JFFS2 reset restores:

- switch configuration
- users, roles, passwords, and serial-authentication policy
- SSH keys and service policy
- firmware repository and time settings
- compatibility-notice acknowledgements

The read-only kernel and SquashFS firmware remain unchanged.

## Firmware recovery

If the system cannot boot or the normal updater cannot run:

1. Disconnect switch power and network/SFP connections.
2. Attach the SPI programmer.
3. Read the current flash for diagnosis.
4. Write a previously verified complete 16 MiB backup or validated postmerkOS image.
5. Verify the write before disconnecting the programmer.
6. Disconnect or electrically release the SPI programmer before applying switch power.

The research tools under `tools/research/nor` can split a flash dump for inspection. They are not part of the supported update process.
