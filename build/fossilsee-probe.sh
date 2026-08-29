#!/bin/sh
# fossilsee-probe.sh -- proves the OPTIONAL in-process fossil path
# (libfossilsee, loaded via dlopen) is EQUIVALENT to the `fossil sql`
# subprocess it replaces, on a real encrypted repo carrying every artifact
# type viki extracts without HTTP.
#
# The thing this has to rule out, and the reason it is not just "run it
# twice": a comparison of two paths passes TRIVIALLY if both legs are
# secretly the same path. libfossilsee is optional by design and viki falls
# back silently, so a missing library would make every equivalence
# assertion below pass while proving nothing at all. L1/L2 pin which path
# each leg actually took, using `viki fossilsee-status`, and they are the
# assertions to look at first if this file ever goes green for the wrong
# reason.
#
# Usage: sh build/fossilsee-probe.sh <empty-dir>
#
# Requires: a SQLCipher fossil ($VIKI_FOSSIL_BIN or fossil-see on PATH),
# sqlite3, and a built libfossilsee. Skips -- loudly, and with a nonzero
# tally -- rather than pretending, if the library is absent.
set -u

DIR="${1:-}"
[ -n "$DIR" ] || { echo "usage: sh build/fossilsee-probe.sh <empty-dir>" >&2; exit 2; }
mkdir -p "$DIR" || exit 2
DIR=$(cd "$DIR" && pwd)

ROOT=$(cd "$(dirname "$0")/.." && pwd)
# VIKI_BIN honoured so an OLD binary can be scored against this file -- which
# is how a new assertion is shown to be non-vacuous. Without it every "does it
# fail against the pre-fix build?" check silently re-tests the current one.
VIKI="${VIKI_BIN:-$ROOT/build/dist/viki}"
[ -x "$VIKI" ] || { echo "no $VIKI -- run build/build.sh first" >&2; exit 2; }

FOSSIL="${VIKI_FOSSIL_BIN:-}"
if [ -z "$FOSSIL" ]; then
    if command -v fossil-see >/dev/null 2>&1; then FOSSIL=fossil-see
    else echo "no fossil-see; set VIKI_FOSSIL_BIN" >&2; exit 2; fi
fi
export VIKI_FOSSIL_BIN="$FOSSIL"

# Locate the library unless the caller pinned one.
LIB="${VIKI_FOSSILSEE_LIB:-}"
if [ -z "$LIB" ]; then
    # Both directory names are searched: the sibling checkout was renamed
    # fossil-sqlcipher-libressl -> fossil-see on 2026-08-29, and an older
    # clone (or another machine) still has the long name.  The GitHub repo
    # is still wmacevoy/fossil-sqlcipher-libressl -- only the local path
    # changed -- so neither name is wrong, and searching both costs nothing.
    for c in "$ROOT/../fossil-see/build/dist/libfossilsee.dylib" \
             "$ROOT/../fossil-see/build/dist/libfossilsee.so" \
             "$ROOT/../fossil-sqlcipher-libressl/build/dist/libfossilsee.dylib" \
             "$ROOT/../fossil-sqlcipher-libressl/build/dist/libfossilsee.so"; do
        [ -f "$c" ] && { LIB="$c"; break; }
    done
fi

nPass=0; nFail=0
ok(){   nPass=$((nPass+1)); printf '  ok   %s\n' "$1"; }
bad(){  nFail=$((nFail+1)); printf '  FAIL %s\n' "$1"; [ $# -gt 1 ] && printf '       %s\n' "$2"; }
chk(){  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "expected [$3] got [$2]"; fi }

# Self-contained environment: never touch the developer's real fossil state.
export FOSSIL_HOME="$DIR/home"; mkdir -p "$FOSSIL_HOME"
# A RAW 256-bit key: SQLCipher takes x'<64 hex>' verbatim and skips PBKDF2,
# which is ~52x faster per open (6.4ms vs 333ms, FINDINGS.md) -- and this
# probe opens the repo many times. Built from a variable because the
# nested-quote form this line used to have evaluated to x'"aaa..."' and
# silently fell back to PASSPHRASE derivation.
KEYHEX=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
export FOSSIL_SEE_KEY="x'${KEYHEX}'"
export VIKI_FOSSIL_USER=viki
# Fossil refuses WRITE commands without a resolvable user, and a fresh
# FOSSIL_HOME has no default configured. CLAUDE.md notes this for `fossil
# ticket`; it applies to `wiki create`, `attachment add` and `unversioned
# add` too -- the read-only `fossil wiki list`/`export` calls viki makes
# are what have no such requirement. Setting USER covers every one of them
# uniformly, and this probe already owns its whole environment.
export USER=viki
# BM25-only on purpose: this probe is about EXTRACTION equivalence, and a
# model would add ~seconds per index run for no assertion here.
export VIKI_MODEL_DIR="$DIR/no-such-model"

REPO="$DIR/probe.efossil"
CO="$DIR/co"
W="$DIR/work"
mkdir -p "$CO" "$W"
LOG="$DIR/plant.log"

echo "== planting an encrypted repo with every non-HTTP artifact type =="
"$FOSSIL" init "$REPO" -A viki                    >"$LOG" 2>&1 || { echo "init failed"; exit 2; }
( cd "$CO" && "$FOSSIL" open "$REPO" )           >>"$LOG" 2>&1 || { echo "open failed"; exit 2; }

printf 'The mare at site two refused the trough after the storm.\n' > "$CO/field.md"
printf 'Hay delivery runs Thursdays; the north gate is chained.\n'  > "$CO/logistics.md"
( cd "$CO" && "$FOSSIL" add field.md logistics.md )              >>"$LOG" 2>&1
( cd "$CO" && "$FOSSIL" commit -m "field and logistics notes" --user viki ) >>"$LOG" 2>&1
printf 'Addendum: the trough was refilled Friday morning.\n' >> "$CO/field.md"
( cd "$CO" && "$FOSSIL" commit -m "trough refilled Friday" --user viki )    >>"$LOG" 2>&1

printf 'Standing guidance for winter water hauling at the north pasture.\n' > "$W/wiki.txt"
( cd "$CO" && "$FOSSIL" wiki create WinterWaterHauling "$W/wiki.txt" ) >>"$LOG" 2>&1

( cd "$CO" && "$FOSSIL" ticket add title "north gate chain is frozen" \
    comment "The chain seizes below twenty degrees." --user viki ) >>"$LOG" 2>&1

printf 'Dated observation: subzero week, troughs iced by 0600.\n' > "$W/note.txt"
( cd "$CO" && "$FOSSIL" wiki create "Subzero week observations" "$W/note.txt" \
    --technote "2026-02-01 09:00:00" --technote-tags "weather" \
    -M text/x-markdown ) >>"$LOG" 2>&1

printf 'Bench numbers for the trough heater, recorded on site.\n' > "$W/attach.txt"
( cd "$CO" && "$FOSSIL" attachment add WinterWaterHauling "$W/attach.txt" ) >>"$LOG" 2>&1

# A BINARY ATTACHMENT, ADDED BEFORE THE TEXT ONE. This fixture is the whole
# reason the N-series below can fail: this probe attached ONLY a text file, so
# it certified the two transports "equivalent" while they built DIFFERENT
# corpora.
#
# `fossil sql` prints the concatenated frame as a C STRING, so a payload is cut
# at its first NUL while the declared count is the full BLOB length. The count
# then ran over the NEXT artifact's whole record and could land on a later
# newline: the parser resynced, swallowed that artifact, still reached the
# sentinel, and so reported itself AUTHORITATIVE -- after which sweep_sources()
# DELETED the swallowed attachment's live row. The in-process path passes an
# explicit length and never truncated. Exit 0 on both. (FINDINGS.md)
#
# Ordering matters: the binary one must sort FIRST by a.mtime so its bad count
# runs over the text one.
# LARGE, so the failure does not depend on byte alignment. The payload is cut
# at the NUL (8 bytes survive) while the count says 5009, so the count runs over
# everything that follows INCLUDING the sentinel. The precise consequence then
# varies -- sometimes a resync files one artifact's text under another's header
# and still reaches the sentinel (which is the DATA LOSS case, because the class
# then claims authority and sweep_sources deletes the swallowed row), sometimes
# the sentinel is eaten and the whole class goes quietly non-authoritative. Both
# are wrong and both are exit 0; asserting on the TEXT ATTACHMENT SURVIVING
# covers either without depending on which one a given repo produces.
python3 -c "open('$W/attach.bin','wb').write(b'BBBBBBBB'+b'\x00'+b'Z'*5000)" 2>/dev/null \
  || { printf 'BBBBBBBB\000' > "$W/attach.bin"; \
       i=0; while [ $i -lt 100 ]; do printf 'ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ' >> "$W/attach.bin"; i=$((i+1)); done; }
( cd "$CO" && "$FOSSIL" attachment add WinterWaterHauling "$W/attach.bin" ) >>"$LOG" 2>&1

printf 'Runbook: how to thaw the north gate chain without a torch.\n' > "$W/uv.txt"
( cd "$CO" && "$FOSSIL" unversioned add "$W/uv.txt" --as gate-runbook.txt ) >>"$LOG" 2>&1

echo "== L: which path did each leg actually take? =="
unset VIKI_FOSSILSEE_LIB
S_OFF=$("$VIKI" fossilsee-status 2>&1); R_OFF=$?
case "$S_OFF" in *"not found"*) ok "L1 CONTROL: no library -> subprocess path" ;;
  *) bad "L1 CONTROL: no library -> subprocess path" "status: $S_OFF" ;; esac
[ "$R_OFF" -ne 0 ] || bad "L1b CONTROL: status exits nonzero when unavailable" "exit $R_OFF"
[ "$R_OFF" -ne 0 ] && ok "L1b CONTROL: status exits nonzero when unavailable"

if [ -z "$LIB" ] || [ ! -f "$LIB" ]; then
    bad "L2 library present" "libfossilsee not found; set VIKI_FOSSILSEE_LIB. Every equivalence assertion below would pass trivially, so this probe refuses to run them."
printf '\n%d passed, %d failed\n' "$nPass" "$nFail"
    exit 1
fi
export VIKI_FOSSILSEE_LIB="$LIB"
S_ON=$("$VIKI" fossilsee-status 2>&1); R_ON=$?
case "$S_ON" in *"loaded (ABI"*) ok "L2 library present -> in-process path" ;;
  *) bad "L2 library present -> in-process path" "status: $S_ON" ;; esac
chk "L2b status exits 0 when available" "$R_ON" "0"

echo "== index the same repo down each path =="
run_index(){  # $1 = "on" | "off"; leaves output in $DIR/idx-$1.txt
    rm -rf "$CO/.viki"
    if [ "$1" = "off" ]; then ( cd "$CO" && env -u VIKI_FOSSILSEE_LIB "$VIKI" index . )>"$DIR/idx-$1.txt" 2>&1
    else                      ( cd "$CO" && "$VIKI" index . )                          >"$DIR/idx-$1.txt" 2>&1
    fi
    sqlite3 "$CO/.viki/cache.db" \
      "SELECT path FROM viki_source ORDER BY path;"                 > "$DIR/src-$1.txt"
    sqlite3 "$CO/.viki/cache.db" \
      "SELECT content_hash, chunk_ix FROM viki_chunk ORDER BY 1,2;" > "$DIR/chunks-$1.txt"
    grep 'not authoritative' "$DIR/idx-$1.txt" > "$DIR/auth-$1.txt" 2>/dev/null || : > "$DIR/auth-$1.txt"
}
run_index off
run_index on

N_OFF=$(wc -l < "$DIR/src-off.txt" | tr -d ' ')
N_ON=$(wc -l  < "$DIR/src-on.txt"  | tr -d ' ')

# E: the equivalence claims. Each is a byte comparison, not a count.
if diff -q "$DIR/src-off.txt" "$DIR/src-on.txt" >/dev/null 2>&1
then ok "E1 identical source sets ($N_ON sources)"
else bad "E1 identical source sets" "$(diff "$DIR/src-off.txt" "$DIR/src-on.txt" | head -6)"; fi

# The citation identity. If these ever diverge the two paths compose
# different text for the same artifact, which is cache FRAGMENTATION across
# peers (CLAUDE.md's sharing contract), not a cosmetic difference.
if diff -q "$DIR/chunks-off.txt" "$DIR/chunks-on.txt" >/dev/null 2>&1
then ok "E2 identical content_hash/chunk_ix set ($(wc -l < "$DIR/chunks-on.txt" | tr -d ' ') chunks)"
else bad "E2 identical content_hash/chunk_ix set" "$(diff "$DIR/chunks-off.txt" "$DIR/chunks-on.txt" | head -6)"; fi

# The AUTHORITY verdict is what licenses sweep_sources() to delete rows, so
# a disagreement here is the dangerous kind. This assertion is exactly what
# caught add_content_sql_commands() being missing: content() is not
# registered by db_open_repository(), so ckin:/note:/attach: all silently
# went non-authoritative on the in-process leg while the subprocess leg
# thought they were fine.
if diff -q "$DIR/auth-off.txt" "$DIR/auth-on.txt" >/dev/null 2>&1
then ok "E3 identical authority verdicts"
else bad "E3 identical authority verdicts" "off: $(cat "$DIR/auth-off.txt")
       on:  $(cat "$DIR/auth-on.txt")"; fi

# C: coverage -- prove the artifact types are really present, so E1/E2 are
# comparing something substantial rather than two empty sets.
for ns in "ckin:" "wiki:" "ticket:" "note:" "attach:" "uv:"; do
    if grep -q "^$ns" "$DIR/src-on.txt"; then ok "C[$ns] extracted"
    else bad "C[$ns] extracted" "absent from the indexed source list"; fi
done
if grep -q '^\./field\.md' "$DIR/src-on.txt"; then ok "C[files] extracted"
else bad "C[files] extracted"; fi

# R: retrieval agrees end to end, not just the tables.
ask_hash(){ ( cd "$CO" && $2 "$VIKI" ask "$1" --k 1 2>/dev/null ) | awk 'NR==1{print $3}'; }
rm -rf "$CO/.viki"; run_index off; A_OFF=$(ask_hash "north gate chain frozen" "env -u VIKI_FOSSILSEE_LIB")
rm -rf "$CO/.viki"; run_index on;  A_ON=$(ask_hash  "north gate chain frozen" "env")
if [ -n "$A_ON" ] && [ "$A_OFF" = "$A_ON" ]; then ok "R1 'viki ask' top hit identical ($A_ON)"
else bad "R1 'viki ask' top hit identical" "off=[$A_OFF] on=[$A_ON]"; fi

# F: failure handling. A bad library must degrade, never crash or corrupt.
echo "not a library" > "$DIR/broken.dylib"
S_BAD=$(VIKI_FOSSILSEE_LIB="$DIR/broken.dylib" "$VIKI" fossilsee-status 2>&1)
case "$S_BAD" in *"did not load"*) ok "F1 CONTROL: unloadable library reports why" ;;
  *) bad "F1 CONTROL: unloadable library reports why" "status: $S_BAD" ;; esac

rm -rf "$CO/.viki"
( cd "$CO" && VIKI_FOSSILSEE_LIB="$DIR/broken.dylib" "$VIKI" index . ) >"$DIR/idx-broken.txt" 2>&1
RC_BAD=$?
chk "F2 CONTROL: index still succeeds with a broken library" "$RC_BAD" "0"
sqlite3 "$CO/.viki/cache.db" "SELECT path FROM viki_source ORDER BY path;" > "$DIR/src-broken.txt"
if diff -q "$DIR/src-off.txt" "$DIR/src-broken.txt" >/dev/null 2>&1
then ok "F3 CONTROL: broken library falls back to the subprocess result"
else bad "F3 CONTROL: broken library falls back" "$(diff "$DIR/src-off.txt" "$DIR/src-broken.txt" | head -4)"; fi

# S: the standalone guarantee -- viki must not have acquired a LINK-time
# dependency on fossil. That is the property that keeps the four-platform
# build working on machines with no fossil-see at all.
if command -v otool >/dev/null 2>&1; then DEPS=$(otool -L "$VIKI" 2>/dev/null)
elif command -v ldd >/dev/null 2>&1;  then DEPS=$(ldd "$VIKI" 2>/dev/null)
else DEPS=""; fi
if [ -n "$DEPS" ]; then
    if printf '%s' "$DEPS" | grep -qi fossil
    then bad "S1 viki links no fossil library" "$(printf '%s' "$DEPS" | grep -i fossil)"
    else ok "S1 viki links no fossil library at build time"; fi
else
    printf '  --   S1 skipped (no otool/ldd)\n'
fi

# ---- E: the process must EXIT CLEANLY, not merely print the right thing ----
#
# THE GAP THIS CLOSES. Every assertion above compares OUTPUT, and none of them
# reads an exit status -- so for as long as it existed, `viki index` on an
# ENCRYPTED repo with libfossilsee loaded printed a complete, correct answer
# and then died with SIGSEGV (139) inside exit(). The whole suite stayed green.
#
# Cause: atexit is LIFO. main() registered viki_fossilsee_shutdown as its FIRST
# statement, before this file ever dlopen()s the library, so the library's own
# terminators -- LibreSSL's among them -- sat above it and ran first. viki's
# handler then closed the repository through a finalised SQLCipher codec.
# Registered after dlopen() instead (register_shutdown_once()). FINDINGS.md.
#
# Two conditions are BOTH required and neither is the common test setup: the
# repo must be ENCRYPTED (a plaintext repo never reaches sqlite3Codec) and the
# library must be LOADED. That is precisely the production configuration.
#
# AND THE EXIT STATUS IS NOT THE WORST OF IT: stdout is block buffered when
# redirected, so the crash beat stdio's flush and a redirected answer was lost
# entirely -- measured at 0 bytes. E2 asserts the bytes arrive.
echo "== E: clean exit on an encrypted repo with the library loaded =="
if [ -n "${LIB:-}" ] && [ -f "${LIB:-/nonexistent}" ]; then
    ECO="$DIR/exitco"
    rm -rf "$ECO"; mkdir -p "$ECO"
    ( cd "$ECO" && "$FOSSIL" open "$REPO" >/dev/null 2>&1 )
    printf 'exit status fixture\n' > "$ECO/exitcheck.md"
    erc=0
    ( cd "$ECO" && VIKI_FOSSILSEE_LIB="$LIB" VIKI_MODEL_DIR="$DIR/nomodel" \
        "$VIKI" index . >"$DIR/exit.out" 2>"$DIR/exit.err" ) || erc=$?
    [ "$erc" -eq 0 ] \
      && ok "E1 viki index exits 0 (encrypted repo + libfossilsee loaded)" \
      || bad "E1 viki index exits 0 (encrypted repo + libfossilsee loaded)" \
             "exit $erc -- 139 is the atexit-ordering SIGSEGV; see FINDINGS.md"
    # The report the crash used to swallow. A redirected run must still deliver
    # its bytes; 0 bytes with a correct-looking terminal run is the shape that
    # hid this.
    [ -s "$DIR/exit.err" ] \
      && ok "E2 ...and its report survives the redirect (stdio flushed)" \
      || bad "E2 ...and its report survives the redirect" "0 bytes captured"
else
    printf '  --   E1/E2 skipped (no libfossilsee; nothing to order against)\n'
fi

# ---- N: the two transports agree on BINARY input ---------------------------
#
# WHY THE FIXTURE GAINED A BINARY ATTACHMENT. `fossil sql` prints a frame as a
# C STRING, so a payload is cut at its first NUL while the declared count is the
# full BLOB length. The count then runs past the truncated body into whatever
# follows. The in-process path passes an explicit length and never truncated --
# so the two transports this probe certifies "equivalent" could build DIFFERENT
# corpora, and this file could not see it because it only ever attached a TEXT
# file. Fixed by excluding NUL-bearing attachments in SQL (viki_index.c).
#
# HONEST LIMIT, and it is why there is one assertion here and not four. The
# END-TO-END corruption -- a resync that swallows the next artifact, still
# reaches the sentinel, claims authority, and gets the swallowed row DELETED by
# sweep_sources() -- depends on the bad count landing exactly on a newline, and
# therefore on the byte lengths of the surrounding records and on attachment
# mtime ordering. It reproduces in a crafted scratch repo (FINDINGS.md has the
# byte-exact recipe) and it does NOT reproduce here. Three assertions written
# against it passed identically on a binary WITHOUT the fix, so they were
# deleted rather than shipped: an assertion that cannot fail is worse than
# none. What remains is the invariant that does hold on any input -- the two
# transports must agree -- which is this file's whole subject.
echo "== N: the transports agree on binary input =="
NCO="$DIR/nulco"
rm -rf "$NCO"; mkdir -p "$NCO"
( cd "$NCO" && "$FOSSIL" open "$REPO" >/dev/null 2>&1 )
if [ -n "${LIB:-}" ] && [ -f "${LIB:-/nonexistent}" ]; then
    ( cd "$NCO" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" index . >/dev/null 2>&1 )
    NA=$( cd "$NCO" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" sql \
          "SELECT group_concat(path) FROM (SELECT path FROM viki_source WHERE path LIKE 'attach:%' ORDER BY path)" 2>/dev/null | tr -d ' ' )
    rm -rf "$NCO/.viki"
    ( cd "$NCO" && VIKI_FOSSILSEE_LIB="$LIB" VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" index . >/dev/null 2>&1 )
    NB=$( cd "$NCO" && VIKI_FOSSILSEE_LIB="$LIB" VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" sql \
          "SELECT group_concat(path) FROM (SELECT path FROM viki_source WHERE path LIKE 'attach:%' ORDER BY path)" 2>/dev/null | tr -d ' ' )
    [ -n "$NA" ] && [ "$NA" = "$NB" ] \
      && ok "N1 both transports build the SAME attach: set with a NUL-bearing attachment present" \
      || bad "N1 transports disagree on binary input" "subprocess=$NA in-process=$NB"
else
    printf '  --   N1 skipped (no libfossilsee)\n'
fi


# == P: viki NEVER FORKS ==================================================
#
# viki cannot fork on iOS, which is the entire reason the in-process path
# exists. Every dev machine and CI runner CAN fork, so a subprocess creeping
# back into the index path is invisible here and fails only on the platform
# nobody tests first. VIKI_NO_FORK=1 turns every remaining fork site into a
# loud refusal, which makes that platform property assertable on a laptop.
#
# P2 IS WHAT KEEPS P1 HONEST. A no-fork run that quietly indexed only the
# checkout files would satisfy "exits 0" while proving nothing, so P1
# compares the SOURCE SET against an unrestricted run rather than the exit
# status, and P2 asserts the refusal is real by removing the library.
echo "== P: viki never forks (the iOS constraint, asserted here) =="
PCO="$DIR/noforkco"
rm -rf "$PCO"; mkdir -p "$PCO"
( cd "$PCO" && "$FOSSIL" open "$REPO" >/dev/null 2>&1 )
if [ -n "${LIB:-}" ] && [ -f "${LIB:-/nonexistent}" ]; then
    ( cd "$PCO" && VIKI_FOSSILSEE_LIB="$LIB" VIKI_MODEL_DIR="$DIR/nomodel" \
        "$VIKI" index . >/dev/null 2>&1 )
    PA=$( cd "$PCO" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" sql \
          "SELECT group_concat(path) FROM (SELECT path FROM viki_source ORDER BY path)" 2>/dev/null | tr -d ' ' )
    rm -rf "$PCO/.viki"
    ( cd "$PCO" && VIKI_NO_FORK=1 VIKI_FOSSILSEE_LIB="$LIB" VIKI_MODEL_DIR="$DIR/nomodel" \
        "$VIKI" index . >/dev/null 2>&1 )
    PB=$( cd "$PCO" && VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" sql \
          "SELECT group_concat(path) FROM (SELECT path FROM viki_source ORDER BY path)" 2>/dev/null | tr -d ' ' )
    [ -n "$PA" ] && [ "$PA" = "$PB" ] \
      && ok "P1 VIKI_NO_FORK=1 indexes the SAME source set (no fork needed)" \
      || bad "P1 a no-fork run indexes less than an unrestricted one" "forking=$PA no-fork=$PB"

    rm -rf "$PCO/.viki"
    POUT=$( cd "$PCO" && VIKI_NO_FORK=1 VIKI_FOSSILSEE_LIB=/nonexistent \
            VIKI_MODEL_DIR="$DIR/nomodel" "$VIKI" index . 2>&1 )
    printf '%s' "$POUT" | grep -q 'REFUSING TO FORK' \
      && ok "P2 CONTROL: with no library, VIKI_NO_FORK=1 refuses LOUDLY" \
      || bad "P2 no-fork without a library did not report refusing to fork" "$POUT"
else
    printf '  --   P1 skipped (no libfossilsee)\n'
    printf '  --   P2 skipped (no libfossilsee)\n'
fi


printf '\n%d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
