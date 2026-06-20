#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

select_node() {
  local major=0 machine node_arch archive tools
  if command -v node >/dev/null 2>&1; then
    major="$(node -p 'process.versions.node.split(".")[0]' 2>/dev/null || printf '0')"
  fi
  if [[ "$major" =~ ^[0-9]+$ ]] && (( major >= 20 )) && command -v npm >/dev/null 2>&1; then
    return
  fi

  machine="$(uname -m)"
  case "$machine" in
    x86_64|amd64) node_arch=x64 ;;
    aarch64|arm64) node_arch=arm64 ;;
    *) die "No Node.js >=20 is installed and portable Node is unsupported on $machine" ;;
  esac

  tools="$BUILD_DIR/tools"
  archive="$tools/node-v$NODE_VERSION-linux-$node_arch.tar.xz"
  mkdir -p "$tools"
  if [[ ! -f "$archive" ]]; then
    log "Downloading portable Node.js $NODE_VERSION"
    download_file \
      "https://nodejs.org/dist/v$NODE_VERSION/node-v$NODE_VERSION-linux-$node_arch.tar.xz" \
      "$archive"
  fi
  if [[ ! -x "$tools/node-v$NODE_VERSION-linux-$node_arch/bin/node" ]]; then
    tar -C "$tools" -xJf "$archive"
  fi
  export PATH="$tools/node-v$NODE_VERSION-linux-$node_arch/bin:$PATH"
  need node
  need npm
}

clone_or_update_ref "$UI_REPO_URL" "$UI_DIR" "$UI_REF" "postmerkos-ui"
select_node

log "Building postmerkos-ui from $UI_REF"
(
  cd "$UI_DIR"
  npm_config_cache="$DOWNLOAD_DIR/npm-cache" npm ci --no-audit --no-fund
  npm run build
)

[[ -f "$UI_DIR/build/index.html" ]] || die "UI build did not produce build/index.html"
rm -rf "$BUILD_DIR/postmerkos-ui"
mkdir -p "$BUILD_DIR/postmerkos-ui"
rsync -a --delete "$UI_DIR/build/" "$BUILD_DIR/postmerkos-ui/"

git -C "$UI_DIR" rev-parse HEAD > "$ARTIFACTS_DIR/ui-source-revision.txt"
find "$BUILD_DIR/postmerkos-ui" -type f -print0 | sort -z | xargs -0 sha256sum \
  > "$ARTIFACTS_DIR/ui-files.sha256"
touch "$STAMP_DIR/ui-built"
log "UI output is ready in $BUILD_DIR/postmerkos-ui"
