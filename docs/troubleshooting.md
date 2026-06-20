# Troubleshooting

## postmerkos-ui npm install appears to hang

The UI dependency phase is expected to complete quickly on a warm cache. A terminal
spinner with no package output for several minutes normally indicates npm is retrying
an unreachable registry URL.

The June 20, 2026 UI lockfile incident was caused by `package-lock.json` entries that
referenced an internal build-service proxy instead of `registry.npmjs.org`. The build
now materializes the selected UI checkout into `.work/build/postmerkos-ui-source`,
rewrites only recognized leaked build-service proxy URLs to the configured public
registry, rejects private/local or unapproved package hosts, and leaves the git
checkout unchanged.

The dependency and Vite phases now stream separate logs:

- `.work/logs/postmerkos-ui-npm-install.log`
- `.work/logs/postmerkos-ui-build.log`

They are bounded by `UI_NPM_INSTALL_TIMEOUT=300` and `UI_COMPILE_TIMEOUT=120` seconds.
The registry can be changed with `UI_NPM_REGISTRY`; npm fetch retry and timeout values
can be changed with `UI_NPM_FETCH_RETRIES` and `UI_NPM_FETCH_TIMEOUT`.

To retry only the UI phase:

```sh
rm -rf .work/build/postmerkos-ui-source .work/build/postmerkos-ui
make ui
```

A warning that the top-level `Makefile` is dated in the future is a separate host clock
or ZIP timestamp issue. Correct the system clock and run `touch Makefile`; it is not the
cause of npm network retries.
