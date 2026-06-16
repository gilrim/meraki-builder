# postmerkOS management CLI

`postmerkos-cli` is installed in every MS42/MS42P image and starts automatically for interactive SSH and TTL serial logins. Non-interactive SSH commands are not intercepted. Choose **Raw Linux shell** or run `postmerkos-cli shell` for maintenance work.

The CLI uses the same `configd` JSON validation and hardware application code as the browser.

Examples:

```sh
postmerkos-cli status
postmerkos-cli ports
postmerkos-cli port show 12
postmerkos-cli port set 12 poe.enabled true
postmerkos-cli port set 12 poe.mode '"at"'
postmerkos-cli set network.mode '"dhcp"'
postmerkos-cli config backup /tmp/switch.json
postmerkos-cli config restore /tmp/switch.json
postmerkos-cli firmware tftp
postmerkos-cli shell
```

Backups are plain JSON. This intentionally avoids an additional cryptography helper in the size-constrained 8 MiB SquashFS image.
