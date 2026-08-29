#!/bin/sh
# brief-run.sh -- one scheduled brief. Written by schedule-brief.sh; edit
# freely, it is never regenerated over your changes without --force.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTDIR="${VIKI_BRIEF_DIR:-$HOME/.viki/brief}"
mkdir -p "$OUTDIR" || exit 1
STAMP=$(date -u +%Y-%m-%d)
OUT="$OUTDIR/$STAMP.txt"

# WHERE IT RUNS FROM decides what it can see: the ledger and the cache are
# relative to a checkout. VIKI_BRIEF_CWD names it; the repo root is the
# default because that is the tribe most agents are working in.
cd "${VIKI_BRIEF_CWD:-$ROOT}" 2>/dev/null || cd "$ROOT" || exit 1

# A FAILED BRIEF MUST LEAVE EVIDENCE, not an empty file. brief.sh's own header
# says silence is indistinguishable from a broken job; a scheduler makes that
# worse, because nobody is watching when it breaks.
{
    printf 'brief generated %s  (cwd %s)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(pwd)"
    printf '%s\n' "----------------------------------------------------------------"
    if sh "$ROOT/assistant/brief.sh" "$@" 2>&1; then
        :
    else
        rc=$?
        printf '\n*** THE BRIEF FAILED (exit %d). The lines above are all there is.\n' "$rc"
        printf '*** Do NOT read a short brief as a quiet day.\n'
    fi
} > "$OUT.tmp" 2>&1
mv -f "$OUT.tmp" "$OUT"
ln -sfn "$OUT" "$OUTDIR/latest.txt"
printf '%s\n' "$OUT"
