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
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
chk(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (got '$2' want '$3')"; fi; }

cd "$DIR"
LATE=$(date -u -v-2d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '2 days ago' +%Y-%m-%dT12:00:00Z)
SOON=$(date -u -v+3d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '3 days' +%Y-%m-%dT12:00:00Z)
FAR=$(date -u -v+90d +%Y-%m-%dT12:00:00Z 2>/dev/null || date -u -d '90 days' +%Y-%m-%dT12:00:00Z)

# The id comes from `capture`'s OWN output, not from re-reading --pending.
# Guessing it as "the first pending note" is wrong the moment more than one is
# pending, which is every call after the first -- and it fails silently, so the
# probe reports a ledger bug that is really a test bug.
cap(){
  id=$("$V" capture "$1" 2>/dev/null | awk '{print $1; exit}')
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

echo "== coverage is stated, not implied (2.5) =="
chk "P9 the ledger says what it can and cannot see" \
    "$(printf '%s' "$OUT2" | grep -c 'captured and ingested notes only')" "1"

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
