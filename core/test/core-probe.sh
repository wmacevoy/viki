#!/bin/sh
# core-probe.sh -- the C probe, plus the SOURCE properties core claims.
#
# The C assertions prove behaviour. These prove the CONSTRAINTS, which is the
# other half: viki-core's whole argument is what it does not do, and "we did
# not add a fopen" is exactly the kind of claim that rots silently.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/core/src/viki_core.c $ROOT/core/include/viki_core.h $ROOT/core/src/viki_cal.c $ROOT/core/include/viki_cal.h"
nPass=0; nFail=0
ok(){   nPass=$((nPass+1)); printf '  ok    %s\n' "$1"; }
bad(){  nFail=$((nFail+1)); printf '  FAIL  %s\n        %s\n' "$1" "$2"; }
none(){ # none <label> <regex>
  m=$(grep -nE "$2" $SRC | grep -v '^\s*\*\*' | grep -vE '\*\*|^\S+:[0-9]+: *\*' || true)
  [ -z "$m" ] && ok "$1" || bad "$1" "$(printf '%s' "$m" | head -3)"
}
echo "== C: the constraints, asserted against the source =="
none "C1 no filesystem (no fopen/open/stat/mkdir)"    '\b(fopen|freopen|[^_a-z]open\(|stat\(|mkdir\()'
none "C2 no network (no socket/connect/send/recv)"    '\b(socket\(|connect\(|send\(|recv\(|getaddrinfo)'
none "C3 no subprocess (no fork/exec/popen/system)"   '\b(fork\(|exec[lv]|popen\(|system\()'
none "C4 no fossil (no fossil symbol anywhere)"       'fossil'
none "C5 no threading primitives of core's own"       'pthread_|thrd_|mtx_'
# C5b: core keeps NO mutable process-global. The one _Thread_local permitted is
# the error buffer -- per-thread precisely BECAUSE the context it describes is,
# and a shared one would let one thread read another's failure. Anything else
# thread-local would be core rolling its own context mechanism instead of using
# retain, which is the thing C5 exists to prevent.
tls=$(grep -nE '_Thread_local|VIKI_TLS' $SRC | grep -v 'define VIKI_TLS' | grep -v 'g_err' || true)
[ -z "$tls" ] && ok "C5b the only thread-local is the error buffer" \
               || bad "C5b core has thread-local state other than g_err" "$tls"
# C6 is the positive control for C1-C5: if the grep itself were broken, every
# one of them would pass against any file at all.
if grep -qE 'sqlite3_open|sqlite3_prepare' $SRC; then
  ok "C6 CONTROL: the same grep DOES find sqlite3_* (C1-C5 are not vacuous)"
else
  bad "C6 CONTROL failed" "the grep finds nothing at all -- C1-C5 prove nothing"
fi

# C7 IS THE ONE THIS FILE EXISTS FOR NOW. Calendar input is jsCalendar and
# every field is reached with json_extract(); SQLite IS the parser. If a
# hand-written lexer reappears -- strtok on the input, a quote-state machine, a
# fold rule -- this fails, because that is the moment core stops being "the
# SQLite contracts" and starts being a parser with a database attached.
lex=$(grep -nE 'strtok|ics_unfold|ics_split|inQuote|unescape' $SRC || true)
[ -z "$lex" ] && ok "C7 no hand-written parser: SQLite does the parsing" \
              || bad "C7 a lexer reappeared in core" "$(printf '%s' "$lex" | head -3)"
if grep -q 'json_extract' $SRC; then
  ok "C7b CONTROL: json_extract IS present (C7 is not passing on an empty file)"
else
  bad "C7b CONTROL failed" "no json_extract -- C7 proves nothing"
fi

echo
sh "$ROOT/core/build.sh" >/dev/null 2>&1 || { echo "build failed"; exit 2; }
"$ROOT/core/build/core-probe" || nFail=$((nFail+1))
printf '\nconstraints: %d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
