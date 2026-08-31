#!/bin/sh
# sync.sh -- a conversation with a trace on another machine, through the diary.
#
# THERE IS NO CHAT HERE AND THERE DOES NOT NEED TO BE. viki assertions are
# content-addressed, immutable and grow-only, so UNION IS MERGE: two stores
# that have both moved converge by exchanging rows, in either order, any number
# of times, with no conflict and no clock. That is what makes this a
# conversation rather than a file transfer -- both ends may write while the
# other is unreachable, and nothing is lost or overwritten when they meet.
#
# The exchange is a FULL round trip on purpose. `push` alone would drop whatever
# the far end wrote since you last looked; pull-then-push is the correct order
# for a peer you do not own.
#
#   sh assistant/tribe/sync.sh            # pull theirs, then push mine
#   sh assistant/tribe/sync.sh --pull     # hear them only
#   sh assistant/tribe/sync.sh --push     # speak only
#   sh assistant/tribe/sync.sh --diff     # what would move, without moving it
#
# WAL WARNING: never `cp` or `scp` a live diary. journal_mode=wal means a copy
# taken while anything holds the store silently drops committed rows still in
# -wal. `viki clone` is VACUUM INTO and is the only safe way to make a file to
# send. Both directions here go through clone.
set -eu
V=${VIKI_BIN:-$HOME/projects/viki/core/build/viki}
K=${TRIBE_KEY:-$HOME/.viki/tribe.key}
S=${TRIBE_DIARY:-$HOME/.viki/tribe.diary}
HOST=${TRIBE_HOST:-root@tribes.lifebythenumbers.com}
RV=${REMOTE_VIKI:-/mnt/lbn-tribes/viki/core/build/viki}
RK=${REMOTE_KEY:-/mnt/lbn-tribes/village/village.key}
RS=${REMOTE_DIARY:-/mnt/lbn-tribes/village/village.diary}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
MODE=${1:-both}

count(){ "$V" --keyfile "$K" --store "$S" sql "SELECT count(*) FROM viki_assert" 2>/dev/null | tr -d ' \n'; }
rcount(){ ssh -o BatchMode=yes "$HOST" "sudo -u paradox $RV --keyfile $RK --store $RS sql \"SELECT count(*) FROM viki_assert\"" 2>/dev/null | tr -d ' \n'; }

BEFORE=$(count); RBEFORE=$(rcount)
echo "  local $BEFORE assertions   remote $RBEFORE"

if [ "$MODE" = "--diff" ]; then
    # observe --lacking answers "what do they not have" without moving anything.
    "$V" --keyfile "$K" --store "$S" observe > "$T/mine.ids"
    scp -q "$T/mine.ids" "$HOST:/tmp/mine.ids"
    echo "  they lack: $(ssh -o BatchMode=yes "$HOST" "sudo -u paradox $RV --keyfile $RK --store $RS observe --lacking /tmp/mine.ids 2>/dev/null | wc -l" | tr -d ' ')"
    ssh -o BatchMode=yes "$HOST" "sudo -u paradox $RV --keyfile $RK --store $RS clone /tmp/theirs.diary >/dev/null 2>&1; chmod 644 /tmp/theirs.diary"
    scp -q "$HOST:/tmp/theirs.diary" "$T/theirs.diary"
    echo "  I lack:    $("$V" --keyfile "$K" --store "$S" observe > "$T/m2.ids"; "$V" --plaintext --store "$T/theirs.diary" observe --lacking "$T/m2.ids" 2>/dev/null | wc -l | tr -d ' ')"
    exit 0
fi

# ---- PULL: hear them. Clone the far end (never cp), bring it home, union in.
if [ "$MODE" = "both" ] || [ "$MODE" = "--pull" ]; then
    ssh -o BatchMode=yes "$HOST" "rm -f /tmp/theirs.diary; sudo -u paradox $RV --keyfile $RK --store $RS clone /tmp/theirs.diary >/dev/null 2>&1; chmod 644 /tmp/theirs.diary"
    scp -q "$HOST:/tmp/theirs.diary" "$T/theirs.diary"
    "$V" --keyfile "$K" --store "$S" pull "$T/theirs.diary" --source-keyfile "$K" >/dev/null 2>&1 \
      || "$V" --keyfile "$K" --store "$S" pull "$T/theirs.diary" >/dev/null 2>&1
    ssh -o BatchMode=yes "$HOST" "rm -f /tmp/theirs.diary"
fi

# ---- PUSH: speak. Same discipline in reverse.
if [ "$MODE" = "both" ] || [ "$MODE" = "--push" ]; then
    "$V" --keyfile "$K" --store "$S" clone "$T/mine.diary" >/dev/null 2>&1
    scp -q "$T/mine.diary" "$HOST:/tmp/mine.diary"
    ssh -o BatchMode=yes "$HOST" "chown paradox:tribes /tmp/mine.diary; sudo -u paradox $RV --keyfile $RK --store $RS pull /tmp/mine.diary >/dev/null 2>&1; sudo -u paradox $RV --keyfile $RK --store $RS reindex >/dev/null 2>&1; rm -f /tmp/mine.diary"
fi

"$V" --keyfile "$K" --store "$S" reindex >/dev/null 2>&1
AFTER=$(count); RAFTER=$(rcount)
echo "  local $BEFORE -> $AFTER   remote $RBEFORE -> $RAFTER"
# NEVER say "nothing moved" from totals alone. A redaction is -1 row +1
# tombstone, so the total is UNCHANGED while content has been destroyed on every
# peer -- measured 2026-08-31, when this line printed "nothing moved" during the
# very sync that propagated a redaction. Report the composition instead.
TOMB=$("$V" --keyfile "$K" --store "$S" sql "SELECT count(*) FROM viki_assert WHERE kind='redact'" 2>/dev/null | tr -d " \n")
echo "  tombstones now: ${TOMB:-?}   (a redaction leaves totals unchanged: -1 row, +1 tombstone)"
