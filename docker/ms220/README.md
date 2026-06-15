# Historical MS220 Docker workflow

The former Makefile in this directory downloaded a separate `meraki-builder` checkout and patched an external Buildroot tree. That path is no longer authoritative on `ms42p-dev`.

The Makefile is retained as a compatibility wrapper and delegates to the repository's top-level workflow:

```bash
make all
make base
make web
make distrobox
```

Run those commands from the repository root for clearer paths and logs. The legacy Dockerfile is retained only as historical reference; the supported compatibility environment is the Ubuntu 22.04 distrobox created by `make distrobox`.
