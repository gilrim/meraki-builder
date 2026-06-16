# Start the postmerkOS management interface for interactive login sessions.
case "$-" in
    *i*) ;;
    *) return 0 2>/dev/null || exit 0 ;;
esac
[ -t 0 ] && [ -t 1 ] || return 0 2>/dev/null || exit 0
[ "${POSTMERKOS_RAW_SHELL:-0}" = 1 ] && return 0 2>/dev/null || true
[ "${POSTMERKOS_NO_CLI:-0}" = 1 ] && return 0 2>/dev/null || true
command -v postmerkos-cli >/dev/null 2>&1 || return 0 2>/dev/null || exit 0
postmerkos-cli menu
exit $?
