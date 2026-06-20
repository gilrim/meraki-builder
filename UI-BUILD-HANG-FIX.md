# postmerkOS UI build hang correction

## Root cause

The supplied `postmerkos-ui` `package-lock.json` contained 228 package tarball URLs
under the private host `packages.applied-caas-gateway1.internal.api.openai.org`.
That host is unavailable from a normal Ubuntu 22.04 Distrobox. `npm ci` therefore
entered network retry delays while its progress renderer displayed only a spinner.

## Corrections

- The companion UI repository lockfile now points to `registry.npmjs.org`.
- The UI repository includes a public-registry `.npmrc` with visible, bounded retries.
- The firmware builder uses a clean materialized UI source tree instead of modifying
  the selected git checkout.
- Recognized leaked OpenAI build-proxy URLs are safely rewritten in the materialized
  copy; unknown private/local registries are rejected.
- Dependency installation and Vite compilation have separate streamed logs and hard
  timeouts with explicit failure messages.
- A host regression test covers public, recognized leaked, and unsafe registry URLs.

The `Makefile` future timestamp warning is unrelated. It can be cleared after checking
the host clock with `touch Makefile`.
