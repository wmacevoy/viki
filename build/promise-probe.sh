#!/bin/sh
# promise-probe.sh -- the promise ledger (VIKIVERSE_V1 2.1, 2.2, 2.2b).
#
# The assertion that matters is P4: A SUPERSEDED PROMISE MUST LEAVE THE LEDGER.
# Everything else here is presentation; that one is the difference between a
# ledger you believe at 6am and one you learn to ignore. A promise retired last
# month still reading as owed makes the morning brief wrong in the direction of
# anxiety, and the whole product aims at the opposite.
#
# Usage: sh build/promise-probe.sh <empty-dir>
#
# NO `set -e` HERE, deliberately: every control assertion below is a
# `grep -c ... = 0`, and grep exits 1 when it counts zero. Under set -e the
# first passing CONTROL would kill the probe -- i.e. the suite would abort
# precisely when the code was behaving correctly. The assertions report their
# own failures and the exit status comes from the FAIL count.
DIR="$1"; [ -n "$DIR" ] || { echo "usage: $0 <empty-dir>"; exit 2; }
mkdir -p "$DIR"; DIR=$(cd "$DIR" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
[ -x "$V" ] || { echo "no viki at $V"; exit 2; }
PASS=0; FAIL=0; SKIP=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
# sk() WAS CALLED AT TWO SITES AND NEVER DEFINED. A skipped assertion therefore
# produced `sk: command not found` on stderr and vanished from the tally, so a
# run with no sqlite3 printed `PASS=21 FAIL=0`, exit 0, with nothing saying
# three assertions had not run. CLAUDE.md's "a skipping run still exits 0 --
# never read exit status alone, read the N passed / N failed / N skipped line"
# is implemented in test/m1.sh and was missing here.
sk(){ SKIP=$((SKIP+1)); echo "  SKIP  $1"; }
chk(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (got '$2' want '$3')"; fi; }

cd "$DIR"
LATE=$(date -u -v-2d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '2 days ago' +%Y-%m-%dT12:00:00Z)
SOON=$(date -u -v+3d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '3 days' +%Y-%m-%dT12:00:00Z)
FAR=$(date -u -v+90d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '90 days' +%Y-%m-%dT12:00:00Z)

# The id comes from `capture`'s OWN output, not from re-reading --pending.
# Guessing it as "the first pending note" is wrong the moment more than one is
# pending, which is every call after the first -- and it fails silently, so the
# probe reports a ledger bug that is really a test bug.
cap(){   # cap "<text>" [extra viki capture args...]
  txt="$1"; shift
  id=$("$V" capture "$txt" "$@" 2>/dev/null | awk '{print $1; exit}')
  "$V" index . >/dev/null 2>&1      # structure resolves ids via the projection
  printf '%s' "$id"
}

echo "== the ledger =="
A=$(cap "call the vet about the mare")
"$V" structure "$A" --type task --place ranch --due "$LATE" >/dev/null 2>&1
B=$(cap "send Karl the rank data")
"$V" structure "$B" --type task --place work --due "$SOON" --who warren >/dev/null 2>&1
C=$(cap "watch for the feed store invoice")
"$V" structure "$C" --type task --place ranch --due "$SOON" --who claude >/dev/null 2>&1
D=$(cap "reseed the north pasture")
"$V" structure "$D" --type task --place ranch --due "$FAR" >/dev/null 2>&1
E=$(cap "think about the fence line")
"$V" structure "$E" --type note --place ranch >/dev/null 2>&1
"$V" index . >/dev/null 2>&1

OUT=$("$V" promises --me warren 2>&1)
chk "P1 an overdue promise is marked OVERDUE" \
    "$(printf '%s' "$OUT" | grep -c 'OVERDUE.*vet')" "1"
chk "P2 a promise held by someone else names the holder, not 'mine'" \
    "$(printf '%s' "$OUT" | grep -c 'claude.*feed store')" "1"
chk "P3 CONTROL: a non-task note is NOT a promise" \
    "$(printf '%s' "$OUT" | grep -c 'fence line')" "0"
chk "P3b CONTROL: a promise beyond the horizon is not listed" \
    "$(printf '%s' "$OUT" | grep -c 'north pasture')" "0"
chk "P3c ...but --horizon reaches it" \
    "$("$V" promises --me warren --horizon 200d 2>&1 | grep -c 'north pasture')" "1"

echo "== supersession retires a promise (the assertion that matters) =="
F=$(cap "called the vet, she comes Thursday")
"$V" structure "$F" --type task --place ranch --state closed --closes "$A" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
OUT2=$("$V" promises --me warren 2>&1)
chk "P4 the superseded promise is GONE from the ledger" \
    "$(printf '%s' "$OUT2" | grep -c 'vet about the mare')" "0"
chk "P4b CONTROL: the untouched promises are still there" \
    "$(printf '%s' "$OUT2" | grep -c 'rank data')" "1"

echo "== continuity: the chain, both ways =="
WHY=$("$V" why "$A" 2>&1)
chk "P5 why shows what replaced it" "$(printf '%s' "$WHY" | grep -c 'comes Thursday')" "1"
chk "P6 why counts the hops" "$(printf '%s' "$WHY" | grep -c '0 before, 1 after')" "1"
WHY2=$("$V" why "$F" 2>&1)
chk "P7 and reads backwards from the successor" \
    "$(printf '%s' "$WHY2" | grep -c 'vet about the mare')" "1"
chk "P8 CONTROL: an unrelated note has no chain" \
    "$("$V" why "$B" 2>&1 | grep -c 'supersedes it and it supersedes nothing')" "1"

echo "== coverage is a PRIMITIVE: a query, with no judgment in it =="
# The line viki must not cross. `coverage` reports last-seen times and nothing
# else -- no thresholds, no "stale", no advice. The moment it needs a number
# like "12 hours" it has become policy, and policy belongs in assistant/.
# THE READER DECLARES ITS CHANNEL (2026-08-27). This fixture stands in for a
# browser-reader capture, and the reader now passes --channel rather than only
# prefixing the text. The prefix is kept because it is provenance a human and a
# structuring agent both read; what changed is that coverage no longer has to
# GUESS the channel from it. B5 depends on this: the brief will not send anyone
# to sign in to a channel viki merely inferred.
T=$(cap "[teams] Karl asked for the rank data" --channel teams)
"$V" index . >/dev/null 2>&1
COV=$("$V" coverage 2>&1)
chk "C1 a channel-sourced note appears as its own source" \
    "$(printf '%s' "$COV" | grep -cE '^teams ')" "1"
chk "C2 the user's own captures are a source too" \
    "$(printf '%s' "$COV" | grep -c 'captured here')" "1"
chk "C3 CONTROL: coverage renders NO judgment -- no STALE, no advice" \
    "$(printf '%s' "$COV" | grep -ciE 'stale|sign in|should|overdue')" "0"
if printf '%s' "$("$V" coverage --json 2>&1)" | grep -q '"source":"teams"'; then
  ok "C4 --json is machine-readable, so an agent need not parse a display format"
else no "C4 --json malformed"; fi

echo "== the brief is the ASSISTANT's, and lives outside viki =="
B="$ROOT/assistant/brief.sh"
if [ ! -x "$B" ]; then
  sk "B1 (no assistant/brief.sh)"
else
  BR=$(cd "$DIR" && sh "$B" --me warren 2>&1)
  chk "B1 the brief reports what is at risk" \
      "$(printf '%s' "$BR" | grep -c 'AT RISK')" "1"
  chk "B2 and states its coverage" \
      "$(printf '%s' "$BR" | grep -c 'WHAT I CAN SEE')" "1"
  chk "B3 and what it cannot see at all" \
      "$(printf '%s' "$BR" | grep -c 'not seen at all')" "1"

  # STALENESS IS THE ASSISTANT'S CALL. Age a channel and the judgment appears
  # in the brief -- while `viki coverage` above still refuses to make it.
  if command -v sqlite3 >/dev/null 2>&1; then
    sqlite3 "$DIR/.viki/cache.db" \
      "UPDATE viki_note SET ts='2020-01-02T09:00:00Z' WHERE text LIKE '%[teams]%';" 2>/dev/null
    BR2=$(cd "$DIR" && sh "$B" --me warren 2>&1)
    chk "B4 an old channel is judged STALE by the assistant" \
        "$(printf '%s' "$BR2" | grep -c 'STALE')" "1"
    chk "B5 ...and named in a bounded sign-in list" \
        "$(printf '%s' "$BR2" | grep -c 'SIGN IN: teams')" "1"
    chk "B6 CONTROL: viki coverage STILL says nothing about it" \
        "$(cd "$DIR" && "$V" coverage 2>&1 | grep -ciE 'stale|sign in')" "0"
  else
    sk "B4 (no sqlite3)"; sk "B5 (no sqlite3)"; sk "B6 (no sqlite3)"
  fi

  # THE GOOD MORNING MUST BE SAID OUT LOUD. Silence is indistinguishable from a
  # broken cron job, and a brief that is sometimes absent is one nobody relies on.
  mkdir -p "$DIR/quiet"
  ( cd "$DIR/quiet" && "$V" capture "a passing thought" >/dev/null 2>&1
    "$V" index . >/dev/null 2>&1 )
  QB=$(cd "$DIR/quiet" && sh "$B" --me warren 2>&1)
  chk "B7 a day with nothing owed SAYS SO, rather than printing nothing" \
      "$(printf '%s' "$QB" | grep -c 'nothing due in the next')" "1"
  chk "B8 CONTROL: and the brief is still a full brief, not a stub" \
      "$(printf '%s' "$QB" | grep -c 'WHAT I CAN SEE')" "1"
fi

# THE BRIEF MUST SHOW A PROMISE THAT IS NOT YET LATE.
# assistant/brief.sh filtered the ledger with `grep -vE '^(       |$)'`, which
# was written for the note-id continuation line and also matched every row with
# no risk marker (viki_note.c prints `%-8s ...`, so those begin with NINE
# spaces). A promise due in three days vanished from AT RISK, leaving only the
# count -- the brief warned AFTER a miss, when 2.4 asks for the warning before.
if [ -f "$ROOT/assistant/brief.sh" ]; then
    # Added HERE, after every counting assertion above, so a new row cannot
    # perturb their tallies. Deliberately UNOWNED: viki_note.c treats a promise
    # with no holder as yours, which is what `--me mine` must pick up.
    BF=$(cap "soonmarker renew the trailer registration")
    "$V" structure "$BF" --type task --due "$SOON" >/dev/null 2>&1
    "$V" index . >/dev/null 2>&1        # the ledger is a projection; refresh it
    BSOON=$(VIKI_BIN="$V" sh "$ROOT/assistant/brief.sh" --me mine --horizon 7d 2>/dev/null \
            | sed -n '/AT RISK/,/WHAT I CAN SEE/p')
    chk "B9 the brief lists a promise that is DUE LATER, not just overdue ones" \
        "$(printf '%s' "$BSOON" | grep -c 'soonmarker')" "1"
    # CONTROL: it must still be a brief, not a dump of the whole ledger --
    # the note-id continuation lines are scaffolding and stay out.
    chk "B10 CONTROL: ...and the note-id continuation lines stay filtered" \
        "$(printf '%s' "$BSOON" | grep -cE '^[[:space:]]+[^[:space:]]+$')" "0"
else
    sk "B9 (no assistant/brief.sh)"; sk "B10 (no assistant/brief.sh)"
fi

echo "== coverage is stated, not implied (2.5) =="
chk "P9 the ledger says what it can and cannot see" \
    "$(printf '%s' "$OUT2" | grep -c 'captured and ingested notes only')" "1"

echo "== a DATE-ONLY due date means end of day, not midnight (2026-08-26) =="
# WHY THIS SECTION EXISTS: every assertion above uses a full ISO timestamp
# (LATE/SOON/FAR are all `...T12:00:00Z`), so none of them could see that
# risk_of() compared a 10-char "2026-08-26" against a 27-char zNow with
# strcmp(). The bare date is a PREFIX of the instant, so it sorted first and
# EVERY date-only promise read OVERDUE from 00:00 of the day it was owed.
#
# The ledger being wrong toward anxiety is the one direction this file's own
# header says the product exists to prevent, so the fixture now writes a bare
# date the way a person actually would.
DAY_TODAY=$(date -u +%Y-%m-%d)
DAY_YEST=$(date -u -v-1d +%Y-%m-%d 2>/dev/null || date -u -d 'yesterday' +%Y-%m-%d)
DAY_TOMO=$(date -u -v+1d +%Y-%m-%d 2>/dev/null || date -u -d 'tomorrow' +%Y-%m-%d)
mkdir -p captures
cat > captures/dateonly.md <<EOF
@note 20260826-daonly-000001
@at ${DAY_TODAY}T06:00:00.000000Z
@type task
@who mine
@due $DAY_TODAY
dateonlytoday marker

@note 20260826-daonly-000002
@at ${DAY_TODAY}T06:00:00.000000Z
@type task
@who mine
@due $DAY_YEST
dateonlyyesterday marker

@note 20260826-daonly-000003
@at ${DAY_TODAY}T06:00:00.000000Z
@type task
@who mine
@due $DAY_TOMO
dateonlytomorrow marker
EOF
"$V" index . >/dev/null 2>&1
DOUT=$("$V" promises --all 2>&1)

chk "P12 a promise due TODAY as a bare date reads TODAY, not OVERDUE" \
    "$(printf '%s' "$DOUT" | grep 'dateonlytoday' | grep -c 'TODAY')" "1"
# CONTROL: the fix must not swallow real lateness. A bare date in the PAST is
# still overdue -- if P12 were implemented by never marking a date-only promise
# overdue, this is what catches it.
chk "P13 CONTROL: a bare date in the PAST is still OVERDUE" \
    "$(printf '%s' "$DOUT" | grep 'dateonlyyesterday' | grep -c 'OVERDUE')" "1"
# CONTROL the other way: tomorrow is neither.
chk "P14 CONTROL: a bare date TOMORROW is neither overdue nor due today" \
    "$(printf '%s' "$DOUT" | grep 'dateonlytomorrow' | grep -cE 'OVERDUE|TODAY')" "0"
rm -f captures/dateonly.md
"$V" index . >/dev/null 2>&1

echo "== an unparseable @due must not be RISKED as a guess (2026-08-27) =="
# WHY: every risk decision in viki_note.c is a lexicographic comparison against
# an ISO instant, which is chronological only for ISO input. Before this, @due
# was never validated, so a non-ISO date was not ignored -- it was
# MISCLASSIFIED, and toward anxiety: "08/28/2026" (tomorrow, US format) read
# OVERDUE because "0" < "2", and the summary said "1 overdue" and meant it.
#
# This is the trap CALENDAR INGEST walks into: an ICS adapter emitting a
# non-ISO date would have produced a phantom overdue every morning, in the
# brief, with nothing on screen explaining why.
mkdir -p captures
cat > captures/duevalid.md <<EOF
@note 20260827-duev-000001
@at ${DAY_TODAY}T06:00:00.000000Z
@type task
@who mine
@due 08/28/2026
usdatemarker a US-format due for TOMORROW

@note 20260827-duev-000002
@at ${DAY_TODAY}T06:00:00.000000Z
@type task
@who mine
@due next Tuesday
prosemarker a due date written as prose
EOF
"$V" index . >"$DIR/duev.err" 2>&1
DOUT2=$("$V" promises --all 2>&1)

chk "P15 a US-format due is NOT reported as overdue" \
    "$(printf '%s' "$DOUT2" | grep 'usdatemarker' | grep -c 'OVERDUE')" "0"
chk "P16 ...it is undated instead, which is the honest reading" \
    "$(printf '%s' "$DOUT2" | grep 'usdatemarker' | grep -c '(no due)')" "1"
chk "P17 prose in @due is undated too, not sorted as a string" \
    "$(printf '%s' "$DOUT2" | grep 'prosemarker' | grep -c '(no due)')" "1"
# LOUD, not silent: a dated promise becoming undated must be visible, or the
# footer's "undated promises are not shown" gives the wrong reason for an
# absence. The warning names the note and the value.
chk "P18 the demotion is ANNOUNCED, naming the note and the value" \
    "$(grep -c '20260827-duev-000001' "$DIR/duev.err")" "1"
# CONTROL, and the one that stops P15-P17 being "nothing is ever due": a real
# ISO due must still be dated and still risked.
chk "P19 CONTROL: a valid ISO due is still dated and still risked" \
    "$(printf '%s' "$DOUT2" | grep -c 'soonmarker')" "1"
rm -f captures/duevalid.md
"$V" index . >/dev/null 2>&1


echo "== captured text cannot become note SYNTAX (2026-08-27) =="
# THE ATTACK THIS STANDS AGAINST, measured on main before the fix. Captured
# text is NOT TRUSTED: the Chrome reader POSTs whatever a Facebook, Discord,
# D2L or Outlook message happened to say, and note_parse_file() gives '@' in
# column 0 structural meaning -- `@note` even ENDS the previous block. So a
# message someone else wrote could forge a promise AND retire a real one:
#
#   viki capture "hey did you see this
#   @note forged-1
#   @closes <a real note id>
#   nothing to do here"
#
# ...silently removed "ship the grant budget to renee" from the ledger. A
# ledger that loses an obligation because of something a stranger typed is the
# worst failure this product has, and it was live.
#
# F3 IS THE ONE THAT MATTERS. A fix that only escapes the BODY passes F1/F2 and
# still loses the ledger, because a field value carrying a newline opens a
# content line without touching the body at all.
FVID=$(cap "forgetarget ship the grant budget to renee")
"$V" structure "$FVID" --type task --due "$SOON" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
FNOTE=$("$V" promises --all 2>/dev/null | grep -A1 'forgetarget' | tail -1 | tr -d ' ')

"$V" capture "hey did you see this
@note forgedmarker-1
@type task
@who mine
@closes $FNOTE
nothing to do here" >/dev/null 2>&1
"$V" capture "innocent text" --place "$(printf 'ranch\n@closes %s' "$FNOTE")" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
FOUT=$("$V" promises --all 2>/dev/null)

chk "F1 a body line beginning @note does NOT start a new note" \
    "$(printf '%s' "$FOUT" | grep -c 'forgedmarker-1')" "0"
chk "F2 ...and the real promise it tried to close is STILL OWED" \
    "$(printf '%s' "$FOUT" | grep -c 'forgetarget')" "1"
# F3: the vector a body-only fix misses entirely.
chk "F3 a NEWLINE inside a field value cannot inject a line either" \
    "$(printf '%s' "$FOUT" | grep -c 'forgetarget')" "1"
# CONTROL: this is neutralisation, not refusal. The text must still be
# captured and still be findable -- a fix that dropped the note would pass
# F1-F3 while losing the observation, which is the other way to be wrong.
chk "F4 CONTROL: the hostile capture was still STORED, not discarded" \
    "$(grep -rc 'nothing to do here' captures/ 2>/dev/null | awk -F: '{s+=$2} END{print (s>0)?1:0}')" "1"
# CONTROL: @closes still WORKS when it is viki's own field, or F1-F3 could be
# satisfied by breaking supersession outright -- which P4 also guards.
chk "F5 CONTROL: a legitimate --closes still retires a promise" \
    "$(printf '%s' "$("$V" promises 2>/dev/null)" | grep -c 'call the vet')" "0"


echo "== a channel is DECLARED by its producer, not guessed from text =="
# WHY: `viki coverage` derived channel identity from a "[name] " prefix in note
# text, which cannot be told from a bracket a person typed. Measured on main:
# `viki capture "[TODO] fix the gate latch"` invented a channel named TODO and
# brief.sh listed it under WHAT I CAN SEE as freshly covered -- the coverage
# lie 2.5 exists to prevent, arriving through the mechanism built to prevent
# it. Producers now declare themselves; the bracket survives as a marked
# fallback so notes captured before the change do not silently vanish from
# coverage.
"$V" capture "declaredmarker from a real producer" --channel discord >/dev/null 2>&1
"$V" capture "[PHANTOM] a bracket a person typed" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
COV=$("$V" coverage 2>/dev/null)
CJS=$("$V" coverage --json 2>/dev/null)

chk "H1 a declared channel appears under its own name" \
    "$(printf '%s' "$COV" | grep -c '^discord ')" "1"
chk "H2 ...and is reported as DECLARED in json" \
    "$(printf '%s' "$CJS" | grep -c '"source":"discord","last_seen":"[^"]*","notes":[0-9]*,"declared":true')" "1"
# The bracket is still honoured -- dropping it would silently shrink coverage
# for every note captured before this existed -- but it is MARKED.
chk "H3 an inferred channel is still shown, and marked as inferred" \
    "$(printf '%s' "$COV" | grep 'PHANTOM' | grep -c 'inferred')" "1"
chk "H4 ...and json says declared:false rather than decorating the name" \
    "$(printf '%s' "$CJS" | grep -c '"source":"PHANTOM","last_seen":"[^"]*","notes":[0-9]*,"declared":false')" "1"
# THE ONE THAT MATTERS FOR THE PRODUCT: the brief must never send a person to
# sign in to a channel that does not exist. viki reports the distinction and
# judges nothing; the judgment is in assistant/, which is where it belongs.
if [ -f "$ROOT/assistant/brief.sh" ]; then
    CBRIEF=$(VIKI_BIN="$V" sh "$ROOT/assistant/brief.sh" --me mine 2>/dev/null)
    chk "H5 the brief SHOWS an inferred channel (hiding it is its own lie)" \
        "$(printf '%s' "$CBRIEF" | grep -c 'PHANTOM')" "1"
    chk "H6 ...but never puts one on the SIGN IN list" \
        "$(printf '%s' "$CBRIEF" | grep 'SIGN IN' | grep -c 'PHANTOM')" "0"
else
    sk "H5 (no assistant/brief.sh)"; sk "H6 (no assistant/brief.sh)"
fi
# CONTROL: this is marking, not suppression. A pre-existing bracketed note must
# still count toward coverage, or the fix would have silently shrunk what viki
# claims to see -- the opposite failure and just as dishonest.
chk "H7 CONTROL: the inferred channel still carries its note count" \
    "$(printf '%s' "$COV" | grep 'PHANTOM' | grep -c '1 note(s)')" "1"


echo "== the reader's chunking, and every writer that shares it (2026-08-27) =="
# WHY ALL OF THIS IS ONE SECTION: it is one root cause with three faces, and
# the writer-side fix committed earlier the same day did NOT close it.
#
# note_parse_file() reads with fgets(line, 2048, f) and treated byte 0 of every
# CHUNK as column 0. A line longer than the buffer arrives as several chunks,
# so padding a capture to exactly 2047 bytes re-splits it at a position the
# writer never saw:
#
#   viki capture "$(python3 -c 'print("x"*2047,end="")')@closes <id>"
#
# ...retired a real promise, through the fix that was supposed to stop it. The
# repair belongs in the READER because captures/*.md is hand-edited too.
G_REAL=$(cap "chunkmarker ship the grant budget to renee")
"$V" structure "$G_REAL" --type task --who warren --due "$SOON" >/dev/null 2>&1
G_OTHER=$(cap "chunkother someone mentioned the north gate")
"$V" index . >/dev/null 2>&1
G_ID=$("$V" sql "SELECT note_id FROM viki_note WHERE text LIKE '%chunkmarker%'" 2>/dev/null | tr -d ' ')

"$V" capture "$(python3 -c 'import sys; sys.stdout.write("x"*2047)')@closes $G_ID" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
chk "G1 a body padded to the fgets boundary cannot inject a field" \
    "$("$V" promises --all 2>/dev/null | grep -c 'chunkmarker')" "1"

# emit_field() on the structure path never got write_field()'s newline guard,
# so this needed no padding trick at all -- and /api/structure reaches it too.
"$V" structure "$G_OTHER" --place "$(printf 'ranch\n@closes %s' "$G_ID")" >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
chk "G2 a newline in a --structure field cannot inject a line either" \
    "$("$V" promises --all 2>/dev/null | grep -c 'chunkmarker')" "1"

# THE DATA-LOSS FACE, and the worst of the three: structure_write() consumed a
# continuation chunk as a field line, dropped it from the body and never
# re-emitted it, PERMANENTLY DELETING the user's own words from captures/*.md
# -- the file D-10 calls truth.
# G3 RUNS IN ITS OWN DIRECTORY so the byte comparison is over one file, and it
# asserts on BYTES rather than on a grep: the first version grepped for the
# marker, which survives whether or not the tail of the line was deleted, so it
# passed against the very binary that destroys the text (50/3 with G3 green).
# The defect deletes a SUFFIX; only a length comparison sees that.
mkdir -p g && ( cd g
    export VIKI_MODEL_DIR="$DIR/no-model"
    "$V" capture "$(python3 -c 'import sys; sys.stdout.write("y"*2047 + "@place elsewhere and CHUNKBODYMARKER tail words here")')" >/dev/null 2>&1
    "$V" index . >/dev/null 2>&1 )
G3ID=$(grep -h '^@note ' g/captures/*.md 2>/dev/null | head -1 | awk '{print $2}')
G3BEFORE=$(cat g/captures/*.md 2>/dev/null | wc -c | tr -d ' ')
( cd g && VIKI_MODEL_DIR="$DIR/no-model" "$V" structure "$G3ID" --type note >/dev/null 2>&1 )
G3AFTER=$(cat g/captures/*.md 2>/dev/null | wc -c | tr -d ' ')
chk "G3 SETUP: the fixture really wrote an over-long line and a note id" \
    "$([ -n "$G3ID" ] && [ "${G3BEFORE:-0}" -gt 2047 ] && echo 1 || echo 0)" "1"
# structure ADDS an @type line, so the file must grow. It shrank by 40 bytes
# before the fix, because the continuation chunk was eaten.
chk "G3b viki structure does not DELETE body text at the chunk boundary" \
    "$([ "${G3AFTER:-0}" -ge "${G3BEFORE:-0}" ] && echo 1 || echo 0)" "1"
chk "G3c CONTROL: ...and the tail of that line is still in the file" \
    "$(grep -hc 'tail words here' g/captures/*.md 2>/dev/null | head -1)" "1"

echo "== coverage --json must stay parseable, and the brief must say when it is not =="
# An INFERRED channel name is raw note text, so it can carry a quote. One
# ordinary capture -- `[he said "no"] ...` -- produced invalid JSON, brief.sh's
# `except: rows = []` reported "nothing at all, no captures, no channels", and
# three live channels including a DECLARED one vanished from the brief at
# exit 0. The coverage lie 2.5 exists to prevent, on the surface where it does
# the most damage.
"$V" capture '[he said "no"] pasture gate is open' >/dev/null 2>&1
"$V" index . >/dev/null 2>&1
chk "G4 a quote in an inferred channel name still yields valid JSON" \
    "$("$V" coverage --json 2>/dev/null | python3 -c 'import json,sys
try:
    json.load(sys.stdin); print(1)
except Exception: print(0)')" "1"
# CONTROL: the channel is still REPORTED, not dropped to make the JSON valid.
chk "G5 CONTROL: ...and that channel is still present, not silently dropped" \
    "$("$V" coverage 2>/dev/null | grep -c 'he said')" "1"
# The other half: an unreadable coverage must not read as an empty one.
cat > "$DIR/fakeviki" <<'FAKE'
#!/bin/sh
case "$1" in
  coverage) printf '[{"source":"broken",,,}]\n' ;;
  promises) printf 'viki promises: as of now\nRISK DUE WHO WHAT\n\nnothing owed.\n' ;;
  structure) printf 'viki structure: 0 capture(s)\n' >&2 ;;
esac
exit 0
FAKE
chmod +x "$DIR/fakeviki"
if [ -f "$ROOT/assistant/brief.sh" ]; then
    GB=$(VIKI_BIN="$DIR/fakeviki" sh "$ROOT/assistant/brief.sh" --me warren 2>/dev/null)
    chk "G6 unreadable coverage is ANNOUNCED, not reported as no channels" \
        "$(printf '%s' "$GB" | grep -c 'CANNOT READ COVERAGE')" "1"
    chk "G7 CONTROL: ...and it does NOT claim there is nothing" \
        "$(printf '%s' "$GB" | grep -c 'nothing at all')" "0"
else
    sk "G6 (no assistant/brief.sh)"; sk "G7 (no assistant/brief.sh)"
fi


echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" = 0 ]
