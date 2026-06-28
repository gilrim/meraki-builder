#!/bin/sh
set -eu

SOURCE=${CONFIGD_PORT_PROTOBUF:-/click/switch_port_table/switch_port_protobuf}
PORTS=${CONFIGD_PORTS_FILE:-/click/switch_port_table/dump_pports}
SAMPLES=${SAMPLES:-3}
DELAY=${DELAY:-2}
OUTPUT=${1:-./postmerkos-portstats-evidence-$(date -u +%Y%m%dT%H%M%SZ)}

case "$SAMPLES" in ''|*[!0-9]*) echo "SAMPLES must be a positive integer" >&2; exit 2;; esac
[ "$SAMPLES" -gt 0 ] || { echo "SAMPLES must be greater than zero" >&2; exit 2; }

mkdir -p "$OUTPUT"
umask 077
META="$OUTPUT/metadata.txt"
{
  echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "hostname=$(hostname 2>/dev/null || echo unknown)"
  echo "kernel=$(uname -a 2>/dev/null || echo unknown)"
  echo "protobuf_source=$SOURCE"
  echo "port_table_source=$PORTS"
  echo "samples=$SAMPLES"
  if [ -r /etc/postmerkos-release ]; then
    echo "--- /etc/postmerkos-release ---"
    cat /etc/postmerkos-release
  fi
} > "$META"

if [ ! -r "$SOURCE" ]; then
  echo "Counter handler is not readable: $SOURCE" | tee "$OUTPUT/ERROR.txt" >&2
  exit 1
fi

if [ -r "$PORTS" ]; then
  cat "$PORTS" > "$OUTPUT/dump_pports.txt"
else
  echo "Port table handler is not readable: $PORTS" >> "$META"
fi

index=1
while [ "$index" -le "$SAMPLES" ]; do
  sample=$(printf '%s/protobuf-%02d.bin' "$OUTPUT" "$index")
  cat "$SOURCE" > "$sample"
  bytes=$(wc -c < "$sample" | tr -d ' ')
  printf 'sample_%02d_bytes=%s\n' "$index" "$bytes" >> "$META"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$sample" >> "$OUTPUT/SHA256SUMS"
  fi
  od -An -tx1 -v "$sample" > "${sample%.bin}.hex"
  index=$((index + 1))
  [ "$index" -gt "$SAMPLES" ] || sleep "$DELAY"
done

printf 'Evidence captured in %s\n' "$OUTPUT"
printf 'Keep the raw .bin files; the .hex files are only for convenient review.\n'
