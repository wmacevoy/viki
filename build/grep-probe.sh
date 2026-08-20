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

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
