# auth

`auth.c` authenticates management sessions through Linux PAM service `configd` and reads local account metadata through the standard password/group databases.

Only UID 0 or accounts in the `postmerkos-admin` group are authorized for switch management. This prevents an unrelated local login account from receiving the root-equivalent web terminal, firmware, or switch-configuration privileges.

Password changes re-authenticate the current session account and invoke BusyBox `passwd -a sha256`. Root may update another authorized account; other administrators may update only themselves. New passwords must be 8–128 characters and may not contain line breaks.

## Additional administrator accounts

The factory image authorizes `root`. To add a separate local administrator from the raw shell, create the management group if needed, create the account, and add it to the group:

```sh
addgroup postmerkos-admin 2>/dev/null || true
adduser switchadmin
addgroup switchadmin postmerkos-admin
```

Only UID 0 and members of this group are returned by the web account list or accepted by the management WebSocket.
