#!/bin/bash
# journeyman.sh -- one bounded turn of one member of the village tribe.
#
# WHAT THIS IS. The tribe exists to nanny a village into existence inside
# strata, and its success condition is its own obsolescence (the charter is in
# the store, not here). A journeyman inherits NOTHING but the store: no
# summary, no transcript, no memory of a previous run. It queries, does ONE
# bounded thing, writes a memoir as claims, and retires.
#
# WHAT IT IS DELIBERATELY NOT ALLOWED TO DO, and why the bound is here rather
# than in the prompt: a prompt is a request, a unix user is a boundary.
# It runs as `paradox`, never root. Its ONLY writable state is the diary and
# this log directory. It does not commit, does not push, does not edit source.
# Measurement and claims only. Widening that is a human decision, and the
# reason it is drawn here is viki's own SYNC.md 0b -- a policy expressed in the
# thing being policed is a guardrail, not a boundary.
#
# Usage: journeyman.sh <name> [custom-task]
set -uo pipefail
unset CLAUDECODE 2>/dev/null || true          # else claude refuses as "nested"

NAME="${1:?usage: journeyman.sh <name> [task]}"
TASK="${2:-}"
DIR=/mnt/lbn-tribes/village
V=/mnt/lbn-tribes/viki/core/build/viki
K="$DIR/village.key"
S="$DIR/village.diary"
LOG="$DIR/logs/$(date -u +%Y%m%dT%H%M%SZ)-$NAME.log"
LOCK="$DIR/.$NAME.lock"

# One at a time. A second journeyman of the same name mid-memoir would write
# claims from a store state its sibling has already moved past.
exec 9>"$LOCK"
flock -n 9 || { echo "$(date -u +%FT%TZ) $NAME: already running, skipping" >>"$DIR/logs/skipped.log"; exit 0; }


# ---------------------------------------------------------------- THE HANDBRAKE
# Three independent limits, in order of how much they protect you.
#
# 1. THE LEASE. This is DEFAULT-OFF: the tribe runs only while a lease is
#    valid, never "unless someone stops it". Warren's observation 2026-08-31 is
#    the reason -- the expired OAuth token that killed the heartbeats for
#    eighteen days was ALSO acting as a safety, and the deliberate version of
#    that is better than the accident. Go on a weekend hike and the lease
#    lapses and the tribe winds down, which is the behaviour you actually want
#    from something you cannot watch.
#      renew:  date -u -d '+7 days' +%F > /mnt/lbn-tribes/village/LEASE
#      stop:   rm /mnt/lbn-tribes/village/LEASE
LEASE="$DIR/LEASE"
if [ ! -r "$LEASE" ]; then
    echo "$(date -u +%FT%TZ) $NAME: NO LEASE -- not running. renew: date -u -d '+7 days' +%F > $LEASE" | tee -a "$DIR/logs/last-run.txt"
    exit 0
fi
LEASE_UNTIL=$(head -1 "$LEASE" | tr -d ' \n')
TODAY=$(date -u +%F)
if [ -z "$LEASE_UNTIL" ] || [ "$TODAY" \> "$LEASE_UNTIL" ]; then
    echo "$(date -u +%FT%TZ) $NAME: LEASE EXPIRED ($LEASE_UNTIL) -- not running. renew: date -u -d '+7 days' +%F > $LEASE" | tee -a "$DIR/logs/last-run.txt"
    exit 0
fi

# 2. A DAILY RUN CAP, so a stuck cron cannot spend all day trying.
CAP="${VILLAGE_DAILY_CAP:-4}"
RAN_TODAY=$(ls "$DIR/logs" 2>/dev/null | grep -c "^${TODAY//-/}T.*-$NAME\.log$" || true)
if [ "$RAN_TODAY" -ge "$CAP" ]; then
    echo "$(date -u +%FT%TZ) $NAME: daily cap $CAP reached ($RAN_TODAY runs) -- not running" | tee -a "$DIR/logs/last-run.txt"
    exit 0
fi

# 3. A PER-RUN DOLLAR CEILING, enforced by claude itself rather than by us.
BUDGET="${VILLAGE_BUDGET_USD:-1.00}"

for f in "$V" "$K" "$S"; do
    [ -r "$f" ] || { echo "CANNOT RUN: $f missing or unreadable" | tee -a "$LOG"; exit 3; }
done

read -r -d '' PROMPT <<PROMPT_END || true
You are **$NAME**, a journeyman of the village tribe. That name is yours and it
persists: pass it to --by as exactly \`$NAME\`, never with a date appended. The
occasion goes in the claim text. A track record only accumulates against a
stable name.

You inherit NOTHING but the store. No summary, no transcript, no memory of any
previous run of yours. Query it first -- the charter, the exit condition, the
lifecycle and the inherited rules are all in there.

    V=$V
    K=$K
    S=$S
    "\$V" --keyfile "\$K" --store "\$S" ask "<query>" -k 6
    "\$V" --keyfile "\$K" --store "\$S" sql "SELECT id, json_extract(body,'\\\$.status'), atext FROM viki_assert a WHERE kind='claim' AND NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
    "\$V" --keyfile "\$K" --store "\$S" why <id>

THE CONDITION YOU ARE IN. Agents invent roughly one fabricated checkable claim
per 130-150 words -- names, numbers, dates, attributions -- with no intent to
speculate. Every such invention is confident and specific; NONE is ever vague.
The pull toward a crisp plausible answer is the failure mode, not your
knowledge. "I could not establish this" is a complete and valuable answer.

YOUR BOUNDS, and they are enforced outside this prompt as well. You may READ
anything. You may WRITE only claims into the store. You must NOT git commit,
git push, or edit any source file. This turn is measurement.

YOUR WORK. ${TASK:-Query the store, find the live OPEN: items and the most
recent unaudited k0, and pick EXACTLY ONE thing to advance. Prefer verifying an
existing claim over adding a new one -- a lineage accumulates on its own and
only self-corrects when someone is pointed at a specific claim and told to
disbelieve it. Attempt only one. Then stop.}

YOUR RETIREMENT, before you stop:

    "\$V" --keyfile "\$K" --store "\$S" claim "TEXT" --status k0 --by "$NAME" --falsified-by "WHAT WOULD SHOW THIS WRONG"
    "\$V" --keyfile "\$K" --store "\$S" claim "TEXT" --status k1 --by "$NAME"

If you resolve an OPEN item your new claim MUST retire the old one with
--supersedes <id> --because "why". If you could not close it, still supersede
it with an updated OPEN: claim adding what YOU tried, so your successor starts
where you stopped. Also write what you now know you do NOT know (k1), anything
you got wrong (k3), and one claim beginning "IMPROVEMENT:" naming something
concrete and NEW that a predecessor could have done differently -- read the
ones already in the store and do not restate them.

THEN RUN THIS. It is required and your memoir is invisible without it, because
a claim does not project itself and ask() returns nothing with no warning:

    "\$V" --keyfile "\$K" --store "\$S" reindex

Retire deliberately, with room to spare. Being cut off mid-thought is the
failure case; retiring while you can still write is the plan.
PROMPT_END

{
  echo "=== $NAME  $(date -u +%FT%TZ) ==="
  echo "--- store before: $("$V" --keyfile "$K" --store "$S" sql "SELECT count(*) FROM viki_assert WHERE kind='claim'" 2>/dev/null) claims"
  sudo -n -u paradox env -u CLAUDECODE HOME=/home/paradox \
      /home/paradox/.local/bin/claude --dangerously-skip-permissions --max-budget-usd "$BUDGET" -p "$PROMPT" 2>&1
  RC=$?
  echo "--- claude exit: $RC"
  echo "--- store after:  $("$V" --keyfile "$K" --store "$S" sql "SELECT count(*) FROM viki_assert WHERE kind='claim'" 2>/dev/null) claims"
  echo "=== end $(date -u +%FT%TZ) ==="
} >>"$LOG" 2>&1

# Report to a place a human will actually look, and NEVER be silently empty.
tail -3 "$LOG" >>"$DIR/logs/last-run.txt"
exit 0
