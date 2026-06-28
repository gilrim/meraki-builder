#!/usr/bin/env python3
"""Structural guards for security/availability contracts that require libwebsockets."""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "websocket.c").read_text(encoding="utf-8")

finish_start = source.index('if (!strcmp(type, "firmware_upload_finish"))')
finish_end = source.index('if (!strcmp(type, "firmware_begin_flash"))', finish_start)
finish = source[finish_start:finish_end]
assert "firmware_job_start(session" in finish, "firmware finish must hand off to async validation"
assert "firmware_validate_update(" not in finish, "firmware finish must not validate synchronously"

privileged_gate = source.index("if (!revalidate_session(wsi, session, request_id)) return 0;")
first_privileged = source.index('if (!strcmp(type, "get_auth"))')
assert privileged_gate < first_privileged, "live token/role revalidation must precede privileged dispatch"

revoke_start = source.index("void ws_revoke_user_sessions")
revoke_end = source.index("static int replace_configuration", revoke_start)
revoke = source[revoke_start:revoke_end]
assert "session_revoke_user(username)" in revoke
assert "firmware_job_cancel()" in revoke, "revocation must stop staged firmware work"
assert "deauthenticate_session" in revoke

assert "usleep(" not in source, "authentication callbacks must never sleep the event loop"
assert "lws_get_peer_simple" in source, "rate limiting must include the peer address"
assert 'auth_record_failure_key("global"' in source, "random usernames must not bypass throttling"
assert "firmware_job_poll();" in source, "async validation jobs must be reaped by the service loop"

print("management-plane structural contracts passed")
