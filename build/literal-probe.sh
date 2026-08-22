#!/bin/sh
# literal-probe.sh -- the LITERAL leg of `viki ask` (QUEUE 42).
#
# The property under test is that `ask` now SUBSUMES a literal lookup: a token
# that occurs in exactly one chunk reaches the top of `ask` even when another
# document dominates BOTH ranked legs. That is what removes the "should I ask
# or grep?" decision for literal strings.
#
# It has to be tested UNDER CONTEST. An earlier draft of this probe used a
# five-document corpus and scored 7/7 against a binary with NO literal leg at
# all -- fully vacuous, because the candidate pool (VIKI_CANDIDATE_POOL=40) was
# larger than the corpus, so nothing was ever excluded and the leg had nothing
# to demonstrate. The corpus below therefore contains a LONG document on the
# SAME TOPIC as the query, which wins bm25() on term frequency and the vector
# leg on topical concentration, and buries the one chunk that actually names
# the identifier. Measured: a binary without the leg fills all five slots with
# that document and scores 3 passed, 2 failed here.
#
# Usage: sh build/literal-probe.sh <empty-dir>
set -e
DIR="$1"
[ -n "$DIR" ] || { echo "usage: $0 <empty-dir>"; exit 2; }
mkdir -p "$DIR"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
[ -x "$V" ] || { echo "no viki binary at $V"; exit 2; }
export VIKI_MODEL_DIR="${VIKI_MODEL_DIR:-$ROOT/build/dist/model}"
if [ ! -d "$VIKI_MODEL_DIR" ]; then
  echo "literal-probe: no model at $VIKI_MODEL_DIR"
  echo "REFUSING to run: without the vector leg the buried document is not"
  echo "actually contested, and every assertion below would pass vacuously."
  exit 2
fi
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
chk(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (got '$2' want '$3')"; fi; }

cd "$DIR"; mkdir -p corpus; cd corpus

# THE CONTESTED DOCUMENT: same subject as the query, long enough to occupy
# every candidate slot, and it never uses the identifier.
awk 'BEGIN{
  srand(5);
  n=split("The counted framing is parsed one record at a time by the reader.|" \
          "Parsing the framing avoids splitting on delimiters that can be empty.|" \
          "Each record in the framing carries an explicit length prefix first.|" \
          "The framing parser must never collapse an empty field silently.|" \
          "Reading the counted framing is how every extractor returns its rows.|" \
          "Framing parse errors are distinguishable from an empty result set.", a, "|");
  print "# Counted framing"; print "";
  for(i=0;i<600;i++) print a[int(rand()*n)+1];
}' > dense.md

# THE BURIED CHUNK: one mention of one identifier, inside a document about
# something adjacent. Nothing about it is dense or repeated.
cat > passing.md <<'EOF'
# Extraction strategy

Each artifact class is read with one subprocess rather than one per artifact.
The framing itself is parsed by framed_next and a sentinel row is what proves
the extractor actually ran, rather than the exit status.
EOF

# An ALL-LOWERCASE rare token: no capital or digit announces it. A first draft
# of this leg gated on "hard" tokens and silently dropped exactly this case.
cat > lowercase.md <<'EOF'
# Timeline rendering

The comment shown in the timeline uses ecomment when present. Amending a
check-in rewrites ecomment without touching the check-in's own time.
EOF

$V index . >/dev/null 2>&1

echo "== ask subsumes a literal lookup, under contest =="

N=$($V grep "framed_next" 2>/dev/null | grep -cE '^\[[0-9]+\] [a-f0-9]{64}#' || true)
chk "L1 SETUP: the identifier occurs in exactly ONE chunk (L2 is not vacuous)" "$N" "1"

TOP5=$($V ask "how does framed_next parse the counted framing" --k 5 2>/dev/null \
       | grep -E '^\[[0-9]+\] rrf=' | grep -c 'passing.md' || true)
chk "L2 the chunk naming the identifier reaches the top 5" "$TOP5" "1"

DOM=$($V ask "how does framed_next parse the counted framing" --k 5 2>/dev/null \
      | grep -E '^\[[0-9]+\] rrf=' | head -1 | grep -c 'dense.md' || true)
chk "L3 CONTROL: the dominant document still holds rank 1 (raises, not inverts)" "$DOM" "1"

echo "== an all-lowercase rare token anchors too =="

LC=$($V ask "how is ecomment used when rendering the timeline" --k 5 2>/dev/null \
     | grep -E '^\[[0-9]+\] rrf=' | grep -c 'lowercase.md' || true)
chk "L4 a rare ALL-LOWERCASE token reaches the top 5" "$LC" "1"

echo "== the leg is an input to ranking, never a filter =="

GN=$($V grep "zzzqqxnotpresentanywhere" 2>/dev/null \
     | grep -cE '^\[[0-9]+\] [a-f0-9]{64}#' || true)
chk "L5 CONTROL: grep finds a nowhere-token zero times" "$GN" "0"

ANY=$($V ask "zzzqqxnotpresentanywhere" --k 5 2>/dev/null \
      | grep -cE '^\[[0-9]+\] rrf=' || true)
if [ "$ANY" -gt 0 ]; then ok "L6 CONTROL: ask still answers a nowhere-token (leg is not a filter)"
else no "L6 ask returned nothing -- the literal leg has become a filter"; fi

A=$($V ask "the and for that with this from" --k 3 2>/dev/null | grep -E '^\[[0-9]+\] rrf=' | awk '{print $3}')
B=$($V ask "the and for that with this from" --k 3 2>/dev/null | grep -E '^\[[0-9]+\] rrf=' | awk '{print $3}')
chk "L7 CONTROL: an all-stopword query is deterministic (every term dropped)" "$A" "$B"

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
