# Security, roles, sessions, and SSH keys

## Identity and roles

postmerkOS uses Linux account and group data without PAM. Membership in `postmerkos-admin`, `postmerkos-operator`, or `postmerkos-viewer` determines the role. Root is always an administrator and cannot be deleted or demoted. Configd resolves roles from owned copies of passwd/group data so nested libc lookups cannot corrupt session identity.

## Authorization boundary

Every local-socket and WebSocket operation checks a named capability in configd. Hidden or disabled browser controls are convenience only and are not the security boundary. The current capability matrix is documented in the [accounts and roles guide](../user-guide/accounts-roles.md).

## Web sessions

A browser connection performs WebSocket subprotocol negotiation, protocol `hello`, authentication, and token/session binding. Before each privileged request, configd revalidates the token, expiration, account existence, and current role. Password changes, account deletion, and role changes revoke affected tokens and invalidate connected sessions rather than allowing cached authority to survive.

Authentication throttling is global and monotonic rather than tied only to one socket, so reconnecting does not reset repeated-failure protection. Throttling does not sleep in configd’s event loop.

## Local sessions

The Unix socket uses `SO_PEERCRED` to bind the process UID to a role-aware session. Requests use bounded newline-framed JSON with complete writes, complete-line reads, timeouts, and SIGPIPE suppression.

## SSH public keys

Configd accepts supported SSH public-key records only after:

1. validating the key type and token structure;
2. decoding Base64 strictly;
3. validating the SSH wire-format type field and lengths;
4. rejecting trailing or malformed binary data;
5. calculating the displayed fingerprint from the decoded key.

Adding or removing a key is transactional. Configd renders the candidate `authorized_keys`, writes it atomically, and commits the matching configuration only when both representations succeed. Failure restores the previous configuration and active authorized-key file, preventing removed keys from remaining active behind an apparently successful configuration change.

Private keys are never accepted, stored, backed up, or returned by the management API.
