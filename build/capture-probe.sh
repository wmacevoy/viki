#!/bin/sh
#
# capture-probe.sh -- the capture loop: `viki capture` / projection / `viki notes`.
#
# WHY THIS EXISTS
# ---------------
# The capture loop answers the two questions similarity ranking cannot, and
# both were WRONG in the first implementation:
#
#   "what needs to be done in monument rocks?"  -- needs filter + aggregate
#   "who baited rimi site last?"                -- needs a temporal superlative
#
# C1/C2 are those two questions, and C3 is the control that matters most: an
# OBSERVATION ("new foal in rimi's band") must never be returned as a chore.
# `viki ask` returned exactly that, which is why `notes` exists.
#
# THE REGRESSION THIS FILE EXISTS TO PIN (R1): the first implementation gave
# every capture in the same SECOND the same id, and the projection's
# INSERT OR REPLACE silently kept only the last -- 7 captured, 3 projected.
# It was even documented as acceptable. It is not: capture is the one
# operation that must never lose an input, and rapid capture is normal use.
# R1 captures a burst as fast as the shell can and asserts NOTHING is lost.
#
# Usage: sh build/capture-probe.sh <scratch-dir> [viki-binary]
set -e
DIR="${1:?usage: capture-probe.sh <scratch-dir> [viki-binary]}"
VIKI="${2:-$(cd "$(dirname "$0")/.." && pwd)/build/dist/viki}"
case "$VIKI" in /*) ;; *) echo "ERR: viki path must be ABSOLUTE"; exit 2 ;; esac
PASS=0; FAIL=0
LOGV="$DIR/.v1.err"; LOGV2="$DIR/.v2.err"
ck(){ if eval "$2" >/dev/null 2>&1; then PASS=$((PASS+1)); echo "  PASS  $1"; else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi; }

rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
# NO model anywhere: capture must work offline, on a phone, with nothing built.
export VIKI_MODEL_DIR=/nonexistent-on-purpose

"$VIKI" capture "camper needs propane"                        --type task --place "Monument Rocks" >/dev/null
"$VIKI" capture "new foal in rimi's band"                     --type observation --place "Rimi site" >/dev/null
"$VIKI" capture "buffalo wallow tank 3 is cracked"            --type task --place "monument rocks" >/dev/null
"$VIKI" capture "renee cannot lift heavy items"               --type rule --who Renee >/dev/null
"$VIKI" capture "put out mineral and bait"   --type task --place "Rimi site" --who Warren --state closed >/dev/null
"$VIKI" capture "salted and re-baited the station" --type task --place "Rimi site" --who Marta --state closed >/dev/null
"$VIKI" capture "bait station serviced again"      --type task --place "Rimi site" --who Hugh  --state closed >/dev/null
"$VIKI" index . >/dev/null 2>&1

ck "S1 capture works with NO model present"        '[ -f captures/*.md ]'
ck "S2 every capture reached the projection"       '[ "$("$VIKI" notes --k 99 2>/dev/null | grep -c "^[0-9]\{8\}-")" -eq 7 ]'
ck "C1 open tasks at a place, and ONLY those"      '[ "$("$VIKI" notes --place "monument rocks" --state open --type task 2>/dev/null | grep -c "^[0-9]")" -eq 2 ]'
ck "C1b CONTROL: a different place gives different work" \
   '! "$VIKI" notes --place "rimi site" --state open --type task 2>/dev/null | grep -q "propane"'
ck "C2 --last returns the MOST RECENT, not the best-worded" \
   '"$VIKI" notes --place "rimi site" --grep bait --last 2>/dev/null | grep -q "hugh"'
ck "C2b CONTROL: without --last the older ones are still there" \
   '[ "$("$VIKI" notes --place "rimi site" --grep bait 2>/dev/null | grep -c "^[0-9]")" -eq 3 ]'
ck "C3 an OBSERVATION is never returned as a chore" \
   '! "$VIKI" notes --state open --type task 2>/dev/null | grep -q "new foal"'
ck "C3b CONTROL: the observation IS retrievable as itself" \
   '"$VIKI" notes --type observation 2>/dev/null | grep -q "new foal"'
ck "C4 a RULE is not a chore either"               '! "$VIKI" notes --type task 2>/dev/null | grep -q "cannot lift"'
ck "N1 place spelling is normalised on both sides" \
   '[ "$("$VIKI" notes --place "MONUMENT   rocks" 2>/dev/null | grep -c "^[0-9]")" -eq 2 ]'
ck "N2 --who filters"                              '"$VIKI" notes --who marta 2>/dev/null | grep -q "salted"'
ck "N3 --since excludes the past"                  '[ "$("$VIKI" notes --since 2099-01-01 2>/dev/null | grep -c "^[0-9]")" -eq 0 ]'
ck "N4 --grep is a regex, not a substring"         '"$VIKI" notes --grep "re-?baited" 2>/dev/null | grep -q "salted"'

# R1 -- the data-loss regression, pinned. A burst as fast as the shell allows.
mkdir -p "$DIR/burst" && cd "$DIR/burst"
i=0; while [ $i -lt 25 ]; do "$VIKI" capture "burst note $i" --type task >/dev/null; i=$((i+1)); done
"$VIKI" index . >/dev/null 2>&1
ck "R1 a 25-note burst loses NOTHING (same-second ids)" \
   '[ "$("$VIKI" notes --k 99 2>/dev/null | grep -c "^[0-9]\{8\}-")" -eq 25 ]'
ck "R1b ... and every id is distinct" \
   '[ "$("$VIKI" notes --k 99 2>/dev/null | grep -o "^[0-9]\{8\}-[0-9]\{6\}-[0-9]\{6\}" | sort -u | wc -l)" -eq 25 ]'
ck "R2 rebuild is idempotent, not cumulative" \
   '"$VIKI" index . >/dev/null 2>&1; [ "$("$VIKI" notes --k 99 2>/dev/null | grep -c "^[0-9]\{8\}-")" -eq 25 ]'


# ---- the STRUCTURE half: viki finds the work, an agent judges, viki writes ----
mkdir -p "$DIR/st" && cd "$DIR/st"
"$VIKI" capture "utv tire is flat, left at low gap" >/dev/null
"$VIKI" capture "camper needs propane" >/dev/null
"$VIKI" index . >/dev/null 2>&1
FLAT=$("$VIKI" structure --pending 2>/dev/null | grep "tire is flat" | cut -f1)
ORIG_TEXT="$(grep -c 'utv tire is flat, left at low gap' captures/*.md)"

ck "ST1 --pending lists captures with no @type"     '[ "$("$VIKI" structure --pending 2>/dev/null | grep -c "^[0-9]")" -eq 2 ]'
ck "ST2 applying a type makes it queryable"         '"$VIKI" structure "$FLAT" --type task --place "Low Gap" --state open >/dev/null 2>&1; "$VIKI" index . >/dev/null 2>&1; "$VIKI" notes --place "low gap" --state open 2>/dev/null | grep -q "tire is flat"'
ck "ST2b CONTROL: --pending no longer lists it"     '! "$VIKI" structure --pending 2>/dev/null | grep -q "tire is flat"'
ck "ST3 the CAPTURED TEXT is byte-identical after a rewrite"    '[ "$(grep -c "utv tire is flat, left at low gap" captures/*.md)" -eq "$ORIG_TEXT" ]'
ck "ST3b the other note in the same file survived"  'grep -q "camper needs propane" captures/*.md'
ck "ST4 re-structuring does not duplicate @ lines"    '"$VIKI" structure "$FLAT" --place "Low Gap" >/dev/null 2>&1; [ "$(grep -c "^@place" captures/*.md)" -eq 1 ]'
ck "ST5 an unknown id fails, and says so"           '! "$VIKI" structure 99999999-999999-999999 --type task >/dev/null 2>&1'

# supersession: the fix retires the original
"$VIKI" capture "tire for utv fixed. parked at low gap." >/dev/null
"$VIKI" index . >/dev/null 2>&1
FIX=$("$VIKI" structure --pending 2>/dev/null | grep "tire for utv fixed" | cut -f1)
"$VIKI" structure "$FIX" --type task --place "Low Gap" --state closed --closes "$FLAT" >/dev/null 2>&1
"$VIKI" index . >/dev/null 2>&1
ck "ST6 --closes retires the TARGET note"           '! "$VIKI" notes --state open 2>/dev/null | grep -q "tire is flat"'
ck "ST6b ... and the link is recorded, not just the state"    '"$VIKI" notes --k 99 2>/dev/null >/dev/null; grep -q "^@closes $FLAT" captures/*.md'
ck "ST6c CONTROL: an unrelated open note is untouched"    '"$VIKI" structure --pending 2>/dev/null | grep -q "camper" || "$VIKI" notes 2>/dev/null | grep -q "camper"'
# ST6b proves the link is WRITTEN by grepping the capture file, which is
# exactly what a caller had to do before `notes --closes` existed: the
# supersession link was write-only, so "what retired this?" fell out of the
# projection and back onto the files. ST6d asks it as a QUERY instead.
ck "ST6d --closes ID names the note that RETIRED ID" \
   '[ "$("$VIKI" notes --closes "$FLAT" 2>/dev/null | grep -c "^$FIX")" -eq 1 ]'
ck "ST6e ... and it is the successor, not the retired note itself" \
   '! "$VIKI" notes --closes "$FLAT" 2>/dev/null | grep -q "tire is flat"'
ck "ST6f CONTROL: an id nothing retired matches NOTHING" \
   '[ "$("$VIKI" notes --closes "$FIX" 2>/dev/null | grep -c "^[0-9]\{8\}-")" -eq 0 ]'
ck "ST6g CONTROL: --closes narrows -- without it the note is still listed" \
   '"$VIKI" notes --k 99 2>/dev/null | grep -q "tire is flat"'

# ---- the @type vocabulary: warned, never enforced ----
mkdir -p "$DIR/vocab" && cd "$DIR/vocab"
"$VIKI" capture "invent a type" --type todo   2>"$LOGV" >/dev/null || true
ck "V1 an unknown @type WARNS"                  'grep -q "outside the known set" "$LOGV"'
ck "V1b ... and names the known set"            'grep -q "task observation rule schedule alert question note" "$LOGV"'
"$VIKI" capture "a real task" --type task 2>"$LOGV2" >/dev/null || true
ck "V2 CONTROL: a known @type does NOT warn"    '! grep -q "outside the known set" "$LOGV2"'
"$VIKI" index . >/dev/null 2>&1
ck "V3 an unknown type is KEPT, never rejected" '"$VIKI" notes --type todo 2>/dev/null | grep -q "invent a type"'

# ---- schema migration: a cache predating a column must MIGRATE, not fail ----
# CREATE TABLE IF NOT EXISTS does nothing to an existing table, so a column
# added later is absent from every older cache -- and the symptom is a query
# error that reads like a corrupt database. This bit twice in one day
# (viki_source.ts, then viki_note.closes), so it gets a standing assertion.
mkdir -p "$DIR/mig" && cd "$DIR/mig"
"$VIKI" capture "a note from before the column existed" --type task >/dev/null
"$VIKI" index . >/dev/null 2>&1
sqlite3 .viki/cache.db "ALTER TABLE viki_note RENAME TO n_old;
  CREATE TABLE viki_note(note_id TEXT PRIMARY KEY, ts TEXT NOT NULL, type TEXT, place TEXT,
    who TEXT, due TEXT, state TEXT, text TEXT NOT NULL, source_path TEXT);
  INSERT INTO viki_note SELECT note_id,ts,type,place,who,due,state,text,source_path FROM n_old;
  DROP TABLE n_old;" 2>/dev/null
ck "M1 a cache with no 'closes' column MIGRATES" '"$VIKI" notes --k 9 2>/dev/null | grep -q "before the column"'
ck "M1b ... and the column is really there afterwards" \
   '[ "$(sqlite3 .viki/cache.db "select count(*) from pragma_table_info(\"viki_note\") where name=\"closes\"")" -eq 1 ]'

# ---- claims, leases and stealing ----
# Every assertion here answers something three roleplay agents found by
# colliding (or nearly colliding) on a shared branch. L1/L1b are THE gap: all
# three independently discovered that `--state open` lists CLAIMED work,
# because claiming sets who and never touches state -- and that `--who ""`
# cannot express "unclaimed", since an empty filter means "no filter".
mkdir -p "$DIR/claim" && cd "$DIR/claim"
"$VIKI" capture "repair the west gate" --type task --state open >/dev/null
"$VIKI" capture "haul water north"     --type task --state open >/dev/null
"$VIKI" index . >/dev/null 2>&1
# notes orders NEWEST first, so name the task explicitly rather than taking
# the head and assuming which one it is.
CID=$("$VIKI" notes --type task --k 9 2>/dev/null | grep -B0 -A1 "^[0-9]" | grep -A1 "^[0-9]" | paste - - | grep "west gate" | cut -d" " -f1)
[ -n "$CID" ] || CID=$("$VIKI" notes --type task --k 9 2>/dev/null | grep "^[0-9]" | tail -1 | cut -d" " -f1)
"$VIKI" structure "$CID" --who alice --lease 1m >/dev/null 2>&1
"$VIKI" index . >/dev/null 2>&1

ck "L1 --state open still lists the CLAIMED one (the trap)" \
   '[ "$("$VIKI" notes --type task --state open --k 9 2>/dev/null | grep -c "^[0-9]")" -eq 2 ]'
ck "L1b --unclaimed excludes it -- the query agents actually need" \
   '[ "$("$VIKI" notes --type task --state open --unclaimed --k 9 2>/dev/null | grep -c "^[0-9]")" -eq 1 ]'
ck "L2 CAS: another agent cannot silently take a held task" \
   '! "$VIKI" structure "$CID" --who bob >/dev/null 2>&1'
ck "L2b ... and the refusal names the holder and the way in" \
   '"$VIKI" structure "$CID" --who bob 2>&1 | grep -q "already held by alice"'
ck "L2c CONTROL: --force still overrides, for a holder correcting itself" \
   '"$VIKI" structure "$CID" --who alice --lease 1m --force >/dev/null 2>&1'
ck "L3 a challenge is REFUSED while the lease is live (the niceness)" \
   '! "$VIKI" structure "$CID" --challenge bob >/dev/null 2>&1'
ck "L4 stealing is refused while the lease is live" \
   '! "$VIKI" structure "$CID" --steal bob --grace 1s >/dev/null 2>&1'

# now let the lease lapse
"$VIKI" structure "$CID" --who alice --lease 1s --force >/dev/null 2>&1
"$VIKI" index . >/dev/null 2>&1; sleep 2
ck "L5 --stale finds the lapsed claim"  '"$VIKI" notes --stale --k 9 2>/dev/null | grep -q "west gate"'
ck "L6 a challenge is allowed once the lease lapses" \
   '"$VIKI" structure "$CID" --challenge bob >/dev/null 2>&1'
"$VIKI" index . >/dev/null 2>&1
ck "L7 stealing STILL refused before the grace elapses" \
   '! "$VIKI" structure "$CID" --steal bob --grace 5m >/dev/null 2>&1'
ck "L8 the holder can answer: --heartbeat renews and clears the challenge" \
   '"$VIKI" structure "$CID" --heartbeat --lease 1m >/dev/null 2>&1; "$VIKI" index . >/dev/null 2>&1; ! "$VIKI" notes --k 9 2>/dev/null | grep -q CHALLENGED'

# lapse again, challenge, wait out the grace, steal
"$VIKI" structure "$CID" --who alice --lease 1s --force >/dev/null 2>&1
"$VIKI" index . >/dev/null 2>&1; sleep 2
"$VIKI" structure "$CID" --challenge bob >/dev/null 2>&1; "$VIKI" index . >/dev/null 2>&1; sleep 2
ck "L9 an unanswered challenge past its grace permits the steal" \
   '"$VIKI" structure "$CID" --steal bob --grace 1s --lease 1m >/dev/null 2>&1'
"$VIKI" index . >/dev/null 2>&1
ck "L9b the steal is RECORDED as a supersession, not an overwrite" \
   '"$VIKI" notes --k 9 2>/dev/null | grep -q "stolen-from:alice"'
ck "L9c ... and the new holder is in force"  '"$VIKI" notes --who bob --k 9 2>/dev/null | grep -q "west gate"'
ck "L10 GUARD: stealing an UNCLAIMED task is refused, not silently allowed" \
   'UID2=$("$VIKI" notes --unclaimed --k 9 2>/dev/null | grep "^[0-9]" | head -1 | cut -d" " -f1); ! "$VIKI" structure "$UID2" --steal carol >/dev/null 2>&1'
# The no-ceremony path. A lease is for callers who want precision; most
# claims will not have one, and the friction-free path must be the SAFE one.
# The first implementation had this backwards: an unleased claim was treated
# as instantly stale, punishing exactly the claimer least able to stop and
# estimate their own availability.
"$VIKI" capture "deliver hay to the north pasture" --type task --state open >/dev/null
"$VIKI" index . >/dev/null 2>&1
HAY=$("$VIKI" notes --type task --k 9 2>/dev/null | grep -A1 "^[0-9]" | paste - - | grep hay | cut -d" " -f1)
"$VIKI" structure "$HAY" --who warren >/dev/null 2>&1
"$VIKI" index . >/dev/null 2>&1
ck "L12 a plain claim needs NO lease"          '"$VIKI" notes --who warren --k 9 2>/dev/null | grep -q hay'
ck "L13 undeclared is NOT stale"               '! "$VIKI" notes --stale --k 9 2>/dev/null | grep -q hay'
ck "L13b ... but --stale AGE judges it by claim age" \
   'sleep 2; "$VIKI" notes --stale 1s --k 9 2>/dev/null | grep -q hay'
ck "L13c CONTROL: a generous age still spares it" \
   '! "$VIKI" notes --stale 30d --k 9 2>/dev/null | grep -q hay'
ck "L14 an unleased claim is still NOT unclaimed" \
   '! "$VIKI" notes --unclaimed --k 9 2>/dev/null | grep -q hay'
ck "L15 a bare --heartbeat needs no duration"  '"$VIKI" structure "$HAY" --heartbeat >/dev/null 2>&1'
ck "L16 an unleased claim can be challenged, and reports its age for judging" \
   '"$VIKI" structure "$HAY" --challenge marta 2>&1 | grep -q "declared no lease"'

ck "L11 GUARD: an unparseable --lease is refused, not treated as expired" \
   '! "$VIKI" structure "$CID" --who dave --force --lease "soon" >/dev/null 2>&1'

# ---- HTTP: the capture loop over the API, and the UI page ----
# Server lifecycle uses the lesson fragment-probe paid for: `exec` INSIDE the
# subshell so $! names the server and not a wrapper, plus a precondition that
# refuses to grade a port we do not own (a leaked server from a previous run
# would otherwise answer and be graded as if it were this build).
if command -v curl >/dev/null 2>&1; then
  mkdir -p "$DIR/http" && cd "$DIR/http"
  "$VIKI" capture "<img src=x onerror=alert(1)> hostile capture" >/dev/null
  "$VIKI" index . >/dev/null 2>&1
  PORT=18947
  ( exec "$VIKI" serve --port $PORT >/dev/null 2>&1 ) &
  SRV=$!
  sleep 2
  H="X-Viki-Local: 1"
  ck "H0 PRECONDITION: the server under test is ours" \
     '[ -n "$SRV" ] && kill -0 $SRV 2>/dev/null && curl -sf "http://127.0.0.1:'$PORT'/api/health" >/dev/null'
  ck "H1 /capture serves the UI page"            'curl -sf "http://127.0.0.1:'$PORT'/capture" | grep -q "viki capture"'
  ck "H2 the UI page contains NO innerHTML"       '[ "$(curl -s "http://127.0.0.1:'$PORT'/capture" | grep -c innerHTML)" -eq 0 ]'
  ck "H3 GET /api/pending lists untyped captures" 'curl -sf "http://127.0.0.1:'$PORT'/api/pending" | grep -q "hostile capture"'
  ck "H4 hostile text survives as DATA, escaped"  'curl -sf "http://127.0.0.1:'$PORT'/api/pending" | grep -q "onerror=alert(1)"'
  ck "H5 POST /api/capture with the header works" 'curl -sf -X POST -H "$H" "http://127.0.0.1:'$PORT'/api/capture?text=api+note&type=task" | grep -q "\"ok\":true"'
  ck "H6 POST /api/reindex projects it"           'curl -sf -X POST -H "$H" "http://127.0.0.1:'$PORT'/api/reindex" | grep -q "\"ok\":true"'
  ck "H7 GET /api/notes filters by type"          'curl -sf "http://127.0.0.1:'$PORT'/api/notes?type=task" | grep -q "api note"'
  ck "H7b GET /api/notes takes closes= too, so CLI and API cannot drift" \
     '[ "$(curl -sf "http://127.0.0.1:'$PORT'/api/notes?closes=nothing-retired-this" | sed -n "s/.*\"count\":\([0-9]*\).*/\1/p")" = "0" ]'
  ck "H8 GUARD: POST without the header is refused" \
     '[ "$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:'$PORT'/api/capture?text=driveby")" = "403" ]'
  ck "H8b ... and the drive-by did NOT capture anything" \
     '! curl -sf "http://127.0.0.1:'$PORT'/api/notes?k=99" | grep -q "driveby"'
  ck "H9 GUARD: GET on a mutating route is refused" \
     '[ "$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:'$PORT'/api/capture?text=nope")" = "405" ]'
  ck "H10 read-only routes still work unauthenticated" 'curl -sf "http://127.0.0.1:'$PORT'/api/health" | grep -q mode'
  kill $SRV 2>/dev/null || true
  wait $SRV 2>/dev/null || true
else
  echo "  SKIP  H0-H10 (no curl)"
fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
