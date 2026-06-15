#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"
need distrobox

if ! distrobox list 2>/dev/null | grep -qE "(^|[[:space:]])${DISTROBOX_NAME}([[:space:]]|$)"; then
  log "Creating distrobox $DISTROBOX_NAME from $DISTROBOX_IMAGE"
  distrobox create --name "$DISTROBOX_NAME" --image "$DISTROBOX_IMAGE" --yes
fi

quoted_root="$(printf '%q' "$REPO_ROOT")"
quoted_work="$(printf '%q' "$WORK_DIR")"
quoted_artifacts="$(printf '%q' "$ARTIFACTS_DIR")"
quoted_inputs="$(printf '%q' "$INPUTS_DIR")"
quoted_stamp="$(printf '%q' "$STAMP_DIR/container-deps")"
quoted_stamp_dir="$(printf '%q' "$STAMP_DIR")"
quoted_args=''
for arg in "$@"; do
  quoted_args+=" $(printf '%q' "$arg")"
done

distrobox enter "$DISTROBOX_NAME" -- bash -lc \
  "cd $quoted_root && if [ ! -f $quoted_stamp ]; then ./scripts/install-deps.sh && mkdir -p $quoted_stamp_dir && touch $quoted_stamp; fi; MS42P_IN_DISTROBOX=1 MS42P_WORK_DIR=$quoted_work MS42P_ARTIFACTS_DIR=$quoted_artifacts MS42P_INPUTS_DIR=$quoted_inputs JOBS=$(printf '%q' "$JOBS") $quoted_args"
