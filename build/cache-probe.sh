#!/bin/sh
# cache-probe.sh -- the DISTRIBUTION path: `viki cache push` / `viki cache pull`.
#
# WHY THIS FILE EXISTS. test/m1.sh's C- and M- groups prove the uv round
# trip is lossless, but every assertion in them runs over `file://`, single
# writer, admin user, no HTTP and no capability check. Three real defects
# lived under that blind spot until an operations review reproduced them by
# hand:
#
#   G1  push exited 0 with the hub empty. Uploading unversioned content
#       needs the Fossil `y` capability; the server refuses without it, and
#       fossil's own sync_unversioned() discards the return value. The
#       operator flies to the field and debugs the phone.
#   G2  pull OVERWROTE the local cache -- `fossil uv export` is a bare
#       blob_write_to_file -- destroying anything this device had indexed
#       and not yet pushed.
#   G4  push published the bare .db while WAL held newer content, so a
#       long-lived local reader (the topology VIKIVERSE.md recommends)
#       made push publish a valid but STALE cache.
#
# Each is the project's signature failure: exit 0 while nothing happened.
# So each assertion here is paired with a control that must come out the
# other way, and the capability leg runs over real HTTP.
#
# Usage: sh build/cache-probe.sh <empty-dir>
set -u

DIR="${1:-}"
[ -n "$DIR" ] || { echo "usage: sh build/cache-probe.sh <empty-dir>" >&2; exit 2; }
mkdir -p "$DIR" || exit 2
DIR=$(cd "$DIR" && pwd)

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VIKI="${VIKI_BIN:-$ROOT/build/dist/viki}"
[ -x "$VIKI" ] || { echo "no $VIKI -- run build/build.sh first" >&2; exit 2; }

FOSSIL="${VIKI_FOSSIL_BIN:-}"
if [ -z "$FOSSIL" ]; then
    if command -v fossil-see >/dev/null 2>&1; then FOSSIL=fossil-see
    elif command -v fossil >/dev/null 2>&1; then FOSSIL=fossil
    else echo "no fossil; set VIKI_FOSSIL_BIN" >&2; exit 2; fi
fi
export VIKI_FOSSIL_BIN="$FOSSIL"
command -v sqlite3 >/dev/null 2>&1 || { echo "sqlite3 required" >&2; exit 2; }

nPass=0; nFail=0
ok(){  nPass=$((nPass+1)); printf '  ok   %s\n' "$1"; }
bad(){ nFail=$((nFail+1)); printf '  FAIL %s\n' "$1"; [ $# -gt 1 ] && printf '       %s\n' "$2"; }

export FOSSIL_HOME="$DIR/home"; mkdir -p "$FOSSIL_HOME"
export USER=viki
export VIKI_FOSSIL_USER=viki
export VIKI_MODEL_DIR="$DIR/no-model"      # BM25-only: this probe is about bytes moving
LOG="$DIR/log"; mkdir -p "$LOG"

HUB="$DIR/hub.fossil"
"$FOSSIL" init "$HUB" -A viki >"$LOG/init.out" 2>&1 || { echo "init failed"; exit 2; }

# ---- a spoke with content to publish -------------------------------------
# A spoke is a CLONE, not a checkout of the hub file itself. Opening the hub
# directly leaves no remote to sync to, so `uv add` writes straight into the
# hub and `uv sync` has nothing to do -- which would make every push
# assertion below pass for the wrong reason.
mk_spoke(){   # $1 = dir, $2 = unique text
    ( cd "$DIR" && "$FOSSIL" clone "file://$HUB" "$1.fossil" >>"$LOG/clone.out" 2>&1 )
    mkdir -p "$1" && ( cd "$1" && "$FOSSIL" open "$1.fossil" >>"$LOG/open.out" 2>&1 )
    printf '%s\n' "$2" > "$1/note.md"
    ( cd "$1" && "$VIKI" index . >>"$LOG/index.out" 2>&1 )
}
A="$DIR/spokeA"
mk_spoke "$A" "the mare at site two refused the trough after the storm"

echo "== G4: push must publish a SNAPSHOT, not the live WAL-backed file =="
# HONEST LIMIT: this leg asserts the published blob carries every live row,
# and that assertion is correct -- but it passes against the PRE-FIX binary
# too, so it does not discriminate. Reproducing the hazard needs a reader
# that holds the WAL open ACROSS the push, and a `sqlite3` invocation exits
# and checkpoints before push runs. The operations review reproduced it with
# a live `viki serve`; doing that here means supervising a background server
# for the length of the probe. Until then G4 documents the property and
# guards against regression in the snapshot mechanism, and the fix itself
# rests on that review's measurement rather than on this assertion.
# Force the hazard: hold a reader open so WAL is not checkpointed, then add
# content and push. Before the fix the pushed bytes lagged the live db.
( cd "$A" && sqlite3 .viki/cache.db "PRAGMA journal_mode=WAL;" >/dev/null 2>&1 )
printf 'a second note about the north gate chain freezing\n' >> "$A/note.md"
touch -t 203001010000 "$A/note.md"
( cd "$A" && "$VIKI" index . >>"$LOG/index2.out" 2>&1 )
LIVE=$( sqlite3 "$A/.viki/cache.db" "SELECT count(*) FROM viki_chunk;" )

echo "== push to the hub over file:// =="
( cd "$A" && "$VIKI" cache push --no-model >"$LOG/push.out" 2>&1 )
rc=$?
[ "$rc" -eq 0 ] && ok "P1 push over file:// succeeds" || bad "P1 push over file:// succeeds" "exit $rc"
HUBROWS=$( sqlite3 "$HUB" "SELECT count(*) FROM unversioned WHERE name='viki-cache.db';" 2>/dev/null )
[ "$HUBROWS" = "1" ] && ok "P2 the hub really holds the cache blob" \
                     || bad "P2 the hub really holds the cache blob" "unversioned rows=$HUBROWS"

# G4, asserted against the BYTES THAT LANDED rather than against a snapshot
# this script makes for itself -- the earlier form vacuumed with sqlite3 and
# so tested SQLite, not viki. Extract the published blob and count its rows
# while a WAL-holding reader is still open on the source.
rm -f "$DIR/landed.db"
( cd "$A" && "$FOSSIL" uv export viki-cache.db "$DIR/landed.db" >>"$LOG/uvexport.out" 2>&1 )
LANDED=$( sqlite3 "$DIR/landed.db" "SELECT count(*) FROM viki_chunk;" 2>/dev/null )
if [ -n "$LANDED" ] && [ "$LANDED" = "$LIVE" ]; then
    ok "G4 the PUBLISHED blob carries every live row ($LANDED), despite the open WAL"
else
    bad "G4 the PUBLISHED blob carries every live row, despite the open WAL" \
        "live=$LIVE published=$LANDED -- push published a valid but STALE cache"
fi

echo "== G2: pull must MERGE, never overwrite local work =="
B="$DIR/spokeB"
( cd "$DIR" && "$FOSSIL" clone "file://$HUB" "$B.fossil" >>"$LOG/cloneB.out" 2>&1 )
mkdir -p "$B" && ( cd "$B" && "$FOSSIL" open "$B.fossil" >>"$LOG/openB.out" 2>&1 )
# Local-only content this device indexed and has NOT pushed.
printf 'vermilion cliffs water haul takes ninety minutes each way\n' > "$B/local.md"
( cd "$B" && "$VIKI" index . >>"$LOG/indexB.out" 2>&1 )
LOCAL_ONLY=$( sqlite3 "$B/.viki/cache.db" \
    "SELECT count(*) FROM viki_chunk WHERE chunk_text LIKE '%vermilion%';" )
[ "$LOCAL_ONLY" -ge 1 ] && ok "G2 SETUP: spokeB has local-only work before the pull" \
                        || bad "G2 SETUP: spokeB has local-only work before the pull" "count=$LOCAL_ONLY"

( cd "$B" && "$VIKI" cache pull --no-model >"$LOG/pullB.out" 2>&1 )
rc=$?
[ "$rc" -eq 0 ] && ok "G2 pull succeeds" || bad "G2 pull succeeds" "exit $rc"

SURVIVED=$( sqlite3 "$B/.viki/cache.db" \
    "SELECT count(*) FROM viki_chunk WHERE chunk_text LIKE '%vermilion%';" )
[ "$SURVIVED" -ge 1 ] \
  && ok "G2 local-only work SURVIVED the pull (the reported bug)" \
  || bad "G2 local-only work SURVIVED the pull (the reported bug)" \
         "vermilion rows before=$LOCAL_ONLY after=$SURVIVED -- pull destroyed local indexing"

PULLED=$( sqlite3 "$B/.viki/cache.db" \
    "SELECT count(*) FROM viki_chunk WHERE chunk_text LIKE '%mare at site two%';" )
[ "$PULLED" -ge 1 ] \
  && ok "G2 CONTROL: the pulled content actually arrived (merge is a union)" \
  || bad "G2 CONTROL: the pulled content actually arrived" "remote rows=$PULLED"

# Retrieval must see both sides, not just the surviving table rows.
( cd "$B" && "$VIKI" ask "vermilion cliffs water haul" --k 1 >"$LOG/askB1.out" 2>&1 )
grep -q 'vermilion' "$LOG/askB1.out" \
  && ok "G2 local-only content is still RETRIEVABLE after the pull" \
  || bad "G2 local-only content is still RETRIEVABLE after the pull" "$(head -2 "$LOG/askB1.out")"
( cd "$B" && "$VIKI" ask "mare refused the trough" --k 1 >"$LOG/askB2.out" 2>&1 )
grep -q 'mare' "$LOG/askB2.out" \
  && ok "G2 pulled content is retrievable too (chunk_fts merged, not just viki_chunk)" \
  || bad "G2 pulled content is retrievable too" "$(head -2 "$LOG/askB2.out")"

# Idempotence: a second pull must not duplicate rows (fts5 has no unique key).
BEFORE2=$( sqlite3 "$B/.viki/cache.db" "SELECT count(*) FROM chunk_fts;" )
( cd "$B" && "$VIKI" cache pull --no-model >>"$LOG/pullB2.out" 2>&1 )
AFTER2=$( sqlite3 "$B/.viki/cache.db" "SELECT count(*) FROM chunk_fts;" )
[ "$BEFORE2" = "$AFTER2" ] \
  && ok "G2 a second pull is idempotent (no duplicate fts rows: $AFTER2)" \
  || bad "G2 a second pull is idempotent" "chunk_fts $BEFORE2 -> $AFTER2 (duplicates inflate BM25)"

echo "== G1: push must FAIL when the server refuses the upload =="
# Real HTTP, real capability check. This is the leg m1.sh does not have.
PORT=${VIKI_PROBE_PORT:-8765}
"$FOSSIL" user new noy noy@example.com secret -R "$HUB" >>"$LOG/user.out" 2>&1
# 'y' is what authorises unversioned upload; grant everything else.
"$FOSSIL" user capabilities noy ciorw -R "$HUB" >>"$LOG/user.out" 2>&1
CAPS=$( sqlite3 "$HUB" "SELECT cap FROM user WHERE login='noy';" 2>/dev/null )
case "$CAPS" in
  *y*) bad "G1 SETUP: the test user must LACK 'y'" "caps=$CAPS" ;;
  *)   ok "G1 SETUP: test user has caps [$CAPS], no 'y'" ;;
esac

"$FOSSIL" server "$HUB" --port "$PORT" --localhost >"$LOG/server.out" 2>&1 &
SRV=$!
sleep 2
if ! kill -0 "$SRV" 2>/dev/null; then
    bad "G1 fossil server started" "see $LOG/server.out"
else
    ok "G1 fossil server started on port $PORT"
    C="$DIR/spokeC"
    mkdir -p "$C"
    ( cd "$DIR" && "$FOSSIL" clone --save-http-password \
        "http://noy:secret@localhost:$PORT/" "$DIR/c.fossil" >>"$LOG/clone.out" 2>&1 )
    if [ -f "$DIR/c.fossil" ]; then
        ok "G1 SETUP: clone over http as the capability-limited user"
        ( cd "$C" && "$FOSSIL" open "$DIR/c.fossil" >>"$LOG/openC.out" 2>&1 )
        printf 'a note that must never reach a hub that refused it\n' > "$C/c.md"
        ( cd "$C" && "$VIKI" index . >>"$LOG/indexC.out" 2>&1 )
        ( cd "$C" && "$VIKI" cache push --no-model >"$LOG/pushC.out" 2>&1 )
        prc=$?
        # THE ASSERTION. Before the fix this was 0 with the hub empty.
        [ "$prc" -ne 0 ] \
          && ok "G1 push FAILS when the server refuses the upload (exit $prc)" \
          || bad "G1 push FAILS when the server refuses the upload" \
                 "exit 0 -- push reported success while publishing nothing"
        grep -qi "capability\|refused\|not authorized\|uv-pull-only" "$LOG/pushC.out" \
          && ok "G1 the failure NAMES the cause (the 'y' capability)" \
          || bad "G1 the failure NAMES the cause" "$(tail -3 "$LOG/pushC.out")"

        # CONTROL: grant 'y', same command, must now succeed and land.
        "$FOSSIL" user capabilities noy ciorwy -R "$HUB" >>"$LOG/user.out" 2>&1
        ( cd "$C" && "$VIKI" cache push --no-model >"$LOG/pushC2.out" 2>&1 )
        prc2=$?
        [ "$prc2" -eq 0 ] \
          && ok "G1 CONTROL: with 'y' granted the same push succeeds" \
          || bad "G1 CONTROL: with 'y' granted the same push succeeds" "exit $prc2"
        HUBHAS=$( sqlite3 "$HUB" \
            "SELECT count(*) FROM unversioned WHERE name='viki-cache.db' AND content IS NOT NULL;" 2>/dev/null )
        [ "$HUBHAS" = "1" ] \
          && ok "G1 CONTROL: and the bytes really landed on the hub" \
          || bad "G1 CONTROL: and the bytes really landed on the hub" "hub rows=$HUBHAS"
    else
        bad "G1 SETUP: clone over http as the capability-limited user" "see $LOG/clone.out"
    fi
    kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null
fi

printf '\n%d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
