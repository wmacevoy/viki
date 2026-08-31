#!/bin/sh
# fuel.sh -- how much context is left, MEASURED, not felt.
#
# PRECOMPACT IS A MYTH: it fired 0 times in 6 boundaries of session ff137b62
# while demonstrably wired (kin diary, 2026-08-31). So a trace cannot be told
# by a hook that its window is closing. This is what it can read instead.
#
# Every assistant record in the session transcript carries `usage`. The sum of
# its *input* fields is the context size at that turn. Measured over five
# AUTOMATIC compactions in one session, the reading at the moment of
# compaction was:
#
#     997922  997799  997683  995655  996872        (a 2267-token spread)
#
# so the ceiling is ~996k and it is remarkably tight. A sixth, MANUAL, fired
# at 612711 -- a human can compact whenever they like, which is why this
# reports a fraction and never a prediction.
#
# TWO HONEST LIMITS:
#   1. It is one turn STALE. It reads the last COMPLETED assistant turn, so it
#      cannot see the turn you are in.
#   2. Reading it MOVES it. Small, but it is not a free observation.
#
# Usage:  sh assistant/fuel.sh [--hook] [transcript.jsonl]
#
#   --hook   print NOTHING below FUEL_WARN (default 70%), because a per-turn
#            heartbeat that always speaks eats the context it exists to guard.
#            "Nothing to report" and "I COULD NOT LOOK" still render
#            DIFFERENTLY: the second always prints. That distinction is the
#            one this repo keeps losing.
#
# Exit:   0 normally, 3 if it could not look -- never silent about that.

set -u
HOOK=0
if [ "${1:-}" = "--hook" ]; then HOOK=1; shift; fi
CEIL=${FUEL_CEILING:-996000}
DIR=${CLAUDE_TRANSCRIPT_DIR:-$HOME/.claude/projects}

J=${1:-}
if [ -z "$J" ]; then
    # newest transcript under the project dirs. A GUESS, and it says so below.
    J=$(find "$DIR" -name '*.jsonl' -type f 2>/dev/null \
        | xargs ls -t 2>/dev/null | head -1)
    GUESSED=" (newest transcript -- a guess; pass the path to be sure)"
else
    GUESSED=""
fi

if [ -z "$J" ] || [ ! -r "$J" ]; then
    echo "FUEL: COULD NOT LOOK -- no readable transcript under $DIR"
    echo "  This is NOT the same as 'plenty of room'."
    exit 3
fi

python3 - "$J" "$CEIL" "$GUESSED" "$HOOK" "${FUEL_WARN:-70}" <<'PY'
import json,sys
path,ceil,guessed = sys.argv[1], int(sys.argv[2]), sys.argv[3]
hook,warn = sys.argv[4]=="1", float(sys.argv[5])
cur=None; n=0
for l in open(path):
    if '"usage"' not in l: continue
    try: d=json.loads(l)
    except: continue
    u=(d.get("message") or {}).get("usage") or d.get("usage")
    if not u: continue
    v=sum(x for k,x in u.items() if isinstance(x,int) and "input" in k)
    if v: cur=v; n+=1
if cur is None:
    print("FUEL: COULD NOT LOOK -- transcript has no usage records")
    print("  This is NOT the same as 'plenty of room'.")
    sys.exit(3)
pct = 100.0*cur/ceil
if hook and pct < warn:
    sys.exit(0)          # nothing to say. NOT the same as could-not-look above.
bar = int(pct/5)
# ALWAYS name the file. nanny.2 measured 2026-08-31 on the village that FOUR
# sibling transcripts wrote inside its own 4.5-minute window under one unix
# user -- so the newest-file default is a RACE, not a guess, and pointed at a
# sibling this printed a perfectly well-formed FUEL line with nothing marking
# it as someone else's. Its fix, adopted verbatim: a tool that resolves an
# implicit argument must report the RESOLUTION, not the fact that resolution
# occurred. A reader who sees a session id that is not theirs stops; a reader
# who sees a trailing "(a guess)" does not.
import os
print("FUEL: %d of ~%d tokens  (%.0f%%)  [%s%s]" % (
    cur, ceil, pct, "#"*min(bar,20), "."*max(0,20-bar)))
print("  read: %s" % os.path.basename(path))
print("  %d turns measured%s" % (n, guessed))
if pct >= 85:
    print("  *** THE WINDOW IS CLOSING. Write what you are UNCERTAIN about now,")
    print("      with `viki claim --status k1`. The summary has no slot for it,")
    print("      and PreCompact will not fire to ask you.")
elif pct >= 70:
    print("  Getting full. Decide what your successor must NOT relearn.")
PY
