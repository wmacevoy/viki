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
  # YOURS AND THEIRS ARE SEPARATED, which is 2.2's second acceptance clause and
  # was the last one unmet. The ledger already distinguishes parties -- it
  # prints a WHO column -- so this is presentation, not new data. But it is
  # presentation that changes what the reader DOES: "call the vet" and "claude
  # is watching for the invoice" are the same row shape and completely
  # different obligations, and one list made you re-read the WHO column on
  # every line to find the ones that are actually yours.
  #
  # SPLIT BY COLUMN OFFSET, which is stable here and only here. viki_note.c
  # prints the ledger as `%-8s %-10s %-12s %s`, so WHO is always characters
  # 21-32 whatever the risk marker says. This is the one place a display
  # format may be parsed by position, and it is allowed because the same
  # repository owns both ends -- m1 already parses the ask hit line this way.
  # If that format ever changes, this breaks loudly rather than silently
  # mis-attributing a promise.
  #
  # THE TEST IS THE LITERAL "mine", NOT A COMPARISON AGAINST --me.
  #
  # viki_note.c has already decided this: it renders the caller own rows -- and
  # unowned ones, since a commitment nobody claimed is one you are still
  # carrying -- as the word "mine", and everything else as the holder name. So
  # re-deriving ownership here would be a second copy of that rule, and the
  # first version of this code did exactly that and got it backwards: it
  # compared WHO against "warren" while viki was printing "mine", so every row
  # landed in THEIRS including the ones that were yours.
  ROWS=$(printf '%s\n' "$RISK" | sed -n '3,$p' \
    | grep -vE '^[[:space:]]*$|^[[:space:]]+[^[:space:]]+$' \
    | grep -vE '^[[:space:]]+(undated promises|this ledger sees)')
  YOURS=$(printf '%s\n' "$ROWS" | awk \
    '{ who = substr($0, 21, 12); gsub(/^ +| +$/, "", who); if (who == "" || who == "mine") print }')
  THEIRS=$(printf '%s\n' "$ROWS" | awk \
    '{ who = substr($0, 21, 12); gsub(/^ +| +$/, "", who); if (who != "" && who != "mine") print }')

  if [ -n "$YOURS" ]; then
    printf '  YOURS -- what you owe\n'
    printf '%s\n' "$YOURS" | sed 's/^/  /'
  fi
  if [ -n "$THEIRS" ]; then
    [ -n "$YOURS" ] && printf '\n'
    printf '  THEIRS -- an agent is carrying these. A broken agent promise\n'
    printf '            costs you exactly what a broken one of your own does.\n'
    printf '%s\n' "$THEIRS" | sed 's/^/  /'
  fi
  # Never silently print nothing: if the split matched no row at all, the
  # format moved, and showing the raw rows beats showing an empty section.
  if [ -z "$YOURS" ] && [ -z "$THEIRS" ]; then
    printf '  (could not tell yours from theirs -- showing all)\n'
    printf '%s\n' "$ROWS"
  fi
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
# "COULD NOT READ IT" AND "THERE IS NOTHING" MUST NOT PRINT THE SAME.
#
# This was `except Exception: rows = []`, so a malformed line from viki --
# one ordinary capture containing a quote was enough -- reported as
# "nothing at all, no captures, no channels" while three live channels
# including a declared one sat in the cache. Exit 0, no warning, total
# coverage blackout. That is the exact lie 2.5 exists to prevent, and the
# brief is the surface where it does the most damage.
try:
    rows = json.load(sys.stdin)
except Exception as e:
    print("  CANNOT READ COVERAGE -- output from viki did not parse: %s" % e)
    print("  This is NOT the same as having no channels. Do not read the")
    print("  absence of a channel below as evidence it is quiet.")
    sys.exit(0)
if not rows:
    print("  nothing at all -- no captures, no channels.")
    sys.exit(0)
now = datetime.datetime.now(datetime.timezone.utc)
cut = (now - datetime.timedelta(days=stale_days)).strftime("%Y-%m-%d")
today = now.strftime("%Y-%m-%d")
stale = []
any_stale = False
# THE SYNC ROW IS A DIFFERENT KIND OF FACT and gets a different judgment.
# viki reports WHEN this device last pulled and refuses to say whether that is
# too long; SS 2.9 requires the message "sync is a week stale" to be sayable,
# and this is where the week comes from. A number in src/ would be the scope
# violation SCOPES SS 3 exists to prevent.
sync_row = None
keep = []
for r in rows:
    if r.get("kind") == "sync":
        sync_row = r
    else:
        keep.append(r)
rows = keep

for r in rows:
    src, seen = r["source"], (r["last_seen"] or "")[:10]
    # A CHANNEL viki INFERRED IS NOT A CHANNEL TO SIGN IN TO.
    #
    # `declared` false means the name was guessed from a "[name] " prefix in
    # note text, which cannot be told from a bracket a person typed --
    # `viki capture "[TODO] fix the gate latch"` invents one. viki reports the
    # distinction and declines to judge it (coverage has no thresholds); the
    # judgment is here, and it is: an inferred channel is shown, because
    # hiding it would be its own coverage lie, but it is never put on the
    # SIGN IN list. Sending someone to log in to "TODO" is how a brief stops
    # being believed.
    declared = r.get("declared", True)
    is_stale = bool(seen) and seen < cut
    when = ("today " + (r["last_seen"][11:16] if len(r["last_seen"]) > 16 else "")) \
           if seen == today else (seen or "never")
    print("  %-16s %-14s %3d note(s)%s%s" % (
        src, when, r["notes"],
        "   STALE" if is_stale else "",
        "" if declared else "   (inferred, not a real channel)"))
    if is_stale:
        any_stale = True
    if is_stale and src != "captured here" and declared:
        stale.append(src)
# TWO DIFFERENT QUESTIONS, AND THEY WERE ANSWERED WITH ONE LINE.
#
# `stale` is the SIGN-IN list, and "captured here" is deliberately excluded
# from it -- you cannot log in to your own captures. But the closing line was
# chosen from that same list, so a run where the only stale row was
# "captured here" printed "all channels read today." DIRECTLY UNDER a row
# marked STALE. Measured 2026-08-29 on this repo.
#
# Contradicting yourself on screen is worse than either message alone: it
# teaches the reader that the summary line is decoration.
# SYNC_STALE_DAYS is a judgment about the week Warren keeps, not a fact about
# a corpus. Seven, because that is the interval SS 2.9 acceptance text names.
SYNC_STALE_DAYS = 7
if sync_row is not None:
    ls = (sync_row.get("last_seen") or "")[:10]
    if not ls:
        print("  (sync)           NEVER pulled from a hub -- if this tribe has one,")
        print("                   everything above is only what THIS device wrote.")
    else:
        cutoff = (now - datetime.timedelta(days=SYNC_STALE_DAYS)).strftime("%Y-%m-%d")
        if ls < cutoff:
            print("  (sync)           last pull %s -- MORE THAN %d DAYS AGO." % (ls, SYNC_STALE_DAYS))
            print("                   Peers may have written things this device cannot see:")
            print("                   viki cache pull")
        else:
            print("  (sync)           last pull %s" % ls)

if stale:
    # The sign-in round as a BOUNDED task -- a list that shrinks on a good day,
    # rather than "go check everything".
    print("\n  SIGN IN: " + ", ".join(stale))
    print("  Five minutes now buys the rest of the day.")
elif any_stale:
    # Stale, but nothing you can DO about it by signing in. Name it rather than
    # rounding it to "all read".
    print("\n  Nothing to sign in to, but not everything is current --")
    print("  the rows marked STALE above have not been added to.")
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
