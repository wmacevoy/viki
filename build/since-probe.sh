#!/bin/sh
#
# since-probe.sh -- incremental indexing (`viki index --since`).
#
# WHY THIS EXISTS
# ---------------
# An after-receive hook runs SYNCHRONOUSLY in the pushing client's request
# path (QUEUE.md 28), so a full re-index there does not merely waste work --
# it stalls the push. `--since` is what makes a hook affordable.
#
# THE ASSERTION THAT MATTERS IS S5/S5b, NOT THE SPEED ONES.
# sweep_sources() retires every source it did not observe, and an incremental
# run deliberately does not observe what did not change. If it were
# authoritative it would delete almost the entire cache on every hook firing.
# S5 removes a real source and requires the incremental run to leave it alone;
# S5b requires a FULL pass to then retire it, so "never retires" is a property
# of the incremental path and not a broken sweep.
#
# Usage: sh build/since-probe.sh <scratch-dir> [viki-binary] [fossil-binary]
set -e
DIR="${1:?usage: since-probe.sh <scratch-dir> [viki] [fossil]}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VIKI="${2:-$ROOT/build/dist/viki}"
FOSSIL="${3:-$ROOT/vendor/fossil-see/build/dist/fossil-see}"
case "$VIKI" in /*) ;; *) echo "ERR: viki path must be ABSOLUTE"; exit 2 ;; esac
[ -x "$FOSSIL" ] || { echo "  SKIP (no fossil-see binary at $FOSSIL)"; exit 0; }
PASS=0; FAIL=0
ck(){ if eval "$2" >/dev/null 2>&1; then PASS=$((PASS+1)); echo "  PASS  $1"; else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi; }

rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
export FOSSIL_HOME="$DIR" FOSSIL_SEE_KEY=since-probe-key
export VIKI_FOSSIL_BIN="$FOSSIL" VIKI_MODEL_DIR=/nonexistent-on-purpose
"$FOSSIL" init hub.efossil >/dev/null 2>&1
mkdir co && cd co && "$FOSSIL" open ../hub.efossil --force >/dev/null 2>&1
printf 'the pump seal was replaced in March\n' > a.md
"$FOSSIL" add a.md >/dev/null 2>&1
"$FOSSIL" commit -m "first check-in about the pump seal" --user-override probe >/dev/null 2>&1
"$VIKI" index . >"$DIR/full1.out" 2>&1 || true

ck "S1 a FULL pass leaves an rcvid mark behind"      '[ -s .viki/rcvid.mark ]'
ck "S1b ... so --since auto can bootstrap itself"    'grep -q "mark advanced" "$DIR/full1.out"'
ck "S2 the mark is a SIBLING of cache.db, not inside it" \
   '[ -f .viki/rcvid.mark ] && ! "$FOSSIL" sql -R ../hub.efossil "select 1" >/dev/null 2>&1 || true; [ -f .viki/rcvid.mark ]'
ck "S2b the mark carries a project code, so a foreign one is detectable" \
   '[ "$(wc -w < .viki/rcvid.mark)" -eq 2 ]'

"$VIKI" index . --since auto >"$DIR/quiet.out" 2>&1 || true
ck "S3 nothing arrived -> every extractor is skipped" 'grep -q "skipping all extractors" "$DIR/quiet.out"'
ck "S3b ... and it says so rather than silently doing nothing" 'grep -q "no artifacts received since" "$DIR/quiet.out"'

# a real arrival
printf 'north tank is cracked, haul a new one\n' > b.md
"$FOSSIL" add b.md >/dev/null 2>&1
"$FOSSIL" commit -m "second check-in about the north tank" --user-override probe >/dev/null 2>&1
"$VIKI" index . --since auto >"$DIR/inc.out" 2>&1 || true
ck "S4 an arrival IS indexed incrementally"          '"$VIKI" grep "north tank" --source "ckin:%" | grep -q "north tank"'
ck "S4b ... and the run announced it was incremental" 'grep -q "incremental, rcvid >" "$DIR/inc.out"'
ck "S4c ... and older content is untouched"           '"$VIKI" grep "pump seal" --source "ckin:%" | grep -q "pump seal"'

# THE SAFETY PROPERTY: an incremental run must retire NOTHING.
rm -f a.md
"$VIKI" index . --since 0 >"$DIR/inc2.out" 2>&1 || true
ck "S5 an incremental run retires NOTHING, even with a source deleted" \
   'grep -q "0 stale source(s) retired" "$DIR/inc2.out"'
ck "S5b ... and says plainly that it is not authoritative" \
   'grep -q "NOT authoritative" "$DIR/inc2.out"'
ck "S5c CONTROL: a FULL pass DOES retire it, so the sweep is not broken" \
   '"$VIKI" index . >"$DIR/full2.out" 2>&1; grep -qE "[1-9][0-9]* stale source\(s\) retired" "$DIR/full2.out"'

# a mark from another project must be refused, not trusted
echo "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef 99999" > .viki/rcvid.mark
"$VIKI" index . --since auto >"$DIR/foreign.out" 2>&1 || true
ck "S6 a mark from a DIFFERENT project is refused"   'grep -q "belongs to project" "$DIR/foreign.out"'
ck "S6b ... and it falls back to a full pass rather than skipping content" \
   '! grep -q "skipping all extractors" "$DIR/foreign.out"'

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
