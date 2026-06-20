#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

validate_positive_integer() {
  local name="$1" value="$2"
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || die "$name must be a positive integer; got: $value"
}

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

run_ui_phase() {
  local phase="$1" timeout_seconds="$2" log_name="$3"
  shift 3
  local rc

  log "$phase (timeout: ${timeout_seconds}s; log: $LOG_DIR/$log_name.log)"
  set +e
  run_logged "$log_name" timeout --foreground --kill-after=15s "${timeout_seconds}s" "$@"
  rc=$?
  set -e

  if (( rc == 124 || rc == 137 )); then
    die "$phase timed out after ${timeout_seconds}s. Review $LOG_DIR/$log_name.log. Check DNS/Internet access and npm registry settings."
  fi
  if (( rc != 0 )); then
    die "$phase failed with status $rc. Review $LOG_DIR/$log_name.log."
  fi
}

validate_positive_integer UI_NPM_INSTALL_TIMEOUT "$UI_NPM_INSTALL_TIMEOUT"
validate_positive_integer UI_COMPILE_TIMEOUT "$UI_COMPILE_TIMEOUT"
validate_positive_integer UI_NPM_FETCH_TIMEOUT "$UI_NPM_FETCH_TIMEOUT"
validate_positive_integer UI_NPM_FETCH_RETRIES "$UI_NPM_FETCH_RETRIES"
need timeout
need python3
need rsync

clone_or_update_ref "$UI_REPO_URL" "$UI_DIR" "$UI_REF" "postmerkos-ui"
select_node

[[ -f "$UI_DIR/package.json" ]] || die "postmerkos-ui package.json is missing from $UI_DIR"
[[ -f "$UI_DIR/package-lock.json" ]] || die "postmerkos-ui package-lock.json is missing from $UI_DIR"

# Never modify the selected git checkout. Build a clean materialized copy so a
# known package-proxy URL can be repaired without making future source updates
# fail the dirty-tree guard.
log "Materializing clean postmerkos-ui build source"
rm -rf "$UI_BUILD_SOURCE_DIR"
mkdir -p "$UI_BUILD_SOURCE_DIR"
rsync -a --delete \
  --exclude '/.git/' \
  --exclude '/node_modules/' \
  --exclude '/build/' \
  "$UI_DIR/" "$UI_BUILD_SOURCE_DIR/"

python3 "$SCRIPT_DIR/sanitize-npm-lock.py" \
  "$UI_BUILD_SOURCE_DIR/package-lock.json" \
  --registry "$UI_NPM_REGISTRY"

log "Building postmerkos-ui from $UI_REF with Node $(node --version) and npm $(npm --version)"
(
  cd "$UI_BUILD_SOURCE_DIR"
  npm_environment=(
    env
    "npm_config_cache=$DOWNLOAD_DIR/npm-cache"
    "npm_config_registry=$UI_NPM_REGISTRY"
    "npm_config_replace_registry_host=always"
    "npm_config_progress=false"
    "npm_config_loglevel=http"
    "npm_config_audit=false"
    "npm_config_fund=false"
    "npm_config_fetch_retries=$UI_NPM_FETCH_RETRIES"
    "npm_config_fetch_retry_mintimeout=1000"
    "npm_config_fetch_retry_maxtimeout=10000"
    "npm_config_fetch_timeout=$UI_NPM_FETCH_TIMEOUT"
  )

  run_ui_phase \
    "Installing postmerkos-ui npm dependencies" \
    "$UI_NPM_INSTALL_TIMEOUT" \
    postmerkos-ui-npm-install \
    "${npm_environment[@]}" npm ci --no-audit --no-fund --foreground-scripts --prefer-offline

  run_ui_phase \
    "Compiling postmerkos-ui production assets" \
    "$UI_COMPILE_TIMEOUT" \
    postmerkos-ui-build \
    "${npm_environment[@]}" npm run build
)

[[ -f "$UI_BUILD_SOURCE_DIR/build/index.html" ]] || die "UI build did not produce build/index.html"
rm -rf "$BUILD_DIR/postmerkos-ui"
mkdir -p "$BUILD_DIR/postmerkos-ui"
rsync -a --delete "$UI_BUILD_SOURCE_DIR/build/" "$BUILD_DIR/postmerkos-ui/"

git -C "$UI_DIR" rev-parse HEAD > "$ARTIFACTS_DIR/ui-source-revision.txt"
find "$BUILD_DIR/postmerkos-ui" -type f -print0 | sort -z | xargs -0 sha256sum \
  > "$ARTIFACTS_DIR/ui-files.sha256"
touch "$STAMP_DIR/ui-built"
log "UI output is ready in $BUILD_DIR/postmerkos-ui"
