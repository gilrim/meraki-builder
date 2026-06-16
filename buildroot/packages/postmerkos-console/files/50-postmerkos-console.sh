# Launch the postmerkOS console only for interactive terminal logins.
case "$-" in
    *i*) ;;
    *) return 0 2>/dev/null || exit 0 ;;
esac
[ -t 0 ] && [ -t 1 ] || return 0 2>/dev/null || exit 0
[ "$(id -u)" -eq 0 ] || return 0 2>/dev/null || exit 0
[ "${POSTMERKOS_RAW_SHELL:-0}" = 1 ] && return 0 2>/dev/null || true
[ "${POSTMERKOS_NO_CONSOLE:-${POSTMERKOS_NO_CLI:-0}}" = 1 ] && return 0 2>/dev/null || true
command -v postmerkos-console >/dev/null 2>&1 || return 0 2>/dev/null || exit 0
postmerkos-console menu
exit $?
