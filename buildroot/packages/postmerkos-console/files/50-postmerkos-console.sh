# Launch postmerkOS management for interactive SSH/login shells belonging to a
# configured administrator, operator, or viewer. Serial auto-entry is handled
# by postmerkos-serial-login rather than this profile.
case "$-" in *i*) ;; *) return 0 2>/dev/null || exit 0 ;; esac
[ -t 0 ] && [ -t 1 ] || return 0 2>/dev/null || exit 0
[ "${POSTMERKOS_RAW_SHELL:-0}" = 1 ] && return 0 2>/dev/null || true
[ "${POSTMERKOS_NO_CONSOLE:-${POSTMERKOS_NO_CLI:-0}}" = 1 ] && return 0 2>/dev/null || true
command -v postmerkosctl >/dev/null 2>&1 || return 0 2>/dev/null || exit 0
role=$(postmerkosctl role 2>/dev/null || echo none)
[ "$role" != none ] || return 0 2>/dev/null || exit 0
postmerkos-console menu
exit $?
