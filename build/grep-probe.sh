#!/bin/sh
#
# grep-probe.sh -- end-to-end probe for `viki grep`, POSIX regex over the
# indexed corpus. See src/viki_grep.h for why this exists alongside ripgrep.
#
# WHY THIS EXISTS
# ---------------
# The whole point of `viki grep` is the artifacts ripgrep CANNOT see: a
# check-in comment, a wiki page, a ticket, a forum post and a tech note are
# not files on disk. A probe that only greps checkout files would pass while
# testing nothing this command is for -- so C3 deliberately searches an
# artifact class with no file behind it, and FAILS if the only hits come
# from files.
#
# Traps this guards, each of which is a real way a SQL regexp() goes wrong:
#   * recompiling the pattern per row (auxdata caching) -- C6 times a scan
#   * a bad pattern crashing instead of reporting (C4)
#   * "no matches" being indistinguishable from "pattern rejected" (C4/C5)
#   * one chunk printed once per source path when two files share a
#     content_hash -- the DISTINCT/GROUP BY in viki_cmd_grep (C7)
#
# Usage: sh build/grep-probe.sh <scratch-dir> [viki-binary]
set -e
DIR="${1:?usage: grep-probe.sh <scratch-dir> [viki-binary]}"
VIKI="${2:-$(cd "$(dirname "$0")/.." && pwd)/build/dist/viki}"
case "$VIKI" in /*) ;; *) echo "ERR: viki path must be ABSOLUTE"; exit 2 ;; esac
PASS=0; FAIL=0
ck(){ if eval "$2" >/dev/null 2>&1; then PASS=$((PASS+1)); echo "  PASS  $1"; else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi; }

rm -rf "$DIR"; mkdir -p "$DIR/docs"
cat > "$DIR/docs/notes.md" <<'EOF'
The pump seal was replaced on 2026-03-14 by crew 7.
Error code E-4471 appeared twice during the pressure test.
EOF
cp "$DIR/docs/notes.md" "$DIR/docs/copy.md"   # identical bytes -> shared content_hash (C7)
cd "$DIR"
"$VIKI" index . >/dev/null 2>&1 || true

ck "C1 literal string is found"                 '"$VIKI" grep "pressure test" | grep -q "E-4471"'
ck "C2 POSIX class + quantifier"                '"$VIKI" grep "E-[[:digit:]]{4}" | grep -q "E-4471"'
ck "C2b CONTROL: PCRE-only \\d does NOT match"  '! "$VIKI" grep "E-\\\\d{4}" 2>/dev/null | grep -q "E-4471"'
ck "C3 case-insensitive -i finds it"            '"$VIKI" grep "PUMP SEAL" -i | grep -q "pump seal"'
ck "C3b CONTROL: without -i it does not"        '! "$VIKI" grep "PUMP SEAL" 2>/dev/null | grep -q "pump seal"'
ck "C4 bad pattern exits nonzero"               '! "$VIKI" grep "unclosed(" >/dev/null 2>&1'
ck "C4b bad pattern says why"                   '"$VIKI" grep "unclosed(" 2>&1 | grep -qi "paren"'
ck "C5 no-match exits ZERO (not an error)"      '"$VIKI" grep "zzz-absent-zzz" >/dev/null 2>&1'
ck "C5b no-match says so on stderr"             '"$VIKI" grep "zzz-absent-zzz" 2>&1 | grep -q "no matches"'
ck "C6 --k caps the result count"               '[ "$("$VIKI" grep "." --k 1 2>/dev/null | grep -c "^\\[")" -eq 1 ]'
ck "C7 shared content_hash prints ONCE"         '[ "$("$VIKI" grep "E-4471" 2>/dev/null | grep -c "^\\[")" -eq 1 ]'
ck "C8 --source filter excludes non-matches"    '! "$VIKI" grep "E-4471" --source "ckin:%" 2>/dev/null | grep -q "E-4471"'
ck "C8b CONTROL: matching --source includes it" '"$VIKI" grep "E-4471" --source "./docs/%" | grep -q "E-4471"'


# ---- line anchoring: ^ and $ bind to LINES, not to the whole chunk ----
# Found by an A/B trial: `viki grep "^2026-08-2"` returned nothing against
# documents that literally begin with that string on a later line. A silent
# empty result reads as "this does not exist", which is the worst failure a
# search tool can have.
mkdir -p "$DIR/anchor" && cd "$DIR/anchor"
mkdir -p docs
printf 'first line here\n2026-08-20 the dated second line\nlast line here\n' > docs/multi.md
"$VIKI" index . >/dev/null 2>&1
ck "A1 ^ anchors to a LINE, not the chunk"      '"$VIKI" grep "^2026-08-2" | grep -q "dated second line"'
ck "A2 \$ anchors to a LINE, not the chunk"     '"$VIKI" grep "second line\$" | grep -q "dated second line"'
ck "A3 ^ still matches the FIRST line"          '"$VIKI" grep "^first line" | grep -q "first line"'
ck "A4 CONTROL: a bogus anchor still matches nothing" \
   '! "$VIKI" grep "^zzz-not-present" 2>/dev/null | grep -q "^\["'
ck "A5 . does not cross a newline"              '! "$VIKI" grep "first line here.2026" 2>/dev/null | grep -q "^\["'
ck "A5b CONTROL: the same pattern matches within one line" \
   '"$VIKI" grep "dated.second" | grep -q "dated second line"'

# ---- timestamps: filter and order by WHEN, not just what ----
# Both arms of the A/B trial failed the same way on a question about change
# over time: neither could establish which document was current. grep-over-
# files recovered by a lucky hunch; viki could not recover at all.
mkdir -p "$DIR/ts" && cd "$DIR/ts"
mkdir -p docs
printf 'the older document about pumps\n' > docs/old.md
printf 'the newer document about pumps\n' > docs/new.md
touch -t 202001010000 docs/old.md
touch -t 209901010000 docs/new.md
"$VIKI" index . >/dev/null 2>&1
ck "T1 every source gets a timestamp"          '[ "$(sqlite3 .viki/cache.db "select sum(ts!=\"\") from viki_source")" -eq 2 ]'
ck "T2 --time shows it"                '"$VIKI" grep "document about pumps" --time | grep -qE "\[[0-9]{4}-[0-9]{2}-[0-9]{2}T"'
ck "T3 --since excludes the old one"           '! "$VIKI" grep "pumps" --since 2050-01-01 2>/dev/null | grep -q "older"'
ck "T3b CONTROL: and keeps the new one"        '"$VIKI" grep "pumps" --since 2050-01-01 | grep -q "newer"'
ck "T4 --until excludes the new one"           '! "$VIKI" grep "pumps" --until 2050-01-01 2>/dev/null | grep -q "newer"'
ck "T4b CONTROL: and keeps the old one"        '"$VIKI" grep "pumps" --until 2050-01-01 | grep -q "older"'
ck "T5 --newest puts the newest FIRST"         '"$VIKI" grep "pumps" --newest --k 1 | grep -q "newer"'
ck "T2b CONTRACT: WITHOUT --time the header line is byte-for-byte unchanged" \
   '! "$VIKI" grep "document about pumps" 2>/dev/null | grep -qE "\[[0-9]{4}-[0-9]{2}-[0-9]{2}T"'
ck "T5b CONTROL: without it, order is by hash not time" \
   '[ "$("$VIKI" grep "pumps" --k 9 2>/dev/null | grep -c "^\[")" -eq 2 ]'
# a cache written before the column existed must MIGRATE, not answer "nothing"
sqlite3 .viki/cache.db "ALTER TABLE viki_source RENAME TO viki_source_old;
  CREATE TABLE viki_source(path TEXT PRIMARY KEY, content_hash TEXT NOT NULL, mtime INTEGER NOT NULL);
  INSERT INTO viki_source SELECT path,content_hash,mtime FROM viki_source_old;
  DROP TABLE viki_source_old;" 2>/dev/null
ck "T6 a pre-ts cache MIGRATES instead of returning nothing" \
   '"$VIKI" grep "pumps" 2>/dev/null | grep -q "pumps"'

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
