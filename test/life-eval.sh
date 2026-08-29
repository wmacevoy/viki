#!/bin/sh
# life-eval.sh -- retrieval quality against a REAL LIFE, not against this repo.
#
# WHY THIS EXISTS, and why it is not test/retrieval-eval.sh:
#
#   Every other measurement in this project asks viki about viki. The corpus is
#   this repo's own documentation, the questions are about chunking and epochs,
#   and a green number there says nothing about whether the tool helps anyone
#   live a day. Measured 2026-08-29 against a corpus of Warren's actual Gmail,
#   Drive and Calendar, viki answered fact lookups at rank 1 and put the answer
#   to "when is my next bait run" at rank 11 of 17 -- below a syllabus for a
#   course that has nothing to do with horses. No repo-corpus eval could have
#   found that, because no repo-corpus question has a WHEN in it.
#
# THE CORPUS IS NOT IN GIT AND MUST NOT BE. It is private correspondence. This
# script takes a directory and a query file; both live outside the repo (or in
# a gitignored path). What is versioned here is the MECHANISM and the CLASSES,
# so a later round can re-run the same measurement over whatever corpus the
# person running it actually has.
#
# Usage:
#   sh test/life-eval.sh <corpus-dir> <queries.tsv>
#
# queries.tsv is three TAB-separated columns, '#' comments allowed:
#   class <TAB> query <TAB> substring that must appear in the winning source
#
# Classes are free-form; the report groups by them. The split that mattered on
# the first run was fact / time / cross -- fact questions passed, time and
# cross-source questions did not, and grouping is what made that visible rather
# than averaging it away into one mediocre number.
#
# It PRINTS NUMBERS AND GATES NOTHING. Exit 0 means it produced them.

set -e
CORPUS=$1
QUERIES=$2
V="${VIKI_BIN:-$(cd "$(dirname "$0")/.." && pwd)/build/dist/viki}"
K="${K:-5}"
DEPTH="${DEPTH:-40}"      # how deep before calling it a miss

[ -d "$CORPUS" ]  || { echo "usage: sh test/life-eval.sh <corpus-dir> <queries.tsv>"; exit 2; }
[ -f "$QUERIES" ] || { echo "usage: sh test/life-eval.sh <corpus-dir> <queries.tsv>"; exit 2; }
[ -x "$V" ]       || { echo "no viki at $V"; exit 2; }

CORPUS=$(cd "$CORPUS" && pwd)
QUERIES=$(cd "$(dirname "$QUERIES")" && pwd)/$(basename "$QUERIES")

# THE QUERY FILE MUST NOT LIVE INSIDE THE CORPUS, and this guard is not
# fussiness -- it is the bug this harness found in its own first run.
#
# queries.tsv sat in the corpus directory, `viki index .` swept it up, and it
# then WON 11 of 22 queries, because a file listing the questions is the single
# most lexically similar document to every one of them. recall@5 read 0.864 and
# meant nothing. An eval that indexes its own answer key measures itself.
case "$QUERIES" in
  "$CORPUS"/*) echo "life-eval: REFUSING to run -- the query file is inside the corpus."
               echo "           $QUERIES"
               echo "           It would be indexed and would win its own queries."
               echo "           Move it outside $CORPUS and re-run."
               exit 2 ;;
esac

# THE CACHE IS BUILT FRESH EVERY RUN, in the corpus dir, because a stale cache
# is the single easiest way to measure a binary that is not the one you think.
# CLAUDE.md records a full green scored against a binary missing three fixes.
cd "$CORPUS"
"$V" index . >/dev/null 2>&1

printf 'life-eval  corpus=%s  k=%s\n' "$CORPUS" "$K"
printf '%s\n' "$("$V" sql "SELECT '           ' || count(*) || ' chunks over ' || count(DISTINCT content_hash) || ' documents'  FROM viki_chunk" 2>/dev/null)"
# WHICH LEGS ARE ACTUALLY RUNNING, SAID OUT LOUD.
#
# `viki ask` falls back to BM25-only with no model and an honest stderr notice,
# which is a required path (CLAUDE.md) -- but a harness that reports a number
# without saying which configuration produced it invites exactly the comparison
# that is not valid. Two numbers from this file are comparable only if this
# line matches. It happened on the first day: a run with VIKI_MODEL_DIR unset
# wrote a SECOND cache epoch ("none/c40o10") beside the real one and reported
# retrieval quality for a vector leg that was not running.
MODE=$("$V" ask "x" --k 1 2>&1 | sed -n 's/^viki ask: //p' | head -1)
case "$MODE" in
  *cosine*) printf '           %s\n\n' "$MODE" ;;
  *)        printf '           %s\n' "${MODE:-mode unknown}"
            printf '           *** DEGRADED: no vector leg. These numbers are BM25+literal\n'
            printf '           *** only and are NOT comparable with a hybrid run.\n'
            printf '           *** Set VIKI_MODEL_DIR to measure what ships.\n\n' ;;
esac

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

printf '%-6s %-46s %5s  %s\n' RANK QUERY CLASS WINNER
printf '%s\n' "----------------------------------------------------------------------------------"

grep -v '^[[:space:]]*#' "$QUERIES" | grep -v '^[[:space:]]*$' | while IFS='	' read -r class q want; do
  [ -n "$q" ] || continue
  # Rank of the FIRST hit whose source contains $want. Missing entirely == 0.
  rank=$("$V" ask "$q" --k "$DEPTH" 2>/dev/null \
         | grep '^\[' | grep -n -- "$want" | head -1 | cut -d: -f1)
  [ -n "$rank" ] || rank=0
  win=$("$V" ask "$q" --k 1 2>/dev/null | grep '^\[' | head -1 | awk '{print $NF}')
  if [ "$rank" = "1" ]; then mark="  1 "; elif [ "$rank" = "0" ]; then mark=" MISS"
  elif [ "$rank" -le "$K" ] 2>/dev/null; then mark="  $rank "; else mark=" $rank!"; fi
  printf '%-6s %-46.46s %5s  %s\n' "$mark" "$q" "$class" "${win##*/}"
  printf '%s\t%s\n' "$class" "$rank" >> "$TMP"
done

printf '\n%s\n' "----------------------------------------------------------------------------------"
awk -F'\t' -v k="$K" '
  { n[$1]++; tot++
    if ($2 == 1)            { r1[$1]++; R1++ }
    if ($2 >= 1 && $2 <= k) { rk[$1]++; RK++ }
    if ($2 == 0)            { miss[$1]++; MISS++ }
    if ($2 >= 1)            { mrr[$1] += 1/$2; MRR += 1/$2 }
  }
  END {
    printf "%-10s %5s %9s %9s %8s %7s\n", "CLASS", "n", "recall@1", "recall@" k, "MRR", "missing"
    for (c in n)
      printf "%-10s %5d %9.3f %9.3f %8.3f %7d\n", c, n[c], r1[c]/n[c], rk[c]/n[c], mrr[c]/n[c], miss[c]+0
    printf "%-10s %5d %9.3f %9.3f %8.3f %7d\n", "ALL", tot, R1/tot, RK/tot, MRR/tot, MISS+0
  }' "$TMP" | sort -k1,1

printf '\nA rank shown with "!" is outside k=%s -- present in the corpus, not in the answer.\n' "$K"
printf 'MISS means it was not in the top %s at all.\n' "$DEPTH"
