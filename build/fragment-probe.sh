#!/bin/sh
#
# fragment-probe.sh -- end-to-end probe for DISPLAY-SIDE FRAGMENT MARKING
# in `viki ask`, `viki serve` and `viki grep`. See src/viki_ask.h
# (VIKI_FRAG_*) for the rule; this file is its standing proof.
#
# WHY THIS EXISTS
# ---------------
# `viki index` slices a document into 40-line chunks and stores each slice
# raw. Retrieval then hands one slice back as if it were a whole text: a
# chunk taken from the middle of a document opens mid-sentence and closes
# mid-sentence, and nothing in the output says so. That was a cosmetic
# wart until `viki ask` started printing a citable content_hash -- an
# agent can now quote the dangling fragment *precisely*, which makes it a
# provenance defect. The fix is display-only: nothing stored changes, no
# re-indexing is needed, and this probe asserts the display.
#
# The rule under test:
#   * fragment at the HEAD  <=>  chunk_ix > 0
#   * fragment at the TAIL  <=>  chunk_ix < max(chunk_ix) for that content
#   * an excerpt that is a truncated PREFIX of its chunk is marked
#     separately, because that is a different fact at a different scope.
#
# Traps this guards, each a real way the feature goes wrong:
#   * off-by-one: a SINGLE-CHUNK document is chunk 0 AND the last chunk,
#     so it must get NEITHER marker (F11/F12). This is the assertion most
#     likely to catch a `<=` where a `<` belongs, and F10 keeps it from
#     passing vacuously on empty output.
#   * one-sidedness: "no head marker on chunk 0" also holds if markers
#     never print at all, so F5 is paired with F6 and F8 with F9 -- each
#     absence is asserted next to a presence on the SAME hit.
#   * double-marking: FTS5's snippet() ALREADY writes ' ... ' where it
#     elided text from INSIDE the chunk. That is not fragmentation, and
#     the two notations must coexist without merging or duplicating
#     (F14/F15).
#   * the citation line: three other test files parse
#     `[N] rrf=X.XXXX  <hash>#<ix>  <source>` by position, so the markers
#     must land on the excerpt line and nowhere else (F13).
#   * the JSON API: a client already parsing `snippet` must not silently
#     start receiving viki's decoration mixed into indexed content (S3).
#   * the HTML page: it renders untrusted indexed content, so adding
#     markers must not reach for innerHTML (S5) -- asserted twice, once
#     as a string in the source (S5) and once as a RUNTIME property, by
#     rendering through a DOM whose innerHTML throws (S8-S15).
#   * the human surface citing nothing: `/api/ask` carries "hash" and the
#     page fetched it and dropped it, so the CLI could cite and a person
#     could not (S7-S15). Grepping the page source for `hit.hash` would
#     prove only that a string is in a file, so S8-S15 run the page's OWN
#     <script> against the API's OWN JSON and grade the rendered text.
#   * a second surface drifting: `viki grep` prints the same kind of
#     excerpt under the same citable header, so it must mark the same
#     facts with the SAME strings, not lookalikes of its own (R1-R8).
#
# Usage: sh build/fragment-probe.sh <scratch-dir> [viki-binary]
#   VIKI_MODEL_DIR   as everywhere else; absent => the hybrid checks SKIP
#   VIKI_PROBE_PORT  port for the `viki serve` checks (default 18771)
#   node on PATH     absent => S8-S15 SKIP rather than pass: without a JS
#                    engine the page's rendering cannot be measured, only
#                    grepped, and a grep is not evidence here
set -e
DIR="${1:?usage: fragment-probe.sh <scratch-dir> [viki-binary]}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
VIKI="${2:-$REPO/build/dist/viki}"
case "$VIKI" in /*) ;; *) echo "ERR: viki path must be ABSOLUTE"; exit 2 ;; esac
[ -x "$VIKI" ] || { echo "ERR: no viki binary at $VIKI"; exit 2; }
MODEL="${VIKI_MODEL_DIR:-$REPO/build/dist/model}"
PORT="${VIKI_PROBE_PORT:-18771}"

PASS=0; FAIL=0; SKIP=0
ck(){ if eval "$2" >/dev/null 2>&1; then PASS=$((PASS+1)); echo "  PASS  $1"; else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi; }
skip(){ SKIP=$((SKIP+1)); echo "  SKIP  $1 ($2)"; }

SERVE_PID=""
cleanup(){ [ -n "$SERVE_PID" ] && kill "$SERVE_PID" 2>/dev/null; return 0; }
trap cleanup EXIT INT TERM

rm -rf "$DIR"
mkdir -p "$DIR/bm25/docs" "$DIR/vec/docs" "$DIR/nomodel" "$DIR/logs"
LOG="$DIR/logs"

# ------------------------------------------------------------- the corpus --
# A MULTI-CHUNK document. It used to say "THREE chunks exactly", computed from
# 120 lines at 40 lines per chunk -- correct until chunking gained an OVERLAP
# (embed.h, 2026-08-26), after which the same 120 lines are FOUR windows and
# three assertions failed on arithmetic rather than on behaviour.
#
# So the counts are now READ BACK from the cache after indexing (LAST_IX /
# N_CHUNK below) instead of being predicted here. What this file tests is
# MARKING -- does a middle chunk say it has text above and below, does the last
# chunk omit the tail marker -- and none of that should depend on how many
# chunks a given build produces.
#
# The three planted tokens are still one per region and unique in the corpus,
# so a one-word query names one chunk with no ranking assumptions. Line numbers
# are deliberately not on a chunk boundary: a token AT the boundary would not
# distinguish a correct implementation from one that is off by one chunk.
# gammamarker sits at line 110 rather than 100 because with a 40-line window
# and a 30-line stride, lines 101-120 are the only ones the FINAL window does
# not share with its predecessor -- which is what "the last chunk" has to mean
# for F7 to be about markers and not about overlap.
i=1
while [ "$i" -le 120 ]; do
    case "$i" in
        5)   echo "The alphamarker survey was filed with the county clerk." ;;
        50)  echo "The betamarker inspection found corrosion on the flange." ;;
        110) echo "The gammamarker rebuild finished ahead of the estimate." ;;
        *)   echo "Routine line $i of the maintenance log, nothing notable recorded." ;;
    esac
    i=$((i+1))
done > "$DIR/bm25/docs/long.md"

# ONE chunk. The off-by-one case: chunk 0 is also the LAST chunk here.
cat > "$DIR/bm25/docs/short.md" <<'EOF'
A short standalone note, well under one chunk.
The soloparagraph decision was recorded in March.
EOF

# ONE chunk whose FTS5 snippet is far larger than the 512-byte excerpt
# buffer in viki_ask_result -- 24 tokens of 60 characters each blows past
# it. This exercises the OTHER source of a truncated excerpt (the buffer,
# not the vector leg's substr), with no model required. Being a
# single-chunk document, it is also the control that says truncation and
# fragmentation are independent facts: F16 wants the truncation marker and
# NO fragment markers on the very same hit.
LONGWORD=qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq
{
    printf 'Preamble line of the wide document.\n'
    i=1
    while [ "$i" -le 15 ]; do printf '%s%02d ' "$LONGWORD" "$i"; i=$((i+1)); done
    printf 'needlelongword '
    while [ "$i" -le 30 ]; do printf '%s%02d ' "$LONGWORD" "$i"; i=$((i+1)); done
    printf '\n'
} > "$DIR/bm25/docs/wide.md"

cp "$DIR/bm25/docs/long.md" "$DIR/vec/docs/long.md"   # hybrid phase: ONE document,
                                                      # so `#<ix>` alone names a chunk

( cd "$DIR/bm25" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" index . ) \
    >"$LOG/index.out" 2>"$LOG/index.err" || true

# READ THE LAYOUT BACK rather than predicting it. `viki sql` is used instead of
# a stock sqlite3 so this needs no extra dependency and works on any cache viki
# can open. A build with different chunk params changes these numbers and must
# not change a single assertion below.
layout(){ "$VIKI" sql "$1" 2>/dev/null | tail -1 | tr -d ' '; }
N_CHUNK=$( cd "$DIR/bm25" && layout "SELECT count(*) FROM viki_chunk c
             JOIN viki_source s ON s.content_hash=c.content_hash
             WHERE s.path LIKE '%long.md'" )
LAST_IX=$( cd "$DIR/bm25" && layout "SELECT max(chunk_ix) FROM viki_chunk c
             JOIN viki_source s ON s.content_hash=c.content_hash
             WHERE s.path LIKE '%long.md'" )
: "${N_CHUNK:=3}" "${LAST_IX:=2}"
printf '  layout: long.md is %s chunk(s), last index %s\n' "$N_CHUNK" "$LAST_IX"

# ------------------------------------------------------------- helpers --
# BM25-only and --k 1: every query below names a token that occurs in
# exactly one chunk of the whole corpus, so the FTS leg returns exactly
# one row and the output holds exactly one hit. Nothing here depends on
# ranking, on RRF, or on a model being present.
ask(){ _o=$1; shift
    ( cd "$DIR/bm25" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" ask "$*" --k 1 ) \
        >"$LOG/$_o.out" 2>"$LOG/$_o.err" || true
}
# hit_ix <out> -- the chunk_ix the single hit's header line names.
hit_ix(){ grep -E '^\[1\] rrf=' "$1" | head -1 | awk '{print $3}' | cut -d'#' -f2; }
# nmark <out> <string> -- OCCURRENCES, not lines: an excerpt carries raw
# newlines from the source, so a marker at each end can share a line or
# not depending on the text, and `grep -c` would count either as 1.
nmark(){ grep -o -- "$2" "$1" 2>/dev/null | wc -l; }

HEAD_MARK='<<document continues above>>'
TAIL_MARK='<<document continues below>>'
CUT_MARK='<<excerpt truncated>>'

echo "== A. viki ask: a chunk from the MIDDLE of a document is marked at both ends =="
ask mid betamarker
ck "F1 SETUP: the betamarker query lands on chunk #1 (the middle one)" \
   '[ "$(hit_ix "$LOG/mid.out")" = "1" ]'
ck "F2 the middle chunk is marked at its HEAD" \
   '[ "$(nmark "$LOG/mid.out" "$HEAD_MARK")" -eq 1 ]'
ck "F3 the middle chunk is marked at its TAIL" \
   '[ "$(nmark "$LOG/mid.out" "$TAIL_MARK")" -eq 1 ]'

echo "== B. the FIRST chunk has no head marker -- and still has a tail one =="
ask first alphamarker
ck "F4 SETUP: the alphamarker query lands on chunk #0 (the first one)" \
   '[ "$(hit_ix "$LOG/first.out")" = "0" ]'
ck "F5 CONTROL: the FIRST chunk carries NO head marker" \
   '[ "$(nmark "$LOG/first.out" "$HEAD_MARK")" -eq 0 ]'
ck "F6 ... but it DOES carry a tail marker (F5 is not 'markers never print')" \
   '[ "$(nmark "$LOG/first.out" "$TAIL_MARK")" -eq 1 ]'

echo "== C. the LAST chunk has no tail marker -- and still has a head one =="
ask last gammamarker
ck "F7 SETUP: the gammamarker query lands on the LAST chunk" \
   '[ "$(hit_ix "$LOG/last.out")" = "$LAST_IX" ]'
ck "F8 CONTROL: the LAST chunk carries NO tail marker" \
   '[ "$(nmark "$LOG/last.out" "$TAIL_MARK")" -eq 0 ]'
ck "F9 ... but it DOES carry a head marker (F8 is not 'markers never print')" \
   '[ "$(nmark "$LOG/last.out" "$HEAD_MARK")" -eq 1 ]'

echo "== D. a SINGLE-CHUNK document has NEITHER marker (the off-by-one case) =="
ask solo soloparagraph
ck "F10 SETUP: the single-chunk document returns a hit at all (F11/F12 are not vacuous)" \
   '[ -s "$LOG/solo.out" ] && [ "$(hit_ix "$LOG/solo.out")" = "0" ]'
ck "F11 a single-chunk document carries NO head marker" \
   '[ "$(nmark "$LOG/solo.out" "$HEAD_MARK")" -eq 0 ]'
ck "F12 a single-chunk document carries NO tail marker (chunk 0 IS the last chunk)" \
   '[ "$(nmark "$LOG/solo.out" "$TAIL_MARK")" -eq 0 ]'

echo "== E. the markers do not disturb the citation line, or FTS5's own elision =="
# The header line is a CITATION: `<hash>#<ix>` is exactly what
# /api/chunk?hash=&ix= takes, and test/m1.sh (G1-G6),
# build/forum-e2e-probe.sh (C6) and build/model-uv-e2e-probe.sh (F11) all
# parse it by position. Marking belongs on the excerpt line only.
ck "F13 the header line is untouched: [N] rrf=X.XXXX  <64hex>#<ix>  <source>" \
   'head -1 "$LOG/mid.out" | grep -qE "^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#1  \./docs/long\.md$"'
# FTS5 writes ' ... ' where it dropped text from INSIDE this chunk. That
# is intra-chunk elision, not fragmentation, and the two notations are
# deliberately kept in disjoint alphabets -- dots for one, angle brackets
# for the other -- so a reader cannot collapse them into a single vague
# "something is missing". If a marker ever grows an ellipsis, F14 fails.
ck "F14 no fragment marker contains an ellipsis (disjoint from FTS5's ' ... ')" \
   '! grep -o "<<[^>]*>>" "$LOG/mid.out" | grep -q "\."'
ck "F15 both notations appear on the same hit, once each -- no doubling up" \
   'grep -q " \.\.\. " "$LOG/mid.out" \
    && [ "$(nmark "$LOG/mid.out" "$HEAD_MARK")" -eq 1 ] \
    && [ "$(nmark "$LOG/mid.out" "$TAIL_MARK")" -eq 1 ]'

echo "== F. a truncated EXCERPT is a different fact from a fragmented CHUNK =="
ask wide needlelongword
ck "F16 an excerpt cut short by the display buffer says so" \
   '[ "$(nmark "$LOG/wide.out" "$CUT_MARK")" -eq 1 ]'
ck "F17 ... on a SINGLE-CHUNK document, so it carries no fragment marker (independent facts)" \
   '[ "$(hit_ix "$LOG/wide.out")" = "0" ] \
    && [ "$(nmark "$LOG/wide.out" "$HEAD_MARK")" -eq 0 ] \
    && [ "$(nmark "$LOG/wide.out" "$TAIL_MARK")" -eq 0 ]'
ck "F18 CONTROL: an excerpt that fits is NOT marked truncated" \
   '[ "$(nmark "$LOG/solo.out" "$CUT_MARK")" -eq 0 ]'

echo "== G. the VECTOR leg's excerpt is a truncated prefix, and says so =="
# The vector leg has no snippet() -- there is no keyword match to build a
# window around -- so it shows substr(chunk_text,1,140), a raw PREFIX.
# That prefix is cut short on any chunk longer than 140 characters, which
# is nearly all of them, and the cut deserves its own marker REGARDLESS of
# chunk_ix. Reaching it needs a query the FTS leg cannot answer (otherwise
# the FTS snippet wins the excerpt) and a model to answer it with.
if [ -f "$MODEL/model.onnx" ] && [ -f "$MODEL/vocab.txt" ] && [ -f "$MODEL/viki-manifest.json" ]; then
    ( cd "$DIR/vec" && VIKI_MODEL_DIR="$MODEL" "$VIKI" index . ) \
        >"$LOG/vindex.out" 2>"$LOG/vindex.err" || true
    VLAST_IX=$( cd "$DIR/vec" && VIKI_MODEL_DIR="$MODEL" layout \
        "SELECT max(chunk_ix) FROM viki_chunk c
           JOIN viki_source s ON s.content_hash=c.content_hash
          WHERE s.path LIKE '%long.md'" )
    : "${VLAST_IX:=2}"
    # No token here occurs anywhere in the corpus, so the BM25 leg is
    # empty and every hit -- and every excerpt -- comes from rung 2.
    ( cd "$DIR/vec" && VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "zzqxwv unrelatedtoken" --k 3 ) \
        >"$LOG/v.out" 2>"$LOG/v.err" || true
    # $DIR/vec holds ONE document, so `#<ix>  ` names one chunk. Extract
    # the excerpt block by header, not by line offset: excerpts carry raw
    # newlines, so "the line after the header" is not a block.
    vblock(){ awk -v key="#$1  " '
        /^\[[0-9]+\] rrf=/ { inb = (index($0, key) > 0); next }
        inb && $0 == "" { inb = 0; next }
        inb { print }' "$LOG/v.out"; }
    ck "V1 SETUP: the vector leg answered a query the keyword leg cannot" \
       'grep -q "hybrid mode" "$LOG/v.err" && [ "$(grep -cE "^\[[0-9]+\] rrf=" "$LOG/v.out")" -eq 3 ]'
    ck "V2 the vector leg's 140-char prefix excerpt is marked truncated" \
       '[ "$(vblock 1 | grep -o -- "$CUT_MARK" | wc -l)" -eq 1 ]'
    ck "V3 ... and that same hit is still marked at both ends (chunk #1 of 3)" \
       '[ "$(vblock 1 | grep -o -- "$HEAD_MARK" | wc -l)" -eq 1 ] \
        && [ "$(vblock 1 | grep -o -- "$TAIL_MARK" | wc -l)" -eq 1 ]'
    ck "V4 CONTROL: the FIRST chunk is truncated but has NO head marker" \
       '[ "$(vblock 0 | grep -o -- "$CUT_MARK" | wc -l)" -eq 1 ] \
        && [ "$(vblock 0 | grep -o -- "$HEAD_MARK" | wc -l)" -eq 0 ] \
        && [ "$(vblock 0 | grep -o -- "$TAIL_MARK" | wc -l)" -eq 1 ]'
    ck "V5 CONTROL: the LAST chunk is truncated but has NO tail marker" \
       '[ "$(vblock $VLAST_IX | grep -o -- "$CUT_MARK" | wc -l)" -eq 1 ] \
        && [ "$(vblock $VLAST_IX | grep -o -- "$HEAD_MARK" | wc -l)" -eq 1 ] \
        && [ "$(vblock $VLAST_IX | grep -o -- "$TAIL_MARK" | wc -l)" -eq 0 ]'
else
    for t in "V1 SETUP: the vector leg answered a query the keyword leg cannot" \
             "V2 the vector leg's 140-char prefix excerpt is marked truncated" \
             "V3 ... and that same hit is still marked at both ends (chunk #1 of 3)" \
             "V4 CONTROL: the FIRST chunk is truncated but has NO head marker" \
             "V5 CONTROL: the LAST chunk is truncated but has NO tail marker"
    do
        skip "$t" "no embedding model at $MODEL"
    done
fi

echo "== H. viki serve: JSON booleans, an UNDECORATED snippet, and no innerHTML =="
# PORT HYGIENE, and it is not paranoia -- it was a live bug in this file.
# `viki serve` sets SO_REUSEADDR but not SO_REUSEPORT, so a second server
# on a busy port fails to bind and exits, while curl happily talks to
# whatever is ALREADY listening. Measured: an earlier draft leaked its
# server (it backgrounded a subshell, so $! named the subshell and `kill`
# left the real process orphaned), and the next run's JSON assertions
# silently graded the leaked binary instead of the one under test -- a
# probe that passes by testing the wrong program. Two fixes, both needed:
# `exec` below, so $! IS the server, and this precondition, so a foreign
# listener is a loud failure rather than a false pass.
PORT_BUSY=0
if command -v curl >/dev/null 2>&1; then
    if curl -s -o /dev/null --max-time 2 "http://127.0.0.1:$PORT/api/health"; then PORT_BUSY=1; fi
fi
if [ "$PORT_BUSY" = 1 ]; then
    FAIL=$((FAIL+1))
    echo "  FAIL  S-1 SETUP: port $PORT already has a listener -- refusing to grade it"
    echo "        (set VIKI_PROBE_PORT, or stop the stray server; the remaining S"
    echo "         assertions are SKIPPED rather than measured against it)"
    for t in "S0 SETUP: the JSON API answered both queries" \
             "S1 a middle chunk reports fragment_head AND fragment_tail true" \
             "S2 CONTROL: a single-chunk document reports BOTH false" \
             "S3 the JSON snippet is NOT decorated (booleans, not string mutation)" \
             "S4 chunk_count reports the real extent (the measured count, and 1)" \
             "S5 the HTML page still contains no innerHTML anywhere" \
             "S6 the page renders the SAME marker strings the CLI prints" \
             "S7 the page's renderer reads hit.hash at all" \
             "S8 SETUP: the page's OWN script rendered the API's OWN JSON" \
             "S9 the rendered page carries the content_hash, abbreviated, with #<ix>" \
             "S10 CONTROL: ... abbreviated -- the full 64 hex is NOT on screen unasked" \
             "S11 clicking the citation expands it to the CLI's EXACT citation" \
             "S12 clicking again collapses it back to the abbreviation" \
             "S13 CONTROL: a hit with no hash renders NO citation, not half of one" \
             "S14 CONTROL: a different hash renders differently (data, not a constant)" \
             "S14b CONTROL: a different chunk_ix renders differently, too" \
             "S15 CONTROL: the runtime innerHTML detector is live (a patched page fails it)"
    do
        skip "$t" "port $PORT is not ours"
    done
elif command -v curl >/dev/null 2>&1; then
    # `exec` matters: without it the subshell forks a child for viki and
    # $! names the SUBSHELL, so the trap kills a wrapper and leaves the
    # server running. With it, the subshell becomes the server.
    (
        cd "$DIR/bm25"
        VIKI_MODEL_DIR="$DIR/nomodel"
        export VIKI_MODEL_DIR
        exec "$VIKI" serve --port "$PORT"
    ) >"$LOG/serve.out" 2>"$LOG/serve.err" &
    SERVE_PID=$!
    i=0
    while [ "$i" -lt 15 ]; do
        if curl -s -o /dev/null "http://127.0.0.1:$PORT/api/health"; then break; fi
        i=$((i+1)); sleep 1
    done
    curl -s "http://127.0.0.1:$PORT/api/ask?q=betamarker&k=1" >"$LOG/api.mid.json" 2>/dev/null || true
    curl -s "http://127.0.0.1:$PORT/api/ask?q=soloparagraph&k=1" >"$LOG/api.solo.json" 2>/dev/null || true
    curl -s "http://127.0.0.1:$PORT/" >"$LOG/page.html" 2>/dev/null || true

    ck "S0 SETUP: the JSON API answered both queries" \
       'grep -q "\"hash\"" "$LOG/api.mid.json" && grep -q "\"hash\"" "$LOG/api.solo.json"'
    ck "S1 a middle chunk reports fragment_head AND fragment_tail true" \
       'grep -q "\"fragment_head\":true" "$LOG/api.mid.json" \
        && grep -q "\"fragment_tail\":true" "$LOG/api.mid.json"'
    ck "S2 CONTROL: a single-chunk document reports BOTH false" \
       'grep -q "\"fragment_head\":false" "$LOG/api.solo.json" \
        && grep -q "\"fragment_tail\":false" "$LOG/api.solo.json"'
    # THE point of choosing booleans over decoration: a client that
    # already parses `snippet` -- to quote it, diff it, feed it to a model
    # -- must not silently start receiving marker words it cannot tell
    # from indexed content. `<<` appears in no marker-free JSON body.
    ck "S3 the JSON snippet is NOT decorated (booleans, not string mutation)" \
       '! grep -q "<<" "$LOG/api.mid.json"'
    ck "S4 chunk_count reports the real extent (the measured count, and 1)" \
       'grep -q "\"chunk_count\":$N_CHUNK" "$LOG/api.mid.json" \
        && grep -q "\"chunk_count\":1" "$LOG/api.solo.json"'
    # The page renders indexed content, which is untrusted markup. Adding
    # markers must not have reached for innerHTML to do it.
    ck "S5 the HTML page still contains no innerHTML anywhere" \
       '[ -s "$LOG/page.html" ] && ! grep -q "innerHTML" "$LOG/page.html"'
    ck "S6 the page renders the SAME marker strings the CLI prints" \
       'grep -q -- "$HEAD_MARK" "$LOG/page.html" \
        && grep -q -- "$TAIL_MARK" "$LOG/page.html" \
        && grep -q -- "$CUT_MARK" "$LOG/page.html"'
    kill "$SERVE_PID" 2>/dev/null || true
    SERVE_PID=""

    # ------------------------------------------------------------------
    # THE CITATION. `/api/ask` has always carried "hash"; until 2026-08-26
    # the page fetched it and threw it away, so the one HUMAN surface could
    # not cite what it found (KICKOFF.md deliverable 2, met on the CLI and
    # the JSON API only). These assertions are that gap's standing proof.
    #
    # S7 is static and therefore weak on purpose -- `grep hit.hash` proves
    # a string is in a file, not that a browser renders it, and this
    # project's signature failure is a green result that proves nothing.
    # So S8-S15 EXECUTE the page's own <script> against the API's own JSON
    # in a minimal DOM (render.js below) and grade the TEXT THAT COMES OUT.
    # That is also what makes the no-innerHTML claim real: the shim's
    # innerHTML is a throwing accessor, so any renderer that reaches for it
    # takes every one of S8-S15 down with it, and S15 proves that detector
    # is live rather than decorative by patching the page and watching it
    # fail.
    ck "S7 the page's renderer reads hit.hash at all" \
       'grep -q "hit\.hash" "$LOG/page.html"'

    if command -v node >/dev/null 2>&1; then
        cat > "$DIR/render.js" <<'RENDER_JS'
/* Run viki serve's OWN page script against real /api/ask JSON in a minimal
** DOM, and print the text the page would show. argv:
**   render.js <page.html> <api.json> [plain|click|nohash|rehash] [twice]
** innerHTML is a throwing accessor: a renderer that uses it aborts here. */
var fs = require('fs'), vm = require('vm');
var html = fs.readFileSync(process.argv[2], 'utf8');
var m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.error('NO-SCRIPT'); process.exit(3); }
var data = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
var mode = process.argv[4] || 'plain';
if (mode === 'nohash') data.results.forEach(function(h){ delete h.hash; });
if (mode === 'rehash') data.results.forEach(function(h){
    h.hash = 'ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff'; });
/* `reix` exists because the fixture is --k 1 on a hit whose chunk_ix HAPPENS
** to be 1, so every citation assertion matched the literal '#1' and none of
** them could tell `'#' + hit.chunk_ix` from a hardcoded '#1'. Half the
** citation was ungraded. 77 is chosen to be a value no fixture produces. */
if (mode === 'reix') data.results.forEach(function(h){ h.chunk_ix = 77; });
var made = [];
function Node(tag){ this.tag = tag; this.children = []; this._text = null; this.handlers = {}; }
Object.defineProperty(Node.prototype, 'textContent', {
    get: function(){ return this._text !== null ? this._text
        : this.children.map(function(c){ return c.textContent; }).join(''); },
    set: function(v){ this._text = String(v); this.children = []; } });
Object.defineProperty(Node.prototype, 'innerHTML', {
    get: function(){ throw new Error('RENDERER-USED-innerHTML'); },
    set: function(v){ throw new Error('RENDERER-USED-innerHTML'); } });
Node.prototype.appendChild = function(c){
    if (this._text !== null) {
        var t = new Node('#text'); t._text = this._text; this._text = null;
        this.children.push(t); }
    this.children.push(c); return c; };
Node.prototype.addEventListener = function(ev, fn){
    (this.handlers[ev] = this.handlers[ev] || []).push(fn); };
Node.prototype.click = function(){ var self = this;
    (this.handlers['click'] || []).forEach(function(f){ f.call(self, {}); }); };
var byId = {};
['f','q','status','results'].forEach(function(id){ byId[id] = new Node('div'); });
var ctx = { console: console, encodeURIComponent: encodeURIComponent,
    fetch: function(){ throw new Error('the harness calls renderResults directly'); },
    document: {
        getElementById: function(id){ return byId[id] || new Node('div'); },
        createElement: function(t){ var n = new Node(t); made.push(n); return n; },
        createTextNode: function(t){ var n = new Node('#text'); n._text = String(t); return n; } } };
vm.createContext(ctx);
vm.runInContext(m[1], ctx);
ctx.renderResults(data);
if (mode === 'click') {
    var hs = made.filter(function(n){ return n.className === 'hash'; });
    hs.forEach(function(n){ n.click(); });
    if (process.argv[5] === 'twice') hs.forEach(function(n){ n.click(); });
}
/* COUNT THE CITATION NODES. S13 ("renders NO citation, not half of one")
** cannot be written as a regex over the rendered text: the page's own rank
** marker is '#1', so a pattern matching a bare '#<digit>' fires on every
** result whether a citation was built or not. The node count is the fact the
** assertion is actually about. */
process.stderr.write('CITE-NODES=' +
    made.filter(function(n){ return n.className === 'hash'; }).length + '\n');
process.stdout.write(byId['results'].textContent + '\n');
RENDER_JS
        render(){ node "$DIR/render.js" "$LOG/page.html" "$LOG/api.mid.json" "$@" \
                    >"$LOG/render.$1${2:+.$2}.out" 2>"$LOG/render.$1${2:+.$2}.err"; }
        render plain     || true
        render click     || true
        render click twice || true
        render nohash    || true
        render rehash    || true
        render reix      || true
        # The page must show the hash the SERVER sent, not one it invented,
        # so every expectation below is derived from api.mid.json and from
        # `viki ask`'s own output -- never typed in.
        FULL_HASH=$(sed -n 's/.*"hash":"\([0-9a-f]\{64\}\)".*/\1/p' "$LOG/api.mid.json" | head -1)
        # THE WIDTH IS READ FROM THE HEADER, not retyped. viki_ask.h says the
        # abbreviation width "lives once" so the page and this probe cannot
        # drift -- and the first cut of this probe then hardcoded `cut -c1-12`,
        # which is a second spelling and makes the claim false. Now changing
        # VIKI_HASH_ABBREV_STR moves both.
        ABBREV_N=$(sed -n 's/.*#define VIKI_HASH_ABBREV_STR "\([0-9][0-9]*\)".*/\1/p' \
                     "$REPO/src/viki_ask.h" | head -1)
        # NO SILENT FALLBACK. The first cut defaulted to 12 when the read
        # failed -- and the read DID fail, because this file calls the repo
        # root $REPO and not $ROOT. At the shipped width of 12 the default
        # masked it exactly; it surfaced only when the header was set to 8 and
        # the probe went red against a correct binary. A default that happens
        # to equal the real value is indistinguishable from a working read.
        [ -n "$ABBREV_N" ] || { echo "  FAIL  S-setup: cannot read VIKI_HASH_ABBREV_STR from $REPO/src/viki_ask.h"; FAIL=$((FAIL+1)); }
        ABBR=$(printf '%s' "$FULL_HASH" | cut -c1-"$ABBREV_N")
        FF_ABBR=$(printf '%*s' "$ABBREV_N" '' | tr ' ' 'f')
        CLI_CITE=$(head -1 "$LOG/mid.out" | awk '{print $3}')

        ck "S8 SETUP: the page's OWN script rendered the API's OWN JSON" \
           '[ -s "$LOG/render.plain.out" ] \
            && grep -q "rrf=" "$LOG/render.plain.out" \
            && grep -q "docs/long\.md" "$LOG/render.plain.out" \
            && [ -n "$FULL_HASH" ] && [ -n "$CLI_CITE" ]'
        ck "S9 the rendered page carries the content_hash, abbreviated, with #<ix>" \
           'grep -q -- "$ABBR#1" "$LOG/render.plain.out" \
            && grep -q "CITE-NODES=1" "$LOG/render.plain.err"'
        # S9 alone also passes if the page dumped all 64 characters, which
        # is the OTHER way to fail a human surface. This is what makes the
        # display choice a claim rather than an accident.
        ck "S10 CONTROL: ... abbreviated -- the full 64 hex is NOT on screen unasked" \
           '! grep -q -- "$FULL_HASH" "$LOG/render.plain.out"'
        # THE ONE THAT CLOSES THE DELIVERABLE: what the human can copy off
        # the page must be byte-identical to what `viki ask` prints for the
        # same query on the same corpus. Two surfaces, one citation.
        ck "S11 clicking the citation expands it to the CLI's EXACT citation" \
           '[ "$CLI_CITE" = "$FULL_HASH#1" ] \
            && grep -q -- "$CLI_CITE" "$LOG/render.click.out"'
        ck "S12 clicking again collapses it back to the abbreviation" \
           'grep -q -- "$ABBR#1" "$LOG/render.click.twice.out" \
            && ! grep -q -- "$FULL_HASH" "$LOG/render.click.twice.out"'
        # S13/S14 separate "the page renders the DATA" from "the page
        # prints a hex-shaped constant": remove the field and the citation
        # must vanish; change the field and the citation must follow it.
        # S13 IS NAMED "not half of one" AND USED TO CHECK ONLY THE OTHER HALF.
        # It asserted the abbreviated hash was absent and said nothing about
        # `#<ix>`, so a citeEl() that dropped the hash and still emitted
        # '#' + hit.chunk_ix -- exactly the half-citation the code comments
        # say must never appear, because it invites citing a chunk of nothing
        # -- passed it. The second clause is the assertion doing its job.
        ck "S13 CONTROL: a hit with no hash renders NO citation, not half of one" \
           '[ -s "$LOG/render.nohash.out" ] \
            && grep -q "docs/long\.md" "$LOG/render.nohash.out" \
            && ! grep -q -- "$ABBR" "$LOG/render.nohash.out" \
            && grep -q "CITE-NODES=0" "$LOG/render.nohash.err"'
        ck "S14 CONTROL: a different hash renders differently (data, not a constant)" \
           'grep -q -- "$FF_ABBR#1" "$LOG/render.rehash.out" \
            && ! grep -q -- "$ABBR" "$LOG/render.rehash.out"'
        # S14b: THE OTHER HALF OF THE CITATION. S14 varies only the hash, and
        # the fixture's chunk_ix is 1, so `'#' + hit.chunk_ix` and a literal
        # '#1' were indistinguishable to every assertion above -- half the
        # citation was graded and half was assumed.
        ck "S14b CONTROL: a different chunk_ix renders differently, too" \
           'grep -q -- "$ABBR#77" "$LOG/render.reix.out" \
            && ! grep -q -- "$ABBR#1" "$LOG/render.reix.out"'
        # Every assertion above runs through a DOM whose innerHTML throws,
        # so they collectively prove the runtime property S5 can only check
        # as a string. This is the control that says that detector works:
        # a page patched to assign innerHTML must FAIL the same harness.
        sed 's/\.textContent = /.innerHTML = /g' "$LOG/page.html" >"$LOG/page.bad.html"
        node "$DIR/render.js" "$LOG/page.bad.html" "$LOG/api.mid.json" plain \
            >"$LOG/render.bad.out" 2>"$LOG/render.bad.err" || true
        ck "S15 CONTROL: the runtime innerHTML detector is live (a patched page fails it)" \
           'grep -q "RENDERER-USED-innerHTML" "$LOG/render.bad.err" \
            && ! grep -q -- "$ABBR" "$LOG/render.bad.out"'
    else
        for t in "S8 SETUP: the page's OWN script rendered the API's OWN JSON" \
                 "S9 the rendered page carries the content_hash, abbreviated, with #<ix>" \
                 "S10 CONTROL: ... abbreviated -- the full 64 hex is NOT on screen unasked" \
                 "S11 clicking the citation expands it to the CLI's EXACT citation" \
                 "S12 clicking again collapses it back to the abbreviation" \
                 "S13 CONTROL: a hit with no hash renders NO citation, not half of one" \
                 "S14 CONTROL: a different hash renders differently (data, not a constant)" \
             "S14b CONTROL: a different chunk_ix renders differently, too" \
                 "S15 CONTROL: the runtime innerHTML detector is live (a patched page fails it)"
        do
            skip "$t" "no node on PATH -- S7 is the only citation check left, and it is a grep"
        done
    fi
else
    for t in "S0 SETUP: the JSON API answered both queries" \
             "S1 a middle chunk reports fragment_head AND fragment_tail true" \
             "S2 CONTROL: a single-chunk document reports BOTH false" \
             "S3 the JSON snippet is NOT decorated (booleans, not string mutation)" \
             "S4 chunk_count reports the real extent (the measured count, and 1)" \
             "S5 the HTML page still contains no innerHTML anywhere" \
             "S6 the page renders the SAME marker strings the CLI prints" \
             "S7 the page's renderer reads hit.hash at all" \
             "S8 SETUP: the page's OWN script rendered the API's OWN JSON" \
             "S9 the rendered page carries the content_hash, abbreviated, with #<ix>" \
             "S10 CONTROL: ... abbreviated -- the full 64 hex is NOT on screen unasked" \
             "S11 clicking the citation expands it to the CLI's EXACT citation" \
             "S12 clicking again collapses it back to the abbreviation" \
             "S13 CONTROL: a hit with no hash renders NO citation, not half of one" \
             "S14 CONTROL: a different hash renders differently (data, not a constant)" \
             "S14b CONTROL: a different chunk_ix renders differently, too" \
             "S15 CONTROL: the runtime innerHTML detector is live (a patched page fails it)"
    do
        skip "$t" "no curl on PATH"
    done
fi

echo "== I. viki grep marks the same facts, with the same strings =="
# `viki grep` prints substr(chunk_text,1,N) under the same citable
# `<hash>#<ix>` header `viki ask` uses -- a possibly-middle chunk, cut
# short -- so it is the same provenance defect and takes the same fix.
# It shares the marker LITERALS with `viki ask` (viki_grep.c includes
# viki_ask.h rather than retyping them), which is what R2/R6 are really
# checking: not "some marker printed" but "the same string".
#
# `viki grep` is exact and unranked, so unlike `ask` these need no --k and
# make no ranking assumption at all: each token below occurs in exactly
# one chunk of the corpus.
vgrep(){ _o=$1; shift
    ( cd "$DIR/bm25" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" grep "$@" ) \
        >"$LOG/$_o.out" 2>"$LOG/$_o.err" || true
}
# grep's header has no rrf= field: `[N] <hash>#<ix>  <source>`.
gr_ix(){ grep -E '^\[1\] ' "$1" | head -1 | awk '{print $2}' | cut -d'#' -f2; }

vgrep g.mid betamarker
ck "R1 SETUP: grep finds the middle chunk of long.md, exactly once" \
   '[ "$(grep -cE "^\[[0-9]+\] " "$LOG/g.mid.out")" -eq 1 ] \
    && [ "$(gr_ix "$LOG/g.mid.out")" = "1" ]'
ck "R2 grep marks that middle chunk at BOTH ends, with ask's own strings" \
   '[ "$(nmark "$LOG/g.mid.out" "$HEAD_MARK")" -eq 1 ] \
    && [ "$(nmark "$LOG/g.mid.out" "$TAIL_MARK")" -eq 1 ]'
ck "R3 grep's header line is untouched: [N] <64hex>#<ix>  <source>" \
   'head -1 "$LOG/g.mid.out" | grep -qE "^\[1\] [0-9a-f]{64}#1  \./docs/long\.md$"'

vgrep g.solo soloparagraph
ck "R4 SETUP: the single-chunk document is found (R5 is not vacuous)" \
   '[ -s "$LOG/g.solo.out" ] && [ "$(gr_ix "$LOG/g.solo.out")" = "0" ]'
ck "R5 CONTROL: a single-chunk document gets NEITHER fragment marker" \
   '[ "$(nmark "$LOG/g.solo.out" "$HEAD_MARK")" -eq 0 ] \
    && [ "$(nmark "$LOG/g.solo.out" "$TAIL_MARK")" -eq 0 ]'
# Truncation is a property of the EXCERPT, fragmentation of the CHUNK, and
# --chars moves one without moving the other: same hit, same chunk, two
# different answers about whether the excerpt was cut.
vgrep g.cut soloparagraph --chars 20
ck "R6 an excerpt cut short by --chars says so" \
   '[ "$(nmark "$LOG/g.cut.out" "$CUT_MARK")" -eq 1 ]'
ck "R7 CONTROL: ... on that same single-chunk hit, still no fragment marker" \
   '[ "$(nmark "$LOG/g.cut.out" "$HEAD_MARK")" -eq 0 ] \
    && [ "$(nmark "$LOG/g.cut.out" "$TAIL_MARK")" -eq 0 ]'
ck "R8 CONTROL: an excerpt that fits is NOT marked truncated" \
   '[ "$(nmark "$LOG/g.solo.out" "$CUT_MARK")" -eq 0 ]'

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" -eq 0 ]
