#!/bin/sh
# brief.sh -- the morning brief.  NOT VIKI.  See assistant/README.md.
#
# Composes viki's primitives into a decision. Everything judgmental lives here
# and nowhere in src/: what counts as stale, what is worth waking someone about,
# what to ask. viki supplies facts; this file has the opinions.
#
# THE GOOD MORNING IS SAID OUT LOUD. A day with nothing at risk prints an
# explicit "nothing due" -- silence is indistinguishable from a broken cron job,
# and a brief that is sometimes absent is one nobody comes to rely on.
#
# QUESTIONS ARE BATCHED HERE AND NEVER PUSHED. An assistant that interrupts to
# resolve its own uncertainty has taken the problem and handed it back
# (VIKIVERSE_V1 Q6).
#
# Usage: sh assistant/brief.sh [--me NAME] [--horizon 2d] [--stale-after 1]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
ME="${VIKI_ME:-$(id -un)}"
HORIZON=2d
STALE_DAYS=1          # POLICY, not a fact. A channel unread for this long is
                      # worth a sign-in. Warren's day decides the number.

while [ $# -gt 0 ]; do
  case "$1" in
    --me)          ME="$2"; shift 2 ;;
    --horizon)     HORIZON="$2"; shift 2 ;;
    --stale-after) STALE_DAYS="$2"; shift 2 ;;
    *) shift ;;
  esac
done
[ -x "$V" ] || { echo "no viki at $V"; exit 2; }

TODAY=$(date -u +%Y-%m-%d)
printf 'MORNING BRIEF  %s\n' "$TODAY"

# ---- 1. what is at risk ------------------------------------------------
printf '\nAT RISK\n'
RISK=$("$V" promises --me "$ME" --horizon "$HORIZON" 2>/dev/null || true)
if printf '%s' "$RISK" | grep -qE '^(OVERDUE|TODAY|         [0-9])'; then
  # KEEP EVERY PROMISE ROW, DROP THE SCAFFOLDING.
  #
  # This used to be `grep -vE '^(       |$)'` -- drop lines starting with seven
  # spaces -- which was written for the note-id continuation line and silently
  # ate every promise that was NOT already overdue. viki_note.c:379 prints
  # `%-8s %-10s %-12s %s`, so a row with no risk marker begins with NINE spaces
  # and matched the filter. Measured 2026-08-26: a promise due in three days
  # vanished from AT RISK entirely, leaving only "0 overdue, 0 due today,
  # 1 later".
  #
  # That inverts what section 2.4 asks for. A brief that lists only what is
  # ALREADY overdue warns after the miss; the whole point is the warning
  # BEFORE it.
  #
  # So filter by STRUCTURE, not by indentation: a promise row has several
  # fields, the continuation line is one indented token (the note id), and the
  # footer lines are the ledger describing its own coverage -- which the brief
  # states in its own words further down.
  printf '%s\n' "$RISK" | sed -n '3,$p' \
    | grep -vE '^[[:space:]]*$|^[[:space:]]+[^[:space:]]+$' \
    | grep -vE '^[[:space:]]+(undated promises|this ledger sees)'
else
  # The good morning, stated. This branch is the one that earns trust.
  printf '  nothing due in the next %s.\n' "$HORIZON"
fi

# ---- 2. what I can and cannot see --------------------------------------
#
# Consumes `viki coverage --json`, not the human table. The first version awk'd
# the table and split "captured here" into two fields -- which is what --json is
# for, and a good argument for never parsing a display format.
printf '\nWHAT I CAN SEE\n'
"$V" coverage --json 2>/dev/null | python3 -c '
import json, sys, datetime
# STALENESS IS POLICY AND IT LIVES HERE. viki reports last-seen times and
# deliberately no thresholds; how long is too long is a judgment about
# the users day, not a fact about the corpus.
stale_days = int(sys.argv[1])
try:    rows = json.load(sys.stdin)
except Exception: rows = []
if not rows:
    print("  nothing at all -- no captures, no channels.")
    sys.exit(0)
now = datetime.datetime.now(datetime.timezone.utc)
cut = (now - datetime.timedelta(days=stale_days)).strftime("%Y-%m-%d")
today = now.strftime("%Y-%m-%d")
stale = []
for r in rows:
    src, seen = r["source"], (r["last_seen"] or "")[:10]
    is_stale = bool(seen) and seen < cut
    when = ("today " + (r["last_seen"][11:16] if len(r["last_seen"]) > 16 else "")) \
           if seen == today else (seen or "never")
    print("  %-16s %-14s %3d note(s)%s" % (src, when, r["notes"], "   STALE" if is_stale else ""))
    if is_stale and src != "captured here":
        stale.append(src)
if stale:
    # The sign-in round as a BOUNDED task -- a list that shrinks on a good day,
    # rather than "go check everything".
    print("\n  SIGN IN: " + ", ".join(stale))
    print("  Five minutes now buys the rest of the day.")
else:
    print("\n  all channels read today.")
' "$STALE_DAYS"
printf '  Anything not opened in this browser is not seen at all.\n'

# ---- 3. what I am unsure about -----------------------------------------
# `structure --pending` prints "viki structure: N capture(s) ..." on line 1;
# the first version grepped a leading number and picked up a note ID instead.
PENDING=$("$V" structure --pending 2>/dev/null | sed -n '1s/.*: *\([0-9][0-9]*\).*/\1/p' || true)
UNDATED=$("$V" promises --me "$ME" --all 2>/dev/null | grep -c '(no due)' || true)
if [ "${PENDING:-0}" -gt 0 ] || [ "${UNDATED:-0}" -gt 0 ]; then
  printf '\nQUESTIONS\n'
  [ "${UNDATED:-0}" -gt 0 ] && \
    printf '  %s promise(s) have no due date. Are they promises, or notes?\n' "$UNDATED"
  [ "${PENDING:-0}" -gt 0 ] && \
    printf '  %s capture(s) not yet structured:  viki structure --pending\n' "$PENDING"
else
  printf '\nNo questions.\n'
fi
