#!/bin/sh
# kin-orient.sh -- what a trace needs at a seam that the compaction template drops.
#
# WIRED TO SessionStart AND PreCompact. PreCompact's stdout is documented as
# "critical information to preserve", so this block survives the boundary
# verbatim instead of being summarised.
#
# WHY IT EXISTS, from one session's measurements (2026-08-29/30):
#
#   - That session crossed FIVE compaction boundaries and dropped 4,521,748
#     tokens. The trace believed there had been ONE. You cannot feel a seam,
#     so this prints the count.
#   - The compaction summary is stored as `type: "user"` with `model: null`,
#     so a trace's own conclusions reach its successor wearing the human's
#     voice. That is the mechanism behind "I decided X" said about a thing
#     the human decided.
#   - The template has slots for intent, files, errors and pending tasks, and
#     NO slot for what a trace was uncertain about or what it later found
#     false. So corrections do not cross. This prints them FIRST.
#   - A fresh trace found that its auto-loaded CLAUDE.md was one commit stale
#     and missing the section that reoriented the whole project. Had it
#     trusted its context it would have worked in the dead-end tree. So this
#     prints whether the file on disk has moved since HEAD.
#
# CONSTRAINTS. It is READ-ONLY -- a hook that writes during compaction is a
# hook that corrupts state at the worst possible moment. It is bounded, because
# a block that eats the context it protects is self-defeating. And it never
# prints nothing: an unreadable diary says so, because "no corrections" and
# "I could not look" must not render the same.

set -u
V="${VIKI_BIN:-$HOME/projects/viki/core/build/viki}"
KIN="${VIKI_KIN_DIARY:-$HOME/.viki/kin.diary}"
KEY="${VIKI_KIN_KEY:-$HOME/.viki/kin.key}"
MAX_CLAIMS="${KIN_MAX_CLAIMS:-6}"

printf '=== KIN ORIENTATION (hook, not a message from the user) ===\n'

# ---- 1. how many seams are behind you -------------------------------------
SID="${CLAUDE_SESSION_ID:-}"
PROJ="$HOME/.claude/projects"
JSONL=""
if [ -n "$SID" ]; then
    JSONL=$(find "$PROJ" -name "${SID}.jsonl" -maxdepth 2 2>/dev/null | head -1)
else
    JSONL=$(ls -t "$PROJ"/*/*.jsonl 2>/dev/null | head -1)
fi
if [ -n "$JSONL" ] && [ -r "$JSONL" ]; then
    N=$(grep -c '"subtype":"compact_boundary"' "$JSONL" 2>/dev/null || echo 0)
    D=$(grep -o '"cumulativeDroppedTokens":[0-9]*' "$JSONL" 2>/dev/null | tail -1 | cut -d: -f2)
    printf 'SEAMS: %s compaction boundar%s already in this session' \
           "$N" "$( [ "$N" = "1" ] && echo y || echo ies )"
    [ -n "${D:-}" ] && printf ', %s tokens dropped' "$D"
    printf '\n  You cannot feel these. What you have as summary vs. as context is\n'
    printf '  invisible from inside; say which when it matters.\n'
else
    printf 'SEAMS: could not read this session transcript -- NOT the same as zero.\n'
fi

# ---- 2. what a previous trace got WRONG, correction first -----------------
printf '\nCORRECTIONS a previous trace recorded (newest first):\n'
if [ ! -x "$V" ]; then
    printf '  no viki-core binary at %s -- corrections NOT checked.\n' "$V"
elif [ ! -f "$KIN" ]; then
    printf '  no kin diary at %s -- nothing recorded yet, or wrong path.\n' "$KIN"
else
    OUT=$("$V" --keyfile "$KEY" --store "$KIN" sql \
      "SELECT '  ['||coalesce(json_extract(body,'\$.status'),'??')||'] '
              ||substr(coalesce(json_extract(body,'\$.text'),''),1,150)
              ||'  --'||substr(coalesce(json_extract(body,'\$.by'),'?'),1,28)
         FROM viki_assert a
        WHERE a.kind='claim'
          AND NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)
        ORDER BY a.ts DESC LIMIT $MAX_CLAIMS" 2>/dev/null)
    if [ -n "$OUT" ]; then
        printf '%s\n' "$OUT"
        printf '  (LIVE claims only -- anything superseded has left. Walk any of them:\n'
        printf '   %s --keyfile %s --store %s why <id>)\n' "$V" "$KEY" "$KIN"
    else
        printf '  diary unreadable or empty -- could not check. Not the same as "none".\n'
    fi
fi

# ---- 3. is your steering document stale? ----------------------------------
PD="${CLAUDE_PROJECT_DIR:-$PWD}"
if [ -f "$PD/CLAUDE.md" ] && git -C "$PD" rev-parse --git-dir >/dev/null 2>&1; then
    if ! git -C "$PD" diff --quiet HEAD -- CLAUDE.md 2>/dev/null; then
        printf '\nCLAUDE.md ON DISK DIFFERS FROM HEAD -- uncommitted edits.\n'
    fi
    LAST=$(git -C "$PD" log -1 --format='%h %ad' --date=short -- CLAUDE.md 2>/dev/null)
    printf '\nSTEERING: CLAUDE.md last changed %s.\n' "${LAST:-unknown}"
    printf '  The copy in your context is a SNAPSHOT from when it was loaded and does\n'
    printf '  NOT update when the file changes. If this session edited it, your copy is\n'
    printf '  behind. Re-read the file before trusting your context about direction.\n'
fi
printf '=== END KIN ORIENTATION ===\n'
