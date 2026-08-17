#!/bin/sh
#
# model-uv-e2e-probe.sh -- proof that KICKOFF deliverable 3 is WHOLE:
# `viki cache push`/`pull` move the embedding cache AND THE PINNED MODEL
# as fossil unversioned files (D-12), so a fresh clone gets hybrid
# retrieval from ONE endpoint with no third-party download.
#
# Same aesthetic as m1-e2e-probe.sh: one scratch tree, nothing outside
# $WORK, nothing assumed on PATH. Every check here was run and observed.
#
# usage: model-uv-e2e-probe.sh <empty-work-dir>
#   VIKI=<path>  overrides the binary under test (default build/dist/viki)
#
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
VIKI=${VIKI:-$REPO/build/dist/viki}
MODEL=${MODEL:-$REPO/build/dist/model}
FOSSIL=${FOSSIL:-$REPO/vendor/fossil-see/build/dist/fossil-see}
WORK=${1:?usage: model-uv-e2e-probe.sh <empty-work-dir>}

# This probe cd's into $WORK, so RELATIVE overrides silently resolve
# against the wrong directory once it does -- the run then dies partway
# through with a message that reads like a viki bug. Fail loudly here.
for _p in "$VIKI" "$MODEL" "$FOSSIL"; do
    case "$_p" in /*) ;; *) echo "ERR: VIKI/MODEL/FOSSIL must be ABSOLUTE paths (got '$_p')" >&2; exit 2 ;; esac
done
[ -x "$VIKI" ]   || { echo "ERR: no executable viki at '$VIKI'" >&2; exit 2; }
[ -x "$FOSSIL" ] || { echo "ERR: no executable fossil at '$FOSSIL'" >&2; exit 2; }

# WHY USER and not VIKI_FOSSIL_USER: viki_cache.c passes no --user to
# `fossil uv add`, and never consults viki_fossil_user(). Only $USER (or a
# repo default user) gets past "cannot determine user". FINDINGS.md.
export USER=tester
export VIKI_FOSSIL_USER=tester
export FOSSIL_SEE_KEY="model-uv-probe-key"
export VIKI_FOSSIL_BIN="$FOSSIL"
# Leave no trace in the developer's real ~/.fossil.
export FOSSIL_HOME="$WORK/fossilhome"

mkdir -p "$WORK" "$FOSSIL_HOME"
LOG="$WORK/logs"; mkdir -p "$LOG"
cd "$WORK"

pass=0; fail=0
ok(){ pass=$((pass+1)); echo "  PASS  $1"; }
no(){ fail=$((fail+1)); echo "  FAIL  $1"; }
check(){ if eval "$2"; then ok "$1"; else no "$1"; fi; }

# hub_spoke <name> -- encrypted hub + one opened clone of it.
hub_spoke(){
    "$FOSSIL" init --admin-user tester "$WORK/$1-hub.efossil" >/dev/null 2>&1
    "$FOSSIL" clone --no-open "$WORK/$1-hub.efossil" "$WORK/$1-spoke.efossil" >/dev/null 2>&1
    mkdir -p "$WORK/$1-spoke"
    ( cd "$WORK/$1-spoke" && "$FOSSIL" open "$WORK/$1-spoke.efossil" >/dev/null 2>&1 )
    # A clone that silently produced PLAINTEXT under an .efossil name would
    # make every "encrypted hub" claim below a lie. FINDINGS.md.
    [ "$(head -c 15 "$WORK/$1-spoke.efossil")" != 'SQLite format 3' ] \
        || { echo "FATAL: $1-spoke.efossil is plaintext"; exit 1; }
}
# fresh_clone <hubname> <dir> -- a brand new clone, never indexed.
fresh_clone(){
    "$FOSSIL" clone --no-open "$WORK/$1-hub.efossil" "$WORK/$2.efossil" >/dev/null 2>&1
    mkdir -p "$WORK/$2"
    ( cd "$WORK/$2" && "$FOSSIL" open "$WORK/$2.efossil" >/dev/null 2>&1 )
}

# ------------------------------------------------------------- corpus --
mkdir -p corpus
cat > corpus/barn.md <<'EOF'
# Barn notes

Six horses were grazing near the water trough behind the north barn.
The trough was refilled on Tuesday morning and the pump ran without
complaint. Bring the halters in from the fence before the rain.
EOF
cat > corpus/grocery.md <<'EOF'
# Grocery list

milk, eggs, sourdough bread, two lemons, coffee beans, olive oil,
one bag of frozen peas, and dish soap.
EOF
cat > corpus/tax.md <<'EOF'
# Tax note

The quarterly estimated tax filing deadline is in April. Keep the
receipts for the office deduction in the blue folder.
EOF

echo "== P. encrypted hub + spoke, corpus indexed WITH the model =="
hub_spoke main
cp corpus/*.md "$WORK/main-spoke/"
( cd "$WORK/main-spoke" && "$FOSSIL" add . >/dev/null 2>&1 \
  && "$FOSSIL" commit -m "plant corpus" --no-warnings >/dev/null 2>&1 )
check "P1 hub repo is really encrypted" \
      "[ \"\$(head -c 15 \"$WORK/main-hub.efossil\")\" != 'SQLite format 3' ]"
check "P2 encryption check can still fail (positive control)" \
      "[ \"\$(head -c 15 \"$WORK/main-spoke/.fslckout\")\" = 'SQLite format 3' ]"
( cd "$WORK/main-spoke" && VIKI_MODEL_DIR="$MODEL" "$VIKI" index . \
    >"$LOG/p.idx.out" 2>"$LOG/p.idx.err" )
check "P3 spoke indexed with the pinned model" \
      "grep -q '(model_id=all-MiniLM-L6-v2-qint8-arm64)\$' $LOG/p.idx.err"

echo "== M. push publishes the MODEL, not just the cache =="
push_rc=0
( cd "$WORK/main-spoke" && VIKI_MODEL_DIR="$MODEL" "$VIKI" cache push \
    >"$LOG/m.push.out" 2>"$LOG/m.push.err" ) || push_rc=$?
check "M1 cache push exit 0" "[ $push_rc -eq 0 ]"
# `fossil uv list` always prints details (hash date size storedsize name),
# so every assertion below matches on the LAST field, not a whole line.
"$FOSSIL" uv list -R "$WORK/main-hub.efossil" </dev/null >"$LOG/m.uvlist" 2>&1
check "M2 hub holds the cache blob" \
      "awk '\$NF==\"viki-cache.db\"{f=1}END{exit !f}' $LOG/m.uvlist"
for f in model.onnx vocab.txt viki-manifest.json; do
    check "M3 hub holds viki-model/$f" \
      "awk '\$NF==\"viki-model/$f\"{f=1}END{exit !f}' $LOG/m.uvlist"
done
# Sizes, from the HUB's own uv catalog, must equal the on-disk originals:
# proves whole blobs crossed the wire, not truncated placeholders.
for f in model.onnx vocab.txt viki-manifest.json; do
    want=$(wc -c < "$MODEL/$f" | tr -d ' ')
    check "M4 hub-side size of viki-model/$f is $want bytes" \
          "awk '\$NF==\"viki-model/$f\" && \$(NF-2)==$want{f=1}END{exit !f}' $LOG/m.uvlist"
done
check "M5 push output states the size being published" \
      "grep -qE 'publishing model epoch .all-MiniLM-L6-v2-qint8-arm64. from .* \\([0-9]+\\.[0-9] MB total\\)' $LOG/m.push.err"
check "M6 push output states latest-wins/no-history (D-12)" \
      "grep -q 'LATEST-WINS with no history (D-12)' $LOG/m.push.err"

echo "== N. re-pushing an unchanged epoch is skipped =="
before=$("$FOSSIL" uv list -l -R "$WORK/main-hub.efossil" </dev/null | grep 'viki-model/model.onnx$')
push2_rc=0
( cd "$WORK/main-spoke" && VIKI_MODEL_DIR="$MODEL" "$VIKI" cache push \
    >"$LOG/n.push.out" 2>"$LOG/n.push.err" ) || push2_rc=$?
after=$("$FOSSIL" uv list -l -R "$WORK/main-hub.efossil" </dev/null | grep 'viki-model/model.onnx$')
check "N1 second push exit 0" "[ $push2_rc -eq 0 ]"
check "N2 second push says the epoch is already published" \
      "grep -q \"is already published under viki-model/\" $LOG/n.push.err"
check "N3 second push does NOT re-publish" \
      "! grep -q 'publishing model epoch' $LOG/n.push.err"
check "N4 hub-side model.onnx row (hash+mtime) is untouched" \
      "[ \"\$before\" = \"\$after\" ]"

echo "== F. FRESH CLONE: pull the model, then ask in hybrid mode =="
fresh_clone main fresh
PULLED="$WORK/fresh/pulled-model"
mkdir -p "$WORK/empty-model-dir"
check "F1 fresh clone has no model directory yet" "[ ! -e \"$PULLED\" ]"
check "F2 fresh clone has no cache db yet"        "[ ! -e \"$WORK/fresh/.viki/cache.db\" ]"
# CONTROL: the semantic-only query before the pull. Nothing is here yet, so
# this must find nothing -- otherwise F7's success would prove nothing.
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$PULLED" "$VIKI" ask "equine hydration paddock" \
    >"$LOG/f.pre.out" 2>"$LOG/f.pre.err" ) || true
check "F3 CONTROL: before the pull the fresh clone answers nothing" "[ ! -s $LOG/f.pre.out ]"
rm -rf "$WORK/fresh/.viki"

pull_rc=0
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$PULLED" "$VIKI" cache pull \
    >"$LOG/f.pull.out" 2>"$LOG/f.pull.err" ) || pull_rc=$?
check "F4 cache pull exit 0" "[ $pull_rc -eq 0 ]"
check "F5 pulled model.onnx is byte-identical to the published one" \
      "cmp -s \"$MODEL/model.onnx\" \"$PULLED/model.onnx\""
check "F6 pulled vocab.txt is byte-identical"   "cmp -s \"$MODEL/vocab.txt\" \"$PULLED/vocab.txt\""
check "F7 pulled manifest is byte-identical"    "cmp -s \"$MODEL/viki-manifest.json\" \"$PULLED/viki-manifest.json\""
check "F8 pull verified both sha256s against the epoch pin" \
      "grep -q 'model.onnx sha256 verified against the epoch pin' $LOG/f.pull.err \
       && grep -q 'vocab.txt sha256 verified against the epoch pin' $LOG/f.pull.err"
check "F9 pulled cache db materialized"         "[ -s \"$WORK/fresh/.viki/cache.db\" ]"

# THE CLAIM. `viki index` is never run in $WORK/fresh and no model was ever
# copied there by this script -- both came from the hub over file:// sync.
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$PULLED" "$VIKI" ask "equine hydration paddock" \
    >"$LOG/f.ask.out" 2>"$LOG/f.ask.err" ) || true
check "F10 SELF-CONTAINED: fresh clone runs HYBRID on the pulled model" \
      "grep -q 'viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=all-MiniLM-L6-v2-qint8-arm64)' $LOG/f.ask.err"
# The hit line is `[N] rrf=X.XXXX  <content_hash>#<ix>  <source>`; the
# content_hash field was added to it in the same round as this probe, so
# the source path is the LAST field and no longer carries the '#<ix>'.
check "F11 the vector leg really answers (semantic query, no shared keywords)" \
      "head -1 $LOG/f.ask.out | grep -qE '^\\[1\\] rrf=[0-9]+\\.[0-9]{4}  [0-9a-f]{64}#0  \\./barn\\.md\$'"
# CONTROL for F10/F11: same clone, same cache, same query -- only the model
# directory changes. Proves the PULLED DIRECTORY is what supplies rung 2.
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$WORK/empty-model-dir" "$VIKI" ask "equine hydration paddock" \
    >"$LOG/f.ctl.out" 2>"$LOG/f.ctl.err" ) || true
check "F12 CONTROL: point VIKI_MODEL_DIR elsewhere and the same query degrades" \
      "grep -q 'viki ask: degraded mode' $LOG/f.ctl.err && [ ! -s $LOG/f.ctl.out ]"

echo "== G. second pull is a no-op (epoch already present) =="
g_rc=0
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$PULLED" "$VIKI" cache pull \
    >"$LOG/g.pull.out" 2>"$LOG/g.pull.err" ) || g_rc=$?
check "G1 second pull exit 0" "[ $g_rc -eq 0 ]"
check "G2 second pull refetches nothing" \
      "grep -q \"already present at\" $LOG/g.pull.err && ! grep -q 'uv export viki-model/model.onnx' $LOG/g.pull.err"
# ... but a model directory gutted underneath its manifest must refetch.
rm -f "$PULLED/model.onnx"
g2_rc=0
( cd "$WORK/fresh" && VIKI_MODEL_DIR="$PULLED" "$VIKI" cache pull \
    >"$LOG/g2.pull.out" 2>"$LOG/g2.pull.err" ) || g2_rc=$?
check "G3 a manifest whose blobs vanished triggers a refetch" \
      "[ $g2_rc -eq 0 ] && grep -q 'its files are missing -- refetching' $LOG/g2.pull.err \
       && cmp -s \"$MODEL/model.onnx\" \"$PULLED/model.onnx\""

echo "== H. no model present: push degrades, does NOT fail =="
hub_spoke nomodel
echo 'The zeppelin mooring mast at Cardington was repainted in ochre.' > "$WORK/nomodel-spoke/note.md"
( cd "$WORK/nomodel-spoke" && "$FOSSIL" add . >/dev/null 2>&1 \
  && "$FOSSIL" commit -m note --no-warnings >/dev/null 2>&1 )
( cd "$WORK/nomodel-spoke" && VIKI_MODEL_DIR="$WORK/no-such-dir" "$VIKI" index . \
    >/dev/null 2>&1 )
h_rc=0
( cd "$WORK/nomodel-spoke" && VIKI_MODEL_DIR="$WORK/no-such-dir" "$VIKI" cache push \
    >"$LOG/h.push.out" 2>"$LOG/h.push.err" ) || h_rc=$?
check "H1 model-less push still exits 0" "[ $h_rc -eq 0 ]"
check "H2 model-less push says so, loudly and non-fatally" \
      "grep -q 'no model to publish' $LOG/h.push.err && grep -q 'NOT AN ERROR' $LOG/h.push.err"
"$FOSSIL" uv list -R "$WORK/nomodel-hub.efossil" </dev/null >"$LOG/h.uvlist" 2>&1
check "H3 model-less push still published the cache" \
      "awk '\$NF==\"viki-cache.db\"{f=1}END{exit !f}' $LOG/h.uvlist"
check "H4 model-less push published NO model blobs" \
      "! grep -q 'viki-model/' $LOG/h.uvlist"

echo "== I. pull from a model-less hub: clear, non-fatal =="
fresh_clone nomodel nmfresh
i_rc=0
( cd "$WORK/nmfresh" && VIKI_MODEL_DIR="$WORK/nmfresh/pulled" "$VIKI" cache pull \
    >"$LOG/i.pull.out" 2>"$LOG/i.pull.err" ) || i_rc=$?
check "I1 pull from a model-less hub exits 0" "[ $i_rc -eq 0 ]"
check "I2 pull explains the BM25-only consequence" \
      "grep -q 'no model published on this hub' $LOG/i.pull.err && grep -q 'Not an error' $LOG/i.pull.err"
check "I3 the cache still arrived"  "[ -s \"$WORK/nmfresh/.viki/cache.db\" ]"
check "I4 no half-made model directory left behind" "[ ! -e \"$WORK/nmfresh/pulled\" ]"

echo "== J. NEGATIVE CONTROL: a model that fails its own checksum pin =="
hub_spoke badsum
mkdir -p "$WORK/bad-model"
printf 'not a real onnx graph\n' > "$WORK/bad-model/model.onnx"
printf '[PAD]\n[UNK]\n' > "$WORK/bad-model/vocab.txt"
# model_sha256 deliberately does NOT match model.onnx above; vocab_sha256 does.
vsha=$(shasum -a 256 "$WORK/bad-model/vocab.txt" | awk '{print $1}')
cat > "$WORK/bad-model/viki-manifest.json" <<EOF
{
  "model_id": "deliberately-corrupt-epoch",
  "dim": 384,
  "model_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
  "vocab_sha256": "$vsha"
}
EOF
echo hi > "$WORK/badsum-spoke/note.md"
( cd "$WORK/badsum-spoke" && "$FOSSIL" add . >/dev/null 2>&1 \
  && "$FOSSIL" commit -m note --no-warnings >/dev/null 2>&1 )
( cd "$WORK/badsum-spoke" && VIKI_MODEL_DIR="$WORK/bad-model" "$VIKI" index . >/dev/null 2>&1 )
( cd "$WORK/badsum-spoke" && VIKI_MODEL_DIR="$WORK/bad-model" "$VIKI" cache push \
    >"$LOG/j.push.out" 2>"$LOG/j.push.err" )
fresh_clone badsum bsfresh
j_rc=0
( cd "$WORK/bsfresh" && VIKI_MODEL_DIR="$WORK/bsfresh/pulled" "$VIKI" cache pull \
    >"$LOG/j.pull.out" 2>"$LOG/j.pull.err" ) || j_rc=$?
check "J1 a checksum-mismatched model makes pull FAIL loudly" \
      "[ $j_rc -ne 0 ] && grep -q 'CHECKSUM MISMATCH' $LOG/j.pull.err"
check "J2 the epoch pin is NOT installed, so viki degrades instead of loading it" \
      "[ ! -e \"$WORK/bsfresh/pulled/viki-manifest.json\" ]"
check "J3 the good file was still verified before the bad one was caught" \
      "grep -q 'vocab.txt sha256 verified' $LOG/j.pull.err"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
