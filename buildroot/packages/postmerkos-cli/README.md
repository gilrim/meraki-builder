# postmerkOS management CLI

`postmerkos-cli` is the interactive and scriptable local management interface for postmerkOS. It is installed in every MS42/MS42P image and launches automatically for interactive SSH and hardware-TTL login sessions.

Non-interactive SSH commands are not intercepted. Set `POSTMERKOS_NO_CLI=1` before starting an interactive shell to suppress the menu, or choose **Raw Linux shell** from the menu. The host-side `tools/Flasher/firmware-flasher.sh` also detects the `postmerkOS>` prompt and enters the raw shell automatically.

## Commands

```sh
postmerkos-cli status
postmerkos-cli ports
postmerkos-cli port show 12
postmerkos-cli port set 12 poe.enabled true
postmerkos-cli port set 12 poe.mode '"at"'
postmerkos-cli set network.ipv4.mode '"dhcp"'
postmerkos-cli config show
postmerkos-cli config apply /tmp/delta.json
postmerkos-cli config replace /tmp/complete.json
postmerkos-cli config backup /tmp/switch-backup.enc
postmerkos-cli config restore /tmp/switch-backup.enc
postmerkos-cli firmware status
postmerkos-cli firmware local --overlay preserve /tmp/firmware.bin
postmerkos-cli firmware tftp --server 192.0.2.2 --file firmware.bin
postmerkos-cli firmware http
postmerkos-cli firmware sftp --server 192.0.2.2 --file firmware.bin
postmerkos-cli password
postmerkos-cli shell
```

The `set` and `port set` commands accept every field supported by the shared `configd` JSON schema. They are not gated by current link state, so administrative state, PHY, VLAN, STP, storm-control, and PoE desired state can be set while a port is disconnected.

## Backups

An empty password produces plain JSON. A password produces an OpenSSL-compatible `Salted__` AES-256-CBC file using PBKDF2-HMAC-SHA256 with 100,000 iterations. The browser UI uses the same format, so backups can move in either direction.

## Implementation notes

The CLI does not duplicate switch control logic. It invokes the one-shot `configd` commands, which use the same validation, persistent configuration, Click handlers, PD690xx controller interface, and updater package as the web interface.

## Additional web administrators

The web interface accepts `root` and members of the local `postmerkos-admin` group. From the raw shell:

```sh
addgroup postmerkos-admin 2>/dev/null || true
adduser switchadmin
addgroup switchadmin postmerkos-admin
```

The account then appears in the web password-management list.
