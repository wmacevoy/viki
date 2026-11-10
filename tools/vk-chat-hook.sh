#!/bin/sh
# vk-chat-hook.sh -- a Claude Code hook: the session's transcript into viki (REQ3/SCM-7).
#
# Off unless VIKI_CHAT_STORE is set. A conversation goes only into an encrypted
# diary, so VIKI_CHAT_KEYFILE is required with it. The hook's JSON (with
# transcript_path and session_id) arrives on stdin and is passed through.
#
# It exits 1, never 2, on failure: for a Stop hook, 2 blocks Claude from
# stopping and feeds the error back, which would loop.
[ -n "${VIKI_CHAT_STORE:-}" ] || exit 0
if [ -z "${VIKI_CHAT_KEYFILE:-}" ]; then
  echo "vk-chat-hook: VIKI_CHAT_STORE is set but VIKI_CHAT_KEYFILE is not; nothing ingested" >&2
  exit 1
fi
python3 "$(dirname "$0")/vk.py" --store "$VIKI_CHAT_STORE" --keyfile "$VIKI_CHAT_KEYFILE" \
  ingest-transcript --hook >/dev/null || { echo "vk-chat-hook: ingest failed" >&2; exit 1; }
exit 0
