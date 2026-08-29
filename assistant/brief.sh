#!/bin/sh
# brief.sh -- the morning brief.  NOT VIKI.  See assistant/README.md.
#
# Composes viki-core's primitives into a decision. Everything judgmental lives
# here and nowhere in core/: what counts as stale, what is worth waking someone
# about, what to ask. core supplies facts; this file has the opinions.
#
# PORTED TO viki-core 2026-08-29. It used to call `viki promises`, `viki
# coverage` and `viki structure --pending` on the Fossil-backed binary in src/.
# Warren: "core is the derivable set --- fossil-see is a dead end." Now it
# calls `viki ledger --json`, `viki coverage` and `viki pending` on
# core/build/viki.
#
# IT CONSUMES JSON AND NEVER A DISPLAY FORMAT. The predecessor parsed the
# ledger table by column offset and the format moved under it twice in one
# day -- a note carrying @place made the continuation line two tokens instead
# of one, and every one of them was filed under "an agent is carrying these",
# inventing obligations held by other people. `--json` exists because of that.
#
# THE GOOD MORNING IS SAID OUT LOUD. A day with nothing at risk prints an
# explicit "nothing due" -- silence is indistinguishable from a broken cron
# job, and a brief that is sometimes absent is one nobody comes to rely on.
#
# QUESTIONS ARE BATCHED HERE AND NEVER PUSHED. An assistant that interrupts to
# resolve its own uncertainty has taken the problem and handed it back.
#
# NO BARE `test && printf` AS A LAST STATEMENT, EVER -- `set -e` is on, a
# false test short-circuits to status 1, and the whole script then reports
# itself FAILED after printing perfectly good output. That shipped, and
# ~/.viki/brief/2026-08-29.txt carries the false banner.
#
# Usage: sh assistant/brief.sh [--me NAME] [--horizon 7] [--stale-after 1]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/core/build/viki}"
STORE="${VIKI_STORE:-$HOME/.viki/personal/viki.db}"
KEYFILE="${VIKI_KEYFILE:-$HOME/.viki/personal.key}"
ME="${VIKI_ME:-$(id -un)}"
HORIZON=7             # DAYS. Policy: how far ahead is worth worrying about.
STALE_DAYS=2          # POLICY, not a fact. Warren's day decides the number.

while [ $# -gt 0 ]; do
  case "$1" in
    --me)          ME="$2"; shift 2 ;;
    --horizon)     HORIZON="$2"; shift 2 ;;
    --stale-after) STALE_DAYS="$2"; shift 2 ;;
    --store)       STORE="$2"; shift 2 ;;
    --keyfile)     KEYFILE="$2"; shift 2 ;;
    *) shift ;;
  esac
done

[ -x "$V" ]     || { echo "no viki-core at $V  (run: sh core/build.sh)"; exit 2; }
[ -f "$STORE" ] || { echo "no diary at $STORE"; exit 2; }

# THE KEY IS OPTIONAL AND ITS ABSENCE IS NOT SILENT. A plaintext diary is a
# legitimate choice; guessing which one this is, is not.
if [ -f "$KEYFILE" ]; then  set -- --keyfile "$KEYFILE" --store "$STORE"
else                        set -- --plaintext --store "$STORE"
fi

TODAY=$(date -u +%Y-%m-%d)
printf 'MORNING BRIEF  %s\n' "$TODAY"

# ---- 1. what is at risk ------------------------------------------------
printf '\nAT RISK\n'
"$V" "$@" ledger --me "$ME" --json 2>/dev/null | python3 -c '
import json, sys, datetime
me, horizon = sys.argv[1], int(sys.argv[2])
# "COULD NOT READ IT" AND "THERE IS NOTHING" MUST NOT PRINT THE SAME.
# One ordinary note containing a quote was once enough to make this report
# "nothing at all" while the store was full. Exit 0, no warning.
try:
    rows = json.load(sys.stdin)
except Exception as e:
    print("  CANNOT READ THE LEDGER -- core output did not parse: %s" % e)
    print("  This is NOT the same as having nothing due.")
    sys.exit(0)
today = datetime.date.today().isoformat()
cut   = (datetime.date.today() + datetime.timedelta(days=horizon)).isoformat()

def risk(due):
    if not due:  return ""
    if due <  today: return "OVERDUE"
    if due == today: return "TODAY"
    return ""

# THE HORIZON IS APPLIED HERE, not in core. core returns every live task and
# holds no opinion about which ones matter this morning; "the next seven days"
# is a judgment about the day being planned.
soon = [r for r in rows if r["due"] and r["due"] <= cut]
mine   = [r for r in soon if r["mine"]]
theirs = [r for r in soon if not r["mine"]]

def show(rs):
    for r in rs:
        print("    %-8s %-11s %s" % (risk(r["due"]), r["due"], r["text"]))
        if r["place"]:
            print("             %s" % ("@" + r["place"]))

if mine:
    print("  YOURS -- what you owe")
    show(mine)
if theirs:
    if mine: print("")
    print("  THEIRS -- someone else is carrying these. A broken promise of")
    print("            theirs costs you exactly what one of your own does.")
    show(theirs)
if not mine and not theirs:
    # The good morning, stated. This branch is the one that earns trust.
    print("  nothing due in the next %d day(s)." % horizon)

later   = len(rows) - len(soon)
undated = len([r for r in rows if not r["due"]])
print("\n  %d overdue, %d today, %d within %dd, %d beyond, %d undated"
      % (len([r for r in soon if r["due"] < today]),
         len([r for r in soon if r["due"] == today]),
         len(soon), horizon, later - undated, undated))
' "$ME" "$HORIZON"

# ---- 2. what I can and cannot see --------------------------------------
printf '\nWHAT I CAN SEE\n'
"$V" "$@" coverage 2>/dev/null | python3 -c '
import json, sys, datetime
stale_days = int(sys.argv[1])
try:
    rows = json.load(sys.stdin)
except Exception as e:
    print("  CANNOT READ COVERAGE -- core output did not parse: %s" % e)
    print("  Do not read the absence of a channel below as evidence it is quiet.")
    sys.exit(0)
if not rows:
    print("  nothing at all -- no channels have fed this diary.")
    sys.exit(0)
now   = datetime.datetime.now(datetime.timezone.utc)
today = now.strftime("%Y-%m-%d")
cut   = (now - datetime.timedelta(days=stale_days)).strftime("%Y-%m-%d")
stale, any_stale = [], False
for r in rows:
    src, seen = r["channel"], (r["last_seen"] or "")[:10]
    is_stale = bool(seen) and seen < cut
    when = ("today " + (r["last_seen"][11:16] if len(r["last_seen"]) > 16 else "")) \
           if seen == today else (seen or "never")
    print("  %-16s %-14s %3d item(s)%s" % (src, when, r["n"], "   STALE" if is_stale else ""))
    if is_stale:
        any_stale = True
        # "(none)" is not a channel you can log in to. Sending someone to sign
        # in to a placeholder is how a brief stops being believed.
        if src != "(none)":
            stale.append(src)
if stale:
    print("\n  SIGN IN: " + ", ".join(stale))
    print("  Five minutes now buys the rest of the day.")
elif any_stale:
    # Stale, but nothing to DO about it. Name that rather than rounding it to
    # "all read" -- the predecessor printed "all channels read today" directly
    # under a row marked STALE, which teaches the reader the summary is
    # decoration.
    print("\n  Nothing to sign in to, but not everything is current --")
    print("  the rows marked STALE above have not been added to.")
else:
    print("\n  all channels current.")
' "$STALE_DAYS"

# ---- 3. what I am unsure about -----------------------------------------
UNDATED=$("$V" "$@" ledger --me "$ME" --json 2>/dev/null \
          | python3 -c 'import json,sys
try: print(len([r for r in json.load(sys.stdin) if not r["due"]]))
except Exception: print(0)')
PENDING=$("$V" "$@" pending 2>/dev/null \
          | python3 -c 'import json,sys
try: print(len(json.load(sys.stdin)))
except Exception: print(0)')

if [ "${PENDING:-0}" -gt 0 ] || [ "${UNDATED:-0}" -gt 0 ]; then
  printf '\nQUESTIONS\n'
  if [ "${UNDATED:-0}" -gt 0 ]; then
    printf '  %s task(s) have no due date. Are they promises, or notes?\n' "$UNDATED"
  fi
  if [ "${PENDING:-0}" -gt 0 ]; then
    printf '  %s capture(s) never structured:  viki ledger, then viki task ...\n' "$PENDING"
  fi
else
  printf '\nNo questions.\n'
fi
