#!/bin/sh
#
# m1_e2e_probe.sh -- proof-of-recipe for the Milestone 1 end-to-end test.
#
# This is NOT the finished `make test`; it is the empirical skeleton behind
# the assertion list handed back to the orchestrator. Every check here was
# run and observed. It is deliberately self-contained (build.sh aesthetic):
# one scratch tree, no state outside $WORK, no reliance on anything being
# on PATH.
#
set -eu

VIKI=/Users/wmacevoy/projects/viki/build/dist/viki
MODEL=/Users/wmacevoy/projects/viki/build/dist/model
FOSSIL=/Users/wmacevoy/projects/viki/vendor/fossil-see/build/dist/fossil-see
WORK=${1:?usage: m1_e2e_probe.sh <empty-work-dir>}

# WHY an explicitly nonexistent path rather than `unset VIKI_MODEL_DIR`:
# viki.c's open_embedder_if_available() falls back to the RELATIVE path
# "build/dist/model" when the var is unset OR empty, so both of those
# silently re-enable rung 2 if the cwd happens to contain that path.
# Only a stat()-failing path is deterministic. See FINDINGS.md.
NOMODEL="$WORK/no-such-model-dir"

# WHY USER and not VIKI_FOSSIL_USER: viki_cache.c passes no --user to
# `fossil uv add`, and never consults viki_fossil_user(). Only $USER (or a
# repo default user) gets push past "cannot determine user". FINDINGS.md.
export USER=tester
export FOSSIL_SEE_KEY="m1-e2e-probe-key"
export VIKI_FOSSIL_BIN="$FOSSIL"

LOG="$WORK/logs"
mkdir -p "$LOG"

pass=0; fail=0
ok(){ pass=$((pass+1)); echo "  PASS  $1"; }
no(){ fail=$((fail+1)); echo "  FAIL  $1"; }
check(){ if eval "$2"; then ok "$1"; else no "$1"; fi; }

mkdir -p "$WORK"
cd "$WORK"

# ---------------------------------------------------------------- corpus --
# barn.md is the planted answer. The other two exist so ranking is a real
# discrimination, not a one-document tautology.
mkdir -p src/docs
cat > src/docs/barn.md <<'EOF'
# Barn notes

Six horses were grazing near the water trough behind the north barn.
The trough was refilled on Tuesday morning and the pump ran without
complaint. Bring the halters in from the fence before the rain.
EOF
cat > src/docs/grocery.md <<'EOF'
# Grocery list

milk, eggs, sourdough bread, two lemons, coffee beans, olive oil,
a bag of frozen peas, and dish soap.
EOF
cat > src/docs/tax.md <<'EOF'
# Tax note

The quarterly estimated tax filing deadline is in April. Keep the
receipts for the office deduction in the blue folder.
EOF

# ------------------------------------------------- encrypted hub + spoke --
echo "== fossil-see hub/spoke =="
"$FOSSIL" init --admin-user tester "$WORK/hub.efossil" >/dev/null
"$FOSSIL" clone "$WORK/hub.efossil" "$WORK/spoke.efossil" >/dev/null 2>&1
mkdir -p "$WORK/spoke"
( cd "$WORK/spoke" && "$FOSSIL" open "$WORK/spoke.efossil" >/dev/null )
cp -R src/docs "$WORK/spoke/docs"
( cd "$WORK/spoke" && "$FOSSIL" add docs >/dev/null \
  && "$FOSSIL" commit -m "plant corpus" --no-warnings >/dev/null )
printf 'The north pasture pump is serviced every spring by Ramona.\n' > "$WORK/wiki.txt"
( cd "$WORK/spoke" && "$FOSSIL" wiki create PumpMaintenance "$WORK/wiki.txt" >/dev/null )
( cd "$WORK/spoke" && "$FOSSIL" ticket add \
    title "Gearbox rattles on the north pump" \
    comment "Intermittent metallic knock under load." >/dev/null )

# WHY this must be the repo's own user, not the ambient one: fossil validates
# the name against the repo's user table, and `viki index`'s ticket leg
# swallows the failure as "0 ticket(s)" with exit 0. See FINDINGS.md.
export VIKI_FOSSIL_USER=tester

# A file deliberately NEVER committed. It is the witness for D-11: the fresh
# clone cannot possibly re-derive it from the repo, so finding it there
# proves the answer came from the pulled cache and nowhere else.
cat > "$WORK/spoke/docs/uncommitted-witness.md" <<'EOF'
# Witness document

The zeppelin mooring mast at Cardington was repainted in ochre.
EOF

# KICKOFF.md says "scratch ENCRYPTED repo", so prove it rather than trusting
# the .efossil name -- the glob is only an instruction to apply a key, not a
# property of the file, and a botched clone can leave plaintext under that
# name. Deliberately not `grep -q "SQLite format 3"`: this shell's grep skips
# binary files, so that check passes hardest when the repo is NOT encrypted.
# See FINDINGS.md.
check "P1 hub repo is really encrypted" \
      "[ \"\$(head -c 15 \"$WORK/hub.efossil\")\" != 'SQLite format 3' ]"
check "P2 encryption check can still fail (positive control)" \
      "[ \"\$(head -c 15 \"$WORK/spoke/.fslckout\")\" = 'SQLite format 3' ]"

# ------------------------------------------------------- A. degraded mode --
echo "== A. degraded (BM25-only) =="
cp -R "$WORK/src" "$WORK/degraded"
cd "$WORK/degraded"
VIKI_MODEL_DIR="$NOMODEL" "$VIKI" index . >$LOG/a.idx.out 2>$LOG/a.idx.err
check "A1 index stdout is empty"        "[ ! -s $LOG/a.idx.out ]"
check "A1 index announces no model"     "grep -q 'viki index: no embedding model found' $LOG/a.idx.err"
check "A2 index counted 3 files"        "grep -q '^viki index: 3 file(s) scanned, 3 (re)chunked' $LOG/a.idx.err"
check "A2 index model_id=none"          "grep -q '(model_id=none)\$' $LOG/a.idx.err"
VIKI_MODEL_DIR="$NOMODEL" "$VIKI" ask "horses water trough" >$LOG/a.k.out 2>$LOG/a.k.err
check "A3 degraded banner on stderr"    "grep -q 'viki ask: degraded mode (BM25 keyword search only' $LOG/a.k.err"
check "A3 no hybrid banner"             "! grep -q 'hybrid mode' $LOG/a.k.err"
check "A4 planted answer ranks 1"       "head -1 $LOG/a.k.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/barn\.md\$'"
VIKI_MODEL_DIR="$NOMODEL" "$VIKI" ask "equine hydration paddock" >$LOG/a.s.out 2>$LOG/a.s.err
check "A5 semantic query finds NOTHING" "[ ! -s $LOG/a.s.out ]"
check "A5 '(no matches)' on stderr"     "grep -q '^(no matches)\$' $LOG/a.s.err"

# --------------------------------------------------------- B. hybrid mode --
# WHY a fresh tree, not a re-index of $WORK/degraded: chunk_fts is not
# filtered by model_id and find_or_add() keys on (content_hash, chunk_ix),
# so a two-epoch cache adds the BM25 leg's RRF twice and shifts every
# score. See FINDINGS.md.
echo "== B. hybrid (BM25 + vector) =="
cp -R "$WORK/src" "$WORK/hybrid"
cd "$WORK/hybrid"
VIKI_MODEL_DIR="$MODEL" "$VIKI" index . >$LOG/b.idx.out 2>$LOG/b.idx.err
check "B1 index model_id is the pinned model" \
      "grep -q '(model_id=all-MiniLM-L6-v2-qint8-arm64)\$' $LOG/b.idx.err"
check "B2 embeddings are non-NULL, 384 floats" \
      "[ \"\$(sqlite3 .viki/cache.db 'SELECT count(*) FROM viki_chunk WHERE embedding IS NOT NULL AND length(embedding)=1536')\" = 3 ]"
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "horses water trough" >$LOG/b.k.out 2>$LOG/b.k.err
check "B3 hybrid banner names model_id" \
      "grep -q 'viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=all-MiniLM-L6-v2-qint8-arm64)' $LOG/b.k.err"
check "B4 planted answer ranks 1"       "head -1 $LOG/b.k.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/barn\.md\$'"
# THE vector-leg proof: identical query, opposite outcome from A5.
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "equine hydration paddock" >$LOG/b.s.out 2>$LOG/b.s.err
check "B5 VECTOR LEG: semantic-only query ranks barn.md 1" \
      "head -1 $LOG/b.s.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/barn\.md\$'"
check "B6 --k caps the result count"    "[ \"\$(VIKI_MODEL_DIR=$MODEL $VIKI ask 'equine hydration paddock' --k 2 2>/dev/null | grep -cE '^\[[0-9]+\] rrf=')\" = 2 ]"
# Incrementality: a second run must re-chunk nothing.
VIKI_MODEL_DIR="$MODEL" "$VIKI" index . >$LOG/b.idx2.out 2>$LOG/b.idx2.err
check "B7 re-index is a no-op"          "grep -q '^viki index: 3 file(s) scanned, 0 (re)chunked' $LOG/b.idx2.err"

# ---------------------------------------------------------- C. D-11 loop --
echo "== C. cache push / fresh clone / cache pull =="
cd "$WORK/spoke"
VIKI_MODEL_DIR="$MODEL" "$VIKI" index . >$LOG/c.idx.out 2>$LOG/c.idx.err
check "C1 spoke indexed the wiki page too" "grep -q '1 wiki page(s), 1 (re)chunked' $LOG/c.idx.err"
# NONZERO, not merely rc=0: an unresolvable fossil user makes this leg report
# "0 ticket(s)" and exit 0, indistinguishable from a repo with no tickets.
check "C1b spoke indexed the ticket (nonzero)" \
      "grep -q '1 ticket(s), 1 (re)chunked' $LOG/c.idx.err"
push_rc=0
"$VIKI" cache push >$LOG/c.push.out 2>$LOG/c.push.err || push_rc=$?
check "C2 cache push exits 0"           "[ $push_rc -eq 0 ]"
check "C3 hub now holds viki-cache.db" \
      "\"$FOSSIL\" uv list -R \"$WORK/hub.efossil\" | grep -q 'viki-cache.db'"
SPOKE_FP=$(sqlite3 .viki/cache.db \
  "SELECT group_concat(substr(content_hash,1,12)||':'||hex(substr(embedding,1,8)),'|') FROM (SELECT * FROM viki_chunk ORDER BY content_hash)")

"$FOSSIL" clone "$WORK/hub.efossil" "$WORK/fresh.efossil" >/dev/null 2>&1
mkdir -p "$WORK/fresh"
cd "$WORK/fresh"
"$FOSSIL" open "$WORK/fresh.efossil" >/dev/null
check "C4 fresh clone has NO cache before pull" "[ ! -e .viki/cache.db ]"
check "C5 witness doc is absent from the clone"  "[ ! -e docs/uncommitted-witness.md ]"
pull_rc=0
"$VIKI" cache pull >$LOG/c.pull.out 2>$LOG/c.pull.err || pull_rc=$?
check "C6a cache pull exits 0"          "[ $pull_rc -eq 0 ]"
check "C6 cache pull created the db"    "[ -s .viki/cache.db ]"
FRESH_FP=$(sqlite3 .viki/cache.db \
  "SELECT group_concat(substr(content_hash,1,12)||':'||hex(substr(embedding,1,8)),'|') FROM (SELECT * FROM viki_chunk ORDER BY content_hash)")
check "C7 embeddings round-tripped byte-identically" "[ \"\$SPOKE_FP\" = \"\$FRESH_FP\" ]"
# `viki index` is never invoked in this directory. Anything found here was
# computed on the spoke.
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "equine hydration paddock" >$LOG/c.s.out 2>$LOG/c.s.err
check "C8 planted answer without re-indexing" \
      "head -1 $LOG/c.s.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/barn\.md\$'"
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "airship anchor tower ochre paint" >$LOG/c.w.out 2>$LOG/c.w.err
check "C9 COMPUTE-ONCE: witness doc retrievable though absent on disk" \
      "head -1 $LOG/c.w.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/uncommitted-witness\.md\$'"
VIKI_MODEL_DIR="$NOMODEL" "$VIKI" ask "horses water trough" >$LOG/c.d.out 2>$LOG/c.d.err
check "C10 pulled cache also serves degraded mode" \
      "head -1 $LOG/c.d.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  \./docs/barn\.md\$'"
# Non-file artifact types survive the uv round trip with their virtual paths.
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "metallic knocking noise from the machinery" >$LOG/c.t.out 2>$LOG/c.t.err
check "C11 ticket retrievable from the pulled cache" \
      "head -1 $LOG/c.t.out | grep -qE '^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  ticket:[0-9a-f]{40}\$'"
VIKI_MODEL_DIR="$MODEL" "$VIKI" ask "who services the irrigation pump each spring" >$LOG/c.wk.out 2>$LOG/c.wk.err
check "C12 wiki page retrievable from the pulled cache" \
      "grep -qE '^\[[0-9]+\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  wiki:PumpMaintenance\$' $LOG/c.wk.out"

# ---------------------------------------------------- D. selftests / CLI --
echo "== D. selftests and CLI contract =="
cd "$WORK"
check "D1 ndvss statically linked"      "\"$VIKI\" ndvss-selftest | grep -q '^ndvss instruction set: '"
check "D2 embed-selftest PASS"          "\"$VIKI\" embed-selftest \"$MODEL\" | grep -q '^embed-selftest: PASS\$'"
check "D3 version on stdout"            "[ \"\$(\"$VIKI\" version)\" = 'viki 0.1.0-m1' ]"
check "D4 unknown subcommand exits 1"   "! \"$VIKI\" bogus >/dev/null 2>&1"
check "D5 ask with no query exits 1"    "! \"$VIKI\" ask >/dev/null 2>&1"

echo
echo "PASS=$pass FAIL=$fail"
[ "$fail" -eq 0 ]
