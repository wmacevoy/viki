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
# C5b: core keeps NO mutable PROCESS-global. Exactly two thread-locals are
# permitted, and both are per-thread for the same reason -- the context they
# describe is:
#
#   g_err     the last error. Shared, one thread would read another's failure,
#             and viki_errmsg() is the only way a caller learns WHY.
#   g_pWatch  the listener registry. A store is reached through a retained
#             VikiStore, and retain's stacks are thread-local, so a store is
#             already per-thread; a shared registry would be the only piece
#             disagreeing about that.
#   g_pBlobHold  the one live statement whose blob pointer viki_blob_get()
#             handed back. sqlite3_column_blob() owns that memory until its
#             statement is stepped, so returning a BORROWED pointer -- which
#             is the whole point, for a 23 MB model nobody wants to copy --
#             requires keeping exactly one alive, per thread, like the store.
#
# Anything ELSE going thread-local is core rolling its own context mechanism
# instead of using retain, which is what C5 exists to prevent. The list is
# explicit rather than a pattern so adding a third requires saying why.
tls=$(grep -nE '_Thread_local|VIKI_TLS' $SRC | grep -v 'define VIKI_TLS' \
      | grep -vE 'g_err|g_pWatch|g_pBlobHold' || true)
[ -z "$tls" ] && ok "C5b thread-local state is exactly the three named above" \
               || bad "C5b core grew an unnamed thread-local" "$tls"
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

# C8: THE HOST NEVER READS THE TABLES.
#
# "all interactions are through viki-core, no poking at the sql tables
# directly" is a requirement, and the probe is the only host core has -- so
# the probe is where it is enforced. It may call viki_sql(), which IS the API
# (SCOPES 1b requires a raw rung so a curated verb is never the only door);
# it may NOT open a statement on the connection itself.
#
# The failure this prevents is not untidiness. A probe that reads the tables
# keeps passing while the API is missing the verb it needed -- so the gap is
# never reported, and the first real host discovers it instead.
#
# WHAT IS ALLOWED, and the line is the documented boundary rather than a
# convenience: the host OPENS AND KEYS the connection -- sqlite3_open,
# PRAGMA key, PRAGMA cipher_version -- because that is precisely the part core
# never learns about. What it may not do is name a CORE TABLE in a statement
# of its own; for that there is viki_sql(), which is the API.
PROBE="$ROOT/core/test/core_probe.c"
raw=$(grep -nE 'sqlite3_(prepare|exec)' "$PROBE" \
      | grep -E 'viki_assert|viki_chunk|viki_fts|viki_sig|viki_blob|viki_event' \
      | grep -v 'viki_sql' || true)
[ -z "$raw" ] && ok "C8 the probe never names a core table outside the API" \
              || bad "C8 the probe prepares its own statements on core's tables" \
                     "$(printf '%s' "$raw" | head -3)"
if grep -q 'viki_count\|viki_each\|viki_sql' "$PROBE"; then
  ok "C8b CONTROL: it does read through the API (C8 is not passing on an empty file)"
else
  bad "C8b CONTROL failed" "no API reads at all -- C8 proves nothing"
fi

echo
sh "$ROOT/core/build.sh" >/dev/null 2>&1 || { echo "build failed"; exit 2; }
"$ROOT/core/build/core-probe" || nFail=$((nFail+1))
printf '\nconstraints: %d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
