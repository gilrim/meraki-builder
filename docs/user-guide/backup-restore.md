# Backup and restore

Configuration backups are plain restore-compatible JSON and exclude password hashes, SSH private keys, and other system secrets.

## Web

The browser can download a JSON backup, validate a selected file, and restore after administrator confirmation. Firmware updates recommend **Download Backup and Continue** before the candidate is staged.

## Console

The console can:

- upload a backup to a TFTP server
- download and validate a restore file from TFTP
- display the JSON between copy markers for manual terminal capture
- continue an update without backup after explicit confirmation

Temporary transfer files live in RAM and are removed after use. A local JFFS2 copy is not treated as an external pre-update backup.
