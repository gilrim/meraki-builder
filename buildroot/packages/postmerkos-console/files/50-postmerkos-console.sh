# Launch postmerkOS management for interactive SSH/login shells belonging to a
# configured administrator, operator, or viewer. Serial direct-entry is handled
# by postmerkos-serial-login rather than this profile.
case "$-" in *i*) ;; *) return 0 2>/dev/null || exit 0 ;; esac
[ -t 0 ] && [ -t 1 ] || return 0 2>/dev/null || exit 0
[ "${POSTMERKOS_RAW_SHELL:-0}" = 1 ] && return 0 2>/dev/null || true
[ "${POSTMERKOS_NO_CONSOLE:-${POSTMERKOS_NO_CLI:-0}}" = 1 ] && return 0 2>/dev/null || true

if ! command -v postmerkosctl >/dev/null 2>&1; then
    [ "$(id -u 2>/dev/null)" = 0 ] && {
        echo 'WARNING: configd is unavailable; entering the root recovery shell.'
        return 0 2>/dev/null || exit 0
    }
    echo 'postmerkOS management is unavailable.' >&2
    exit 1
fi

role=$(postmerkosctl role 2>/dev/null || echo none)
if [ "$role" = none ]; then
    if [ "$(id -u 2>/dev/null)" = 0 ]; then
        echo 'WARNING: management role lookup failed; entering the root recovery shell.'
        return 0 2>/dev/null || exit 0
    fi
    echo 'This account has no postmerkOS management role.' >&2
    exit 1
fi

postmerkos-console menu
exit $?
