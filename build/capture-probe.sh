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

# ---- the @type vocabulary: warned, never enforced ----
mkdir -p "$DIR/vocab" && cd "$DIR/vocab"
"$VIKI" capture "invent a type" --type todo   2>"$LOGV" >/dev/null || true
ck "V1 an unknown @type WARNS"                  'grep -q "outside the known set" "$LOGV"'
ck "V1b ... and names the known set"            'grep -q "task observation rule schedule alert question note" "$LOGV"'
"$VIKI" capture "a real task" --type task 2>"$LOGV2" >/dev/null || true
ck "V2 CONTROL: a known @type does NOT warn"    '! grep -q "outside the known set" "$LOGV2"'
"$VIKI" index . >/dev/null 2>&1
ck "V3 an unknown type is KEPT, never rejected" '"$VIKI" notes --type todo 2>/dev/null | grep -q "invent a type"'

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
