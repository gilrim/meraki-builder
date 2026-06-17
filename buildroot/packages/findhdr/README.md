# find_hdr

`find_hdr` is a small target-side C utility that locates gzip or XZ markers in firmware or MTD data and prints the byte offset. Startup module recovery uses it when binary switch modules must be extracted from an available donor partition.

```text
find_hdr -g FILE       locate gzip header
find_hdr -x FILE       locate XZ header
find_hdr -x -f FILE    locate XZ footer
```

The Buildroot package installs `/bin/find_hdr`. Host-side research implementations live under `tools/research/firmware-analysis` and are not part of the target package.
