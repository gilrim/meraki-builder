# Applying the MS42P build-workflow update

Two delivery formats are provided:

1. a complete ready-to-copy repository tree;
2. a Git patch generated against the uploaded `meraki-builder-config-status` tree.

## Before applying

```bash
cd /path/to/meraki-builder
git checkout ms42p-dev
git pull --ff-only origin ms42p-dev
git status
```

Commit or stash any local changes before continuing.

## Option A: apply the patch

Copy `ms42p-build-workflow.patch` into the repository, then run:

```bash
git apply --check ms42p-build-workflow.patch
git apply --index ms42p-build-workflow.patch
git diff --cached --check
```

Review the staged changes:

```bash
git diff --cached --stat
git diff --cached
```

## Option B: copy the complete updated tree

Extract the supplied archive outside the Git checkout, then synchronize it without touching `.git`:

```bash
rsync -a /path/to/extracted/meraki-builder/ ./
git add -A
git diff --cached --check
```

## Commit and push

```bash
git commit -m "Add integrated MS42P make-based firmware build"
git push origin ms42p-dev
```

## First validation after applying

```bash
make help
make doctor
bash -n scripts/*.sh
```

A complete build requires network access for any source or binary input that is not already cached:

```bash
make web
```

On CachyOS, accept the Ubuntu 22.04 distrobox route for the old OpenWrt toolchain and Linux 3.18 kernel build.
