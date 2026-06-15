# config_file module

`config_file.c` owns durable JSON storage.

## Files

- Main: `/etc/switch.json` by default.
- Temporary: `<path>.tmp`.
- Backup: `<path>.bak`.

## Save sequence

The JSON is written to a mode-0600 temporary file, flushed with `fsync`, the old main file is copied to the backup, and the temporary file is atomically renamed. On startup, an invalid main file may be recovered from a valid backup; otherwise defaults are reconstructed.

`config_file_mtime()` supports external-edit detection. `config_file_runtime_error()` exposes rejected external changes through status rather than crashing the daemon.
