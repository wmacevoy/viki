#!/bin/sh
# substrate-probe.sh -- INVERTED CONTROLS OVER THE SUBSTRATE.
#
# Every other probe in this tree asserts that viki does something. This one
# asserts that the SUBSTRATE CANNOT do something -- and names the viki code
# that exists only because of it.
#
# SO A FAILURE HERE IS GOOD NEWS. It does not mean viki broke. It means the
# substrate gained a capability and a workaround in viki is now dead weight.
# Do NOT "fix" a failure by restoring the limitation. Fix it by deleting the
# code the failure names.
#
# WHY THIS EXISTS -- the worked example, measured 2026-08-29:
#
#   index_unversioned() forked `fossil unversioned cat` ONCE PER BLOB for
#   about a year. The reason was written in a comment: unversioned.content is
#   zlib-compressed behind a 4-byte length prefix, and `unversioned cat` "is
#   the only extraction path that does not require linking zlib."
#
#   That was never true. Fossil registers a decompress() SQL function in
#   add_content_sql_commands() -- the SAME call that registers content(),
#   which viki already depended on for five other classes. The fix was one
#   SQL expression; the cost of not knowing was O(blobs) process spawns.
#
#   A comment is a claim, and a claim with no test never expires. This repo
#   already knows that -- test/m1.sh labels eighteen CONTROL assertions. It
#   had simply never pointed that discipline at its own workarounds.
#
# Usage: sh build/substrate-probe.sh <empty-dir>
#
# NOT in CI's required set (yet). A lifted limitation is a scheduling
# question, not a broken build -- but it should be SEEN, so this exits 1 when
# one lifts.

DIR="${1:?usage: substrate-probe.sh <empty-dir>}"
mkdir -p "$DIR" || exit 2
DIR=$(cd "$DIR" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)

FOSSIL="${VIKI_FOSSIL_BIN:-}"
if [ -z "$FOSSIL" ]; then
  for c in "$ROOT/vendor/fossil-see/build/dist/fossil-see" \
           "$ROOT/../fossil-see/build/dist/fossil-see" \
           "$(command -v fossil-see 2>/dev/null)" "$(command -v fossil 2>/dev/null)"; do
    [ -n "$c" ] && [ -x "$c" ] && { FOSSIL="$c"; break; }
  done
fi
LIB="${VIKI_FOSSILSEE_LIB:-}"
if [ -z "$LIB" ]; then
  for c in "$ROOT/../fossil-see/build/dist/libfossilsee.dylib" \
           "$ROOT/../fossil-see/build/dist/libfossilsee.so" \
           "$ROOT/vendor/fossil-see/build/dist/libfossilsee.dylib" \
           "$ROOT/vendor/fossil-see/build/dist/libfossilsee.so"; do
    [ -f "$c" ] && { LIB="$c"; break; }
  done
fi

nHeld=0; nLifted=0; nSkip=0
held(){   nHeld=$((nHeld+1));     printf '  still limited  %-4s %s\n' "$1" "$2"; }
lifted(){ nLifted=$((nLifted+1))
          printf '\n  *** LIFTED     %-4s %s\n' "$1" "$2"
          printf '      NOW DEAD:  %s\n\n' "$3"; }
skip(){   nSkip=$((nSkip+1));     printf '  --  skipped    %-4s %s (%s)\n' "$1" "$2" "$3"; }

# ---- a scratch repo the checks can share ------------------------------
REPO="$DIR/s.fossil"; CO="$DIR/co"
export FOSSIL_HOME="$DIR"
if [ -n "$FOSSIL" ] && [ -x "$FOSSIL" ]; then
  "$FOSSIL" init "$REPO" >/dev/null 2>&1
  mkdir -p "$CO" && ( cd "$CO" && "$FOSSIL" open "$REPO" >/dev/null 2>&1 )
  U=$("$FOSSIL" sql -R "$REPO" "SELECT login FROM user WHERE cap GLOB '*s*' LIMIT 1;" 2>/dev/null)
  ( cd "$CO" && echo seed > seed.txt && "$FOSSIL" add seed.txt >/dev/null 2>&1 \
    && "$FOSSIL" commit -m seed >/dev/null 2>&1 )
  ( cd "$CO" && "$FOSSIL" -U "$U" ticket add title "probe ticket" status Open \
    comment "body" >/dev/null 2>&1 )
fi

# ---- T0: CAN THIS PROBE TELL HELD FROM LIFTED? ------------------------
#
# EVERY ASSERTION BELOW PASSES WHEN NOTHING HAPPENS, which is the shape that
# has produced a vacuous green in this repo more than once -- an early keywrap
# build passed its own round-trip while emitting keys real `age` rejected, and
# a pinsig draft scored 7 PASS on assertions that were all exit-status noise.
# A probe of limitations is especially exposed: "still limited" is also what a
# broken detector prints.
#
# So T0 runs the detection against a limitation we KNOW was lifted this week.
# /json/finfo dropped every deletion until fossil-see's
# fossil-json-finfo-deletions.patch; if this binary carries that patch, the
# detector MUST see the "removed" state. If it does not, the machinery is
# broken and every line below is noise, so the probe refuses to report.
if [ -z "$FOSSIL" ]; then
  skip T0 "the detector can see a lifted limitation" "no fossil binary"
else
  ( cd "$CO" && echo t > t0.txt && "$FOSSIL" add t0.txt >/dev/null 2>&1      && "$FOSSIL" commit -m t0-add >/dev/null 2>&1      && "$FOSSIL" rm t0.txt >/dev/null 2>&1      && "$FOSSIL" commit -m t0-del >/dev/null 2>&1 )
  JOUT=$( cd "$CO" && "$FOSSIL" json finfo --name t0.txt 2>/dev/null )
  # "added" is the did-we-get-output-at-all sentinel, not "modified": this
  # fixture adds then deletes, so there IS no modify step and using it as the
  # sentinel made T0 skip against a binary that was detecting correctly.
  JADD=$(printf '%s' "$JOUT" | grep -c '"state":"added"')
  JDEL=$(printf '%s' "$JOUT" | grep -c '"state":"removed"')
  if [ "${JADD:-0}" -eq 0 ]; then
    skip T0 "the detector can see a lifted limitation" "json finfo produced nothing to read"
  elif [ "${JDEL:-0}" -ge 1 ]; then
    printf '  selftest       T0   detector sees a KNOWN-lifted limitation (json finfo reports "removed")\n'
  else
    printf '  FAIL           T0   this fossil does NOT carry the json-finfo deletion fix,\n'
    printf '                      so the detector is UNVALIDATED and every result below\n'
    printf '                      would be unfalsifiable.\n'
    printf '                      binary: %s\n' "$FOSSIL"
    printf '                      The usual cause is a STALE BUILD OUTPUT: the submodule\n'
    printf '                      SOURCE carries the patch but build/dist/ predates it.\n'
    printf '                        sh vendor/fossil-see/build/build.sh\n'
    printf '                      or point VIKI_FOSSIL_BIN at a build that has it.\n'
    exit 2
  fi
fi

echo "== substrate limitations viki is currently paying for =="

# ---- S1 -------------------------------------------------------------------
# THE most load-bearing one in the tree. `fossil sql` cannot distinguish
# "returned no rows" from "failed to prepare" -- both exit 0. That ambiguity
# is what let sweep_sources() once delete every forum: row, and it is the
# entire reason for the counted framing protocol and its #viki-eof sentinel.
if [ -z "$FOSSIL" ]; then
  skip S1 "fossil sql exits 0 on a FAILED prepare" "no fossil binary"
else
  "$FOSSIL" sql -R "$REPO" --readonly "SELEKT 1;" >/dev/null 2>&1; rcSyn=$?
  "$FOSSIL" sql -R "$REPO" --readonly "SELECT * FROM no_such_table;" >/dev/null 2>&1; rcTab=$?
  if [ "$rcSyn" -eq 0 ] && [ "$rcTab" -eq 0 ]; then
    held S1 "fossil sql exits 0 on a FAILED prepare (syntax and missing table)"
  else
    lifted S1 "fossil sql now reports a failed prepare in its exit status" \
      "the #viki-eof sentinel's reason for existing -- framed_next()/VIKI_FRAME_EOF, src/viki_index.c. The counted FRAMING is still needed for multi-row payloads; the SENTINEL is not."
  fi
fi

# ---- S2 -------------------------------------------------------------------
# There is no `fossil forum export`, so index_forum() reads event/blob and
# parses W cards by hand.
if [ -z "$FOSSIL" ]; then
  skip S2 "no 'fossil forum export' subcommand" "no fossil binary"
elif "$FOSSIL" help forum 2>&1 | grep -qi 'export'; then
  lifted S2 "fossil forum now has an export subcommand" \
    "index_forum()'s hand-rolled event/blob query, src/viki_index.c"
else
  held S2 "no 'fossil forum export' subcommand"
fi

# ---- S3 -------------------------------------------------------------------
# ticketchng.icomment is NULL in practice, so index_ticket_changes() parses
# the artifact's J cards instead of reading the column that looks right.
if [ -z "$FOSSIL" ]; then
  skip S3 "ticketchng.icomment is NULL in practice" "no fossil binary"
else
  ICOM=$("$FOSSIL" sql -R "$REPO" --readonly \
         "SELECT coalesce(count(icomment),0) FROM ticketchng;" 2>/dev/null)
  NROW=$("$FOSSIL" sql -R "$REPO" --readonly \
         "SELECT count(*) FROM ticketchng;" 2>/dev/null)
  if [ "${NROW:-0}" -eq 0 ]; then
    skip S3 "ticketchng.icomment is NULL in practice" "no ticketchng rows to test"
  elif [ "${ICOM:-0}" -eq 0 ]; then
    held S3 "ticketchng.icomment is NULL even with a comment present"
  else
    lifted S3 "ticketchng.icomment is now populated" \
      "the J-card parsing in index_ticket_changes(), src/viki_index.c"
  fi
fi

# ---- S4 -------------------------------------------------------------------
# A stock sqlite3 cannot run viki's vector function, because ndvss is
# statically linked into viki and registered at open. That is the whole
# reason `viki sql` exists as a surface (SCOPES 1b): without it an agent
# cannot do a vector query at all and `ask` is the only door.
if ! command -v sqlite3 >/dev/null 2>&1; then
  skip S4 "stock sqlite3 has no ndvss_cosine_similarity_f" "no sqlite3 on PATH"
elif sqlite3 :memory: "SELECT ndvss_cosine_similarity_f(x'00',x'00',1);" >/dev/null 2>&1; then
  lifted S4 "a stock sqlite3 can now run the vector function" \
    "the argument that 'viki sql' is the ONLY door to the raw rung (SCOPES 1b). The command can stay; its justification changes."
else
  held S4 "stock sqlite3 has no ndvss_cosine_similarity_f"
fi

# ---- S5 -------------------------------------------------------------------
# pragma_table_info() is DENIED in-process: fs_authorizer() allow-lists
# SELECT/READ/FUNCTION/RECURSIVE and SQLITE_PRAGMA is none of those -- even
# though a pragma function is not a write. So ticket_has_col() parses DDL out
# of sqlite_master instead of asking SQLite directly.
if [ -z "$LIB" ] || [ ! -f "$LIB" ]; then
  skip S5 "pragma_table_info is denied in-process" "no libfossilsee"
elif ! command -v cc >/dev/null 2>&1; then
  skip S5 "pragma_table_info is denied in-process" "no cc to build the harness"
elif [ -z "$FOSSIL" ]; then
  skip S5 "pragma_table_info is denied in-process" "no fossil binary (no repo)"
else
  cat > "$DIR/h.c" <<'HARNESS'
#include <stdio.h>
#include <dlfcn.h>
typedef struct fossilsee fossilsee;
typedef int (*fs_row)(void*,int,const char*const*,const int*);
static int row(void*a,int n,const char*const*v,const int*l){(void)a;(void)n;(void)v;(void)l;return 0;}
int main(int c,char**v){
    void *h; int (*o)(const char*,const char*,fossilsee**);
    void (*cl)(fossilsee*); int (*s)(fossilsee*,const char*,fs_row,void*);
    fossilsee *p=0; int rc; (void)c;
    h=dlopen(v[1],RTLD_NOW); if(!h) return 90;
    o=dlsym(h,"fossilsee_open"); cl=dlsym(h,"fossilsee_close"); s=dlsym(h,"fossilsee_sql");
    if(!o||!cl||!s) return 91;
    if(o(v[2],0,&p)) return 92;
    rc=s(p,"SELECT count(*) FROM pragma_table_info('ticket');",row,0);
    cl(p); return rc ? 1 : 0;   /* 0 == the pragma WORKED */
}
HARNESS
  if ! cc -o "$DIR/h" "$DIR/h.c" >/dev/null 2>&1; then
    skip S5 "pragma_table_info is denied in-process" "harness did not compile"
  else
    "$DIR/h" "$LIB" "$REPO" >/dev/null 2>&1; rcP=$?
    case "$rcP" in
      0)  lifted S5 "pragma_table_info now works through libfossilsee" \
            "ticket_has_col()'s DDL parser, src/viki_index.c -- replace it with pragma_table_info()" ;;
      1)  held S5 "pragma_table_info is denied in-process (fs_authorizer)" ;;
      9*) skip S5 "pragma_table_info is denied in-process" "harness could not open the repo (rc=$rcP)" ;;
      *)  skip S5 "pragma_table_info is denied in-process" "harness rc=$rcP" ;;
    esac
  fi
fi

# ---- S6 -------------------------------------------------------------------
# wiki_filter_mimetypes() silently rewrites anything outside its allowlist to
# text/x-fossil-wiki. The patch was written, verified, and DELIBERATELY backed
# out ("fossil is not calendar aware, an honest viki feature"), so this is a
# limitation viki chose to keep -- and the probe records it as such, because a
# chosen limitation still needs to be a known one.
if [ -z "$FOSSIL" ]; then
  skip S6 "a wiki page cannot carry the text/calendar mimetype" "no fossil binary"
else
  printf 'BEGIN:VCALENDAR\nVERSION:2.0\nEND:VCALENDAR\n' > "$DIR/c.ics"
  ( cd "$CO" && "$FOSSIL" -U "$U" wiki create "ProbeCal" "$DIR/c.ics" \
      --mimetype text/calendar >/dev/null 2>&1 )
  NCARD=$("$FOSSIL" sql -R "$REPO" --readonly \
    "SELECT count(*) FROM tag,tagxref,blob b WHERE tag.tagname='wiki-ProbeCal'
       AND tagxref.tagid=tag.tagid AND b.rid=tagxref.rid
       AND content(b.uuid) GLOB '*N text/calendar*';" 2>/dev/null)
  if [ "${NCARD:-0}" -ge 1 ]; then
    lifted S6 "a wiki page now keeps the text/calendar mimetype" \
      "FINDINGS.md's claim that viki cannot mark calendar-ness in the N card. The DESIGN choice (calendar-ness is viki's projection, not Fossil's) stands on its own; only the constraint is gone."
  else
    held S6 "a wiki page cannot carry the text/calendar mimetype"
  fi
fi

# ---- S7 / S8 / S9 --------------------------------------------------------
# "viki cache push/pull forks" is THREE claims, not one, and they have three
# different blockers. Splitting them matters because they die on different
# days and only one of them is actually intrinsic:
#
#   uv export  read + decompress            -> needs NO fork in-process (S9)
#   uv add     writable connection          -> blocked by fs_authorizer (S7)
#   uv sync    the network sync protocol    -> genuinely not SQL (S8)
#
# unversioned_write() is REPLACE INTO unversioned + hname_hash + blob_compress
# + admin_log. No artifact, no Merkle DAG -- consistent with SYNC.md's "uv
# blobs are name-addressed with no hash in the protocol at all." So `uv add`
# wants a WRITE, not a subprocess; and compression is optional (encoding=0 is
# legal and Fossil reads both), so it does not even strictly want zlib.
if [ -z "$LIB" ] || [ ! -f "$LIB" ] || ! command -v nm >/dev/null 2>&1; then
  skip S7 "libfossilsee exports no WRITE verb" "no libfossilsee or no nm"
  skip S8 "libfossilsee exports no SYNC verb" "no libfossilsee or no nm"
else
  SYMS=$(nm -g "$LIB" 2>/dev/null | grep -o 'fossilsee_[a-z_]*' | sort -u)
  if [ -z "$SYMS" ]; then
    skip S7 "libfossilsee exports no WRITE verb" "nm listed no fossilsee symbols"
    skip S8 "libfossilsee exports no SYNC verb" "nm listed no fossilsee symbols"
  else
    if printf '%s\n' "$SYMS" | grep -qE 'fossilsee_(uv_put|uv_add|wiki_put|ticket_new|ticket_change|write)'; then
      lifted S7 "libfossilsee now exports a WRITE verb" \
        "the `uv add` fork in viki_cmd_cache_push_opts(), src/viki_cache.c"
    else
      held S7 "libfossilsee exports no WRITE verb (fs_authorizer denies every write)"
    fi
    # S8 IS THE REAL BLOCKER ON "viki never forks". Everything else on the
    # push/pull path is expressible as SQL; a network protocol is not.
    if printf '%s\n' "$SYMS" | grep -qE 'fossilsee_(sync|clone|uv_sync|push|pull)'; then
      lifted S8 "libfossilsee now exports a SYNC verb" \
        "the `uv sync` fork in BOTH viki_cmd_cache_push_opts() and viki_cmd_cache_pull_opts(), src/viki_cache.c -- this is the last INTRINSIC reason VIKI_NO_FORK cannot be the default. See fossil-see embed/API_V1.md."
    else
      held S8 "libfossilsee exports no SYNC verb (the one genuinely un-SQL-able leg)"
    fi
  fi
fi

# ---- S9 -------------------------------------------------------------------
# `viki cache pull` forks `fossil uv export` to get the cache db out. The
# reason was never uv's nature -- it is that the SUBPROCESS transport cannot
# carry an embedded NUL, and a SQLite file is NUL in its ninth byte. In
# process there is no such limit. If this reads back byte-identical, the
# export fork is dead code on any peer that loads the library.
if [ -z "$LIB" ] || [ ! -f "$LIB" ]; then
  skip S9 "a uv blob cannot be read without forking" "no libfossilsee"
elif [ -z "$FOSSIL" ] || ! command -v cc >/dev/null 2>&1; then
  skip S9 "a uv blob cannot be read without forking" "need fossil and cc"
else
  # a COMPRESSIBLE binary blob: encoding=1, so decompress() is exercised, and
  # NUL appears early so a NUL-truncating transport is caught immediately.
  "$ROOT/build/dist/viki" --version >/dev/null 2>&1   # (no-op; keeps shellcheck honest)
  python3 - "$DIR/uvbin" <<'PYBIN' 2>/dev/null || true
import struct, sys
rows=b''.join(struct.pack('<IIQ', i, i%7, (i*2654435761) % (1<<32)) for i in range(20000))
open(sys.argv[1],'wb').write(b'SQLite format 3\x00' + rows)
PYBIN
  if [ ! -s "$DIR/uvbin" ]; then
    skip S9 "a uv blob cannot be read without forking" "could not build the binary fixture"
  else
    ( cd "$CO" && "$FOSSIL" unversioned add "$DIR/uvbin" --as probe.bin >/dev/null 2>&1 )
    cat > "$DIR/uv.c" <<'UVH'
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
typedef struct fossilsee fossilsee;
typedef int (*fs_row)(void*,int,const char*const*,const int*);
static FILE *g_out;
static int row(void*a,int n,const char*const*v,const int*l){
    (void)a; if(n<1||!v[0]) return 0;
    fwrite(v[0],1,(size_t)(l?l[0]:(int)strlen(v[0])),g_out); return 0; }
int main(int c,char**v){
    void *h; int (*o)(const char*,const char*,fossilsee**); void (*cl)(fossilsee*);
    int (*s)(fossilsee*,const char*,fs_row,void*); fossilsee *p=0; int rc; (void)c;
    h=dlopen(v[1],RTLD_NOW); if(!h) return 90;
    o=dlsym(h,"fossilsee_open"); cl=dlsym(h,"fossilsee_close"); s=dlsym(h,"fossilsee_sql");
    if(!o||!cl||!s) return 91;
    if(o(v[2],0,&p)) return 92;
    g_out=fopen(v[3],"wb"); if(!g_out) return 93;
    rc=s(p,"SELECT CASE WHEN encoding=1 THEN decompress(content) ELSE content END"
           "  FROM unversioned WHERE name='probe.bin';",row,0);
    fclose(g_out); cl(p); return rc?1:0; }
UVH
    if ! cc -o "$DIR/uv" "$DIR/uv.c" >/dev/null 2>&1; then
      skip S9 "a uv blob cannot be read without forking" "harness did not compile"
    elif ! "$DIR/uv" "$LIB" "$REPO" "$DIR/uvout" >/dev/null 2>&1; then
      skip S9 "a uv blob cannot be read without forking" "harness could not query"
    elif cmp -s "$DIR/uvbin" "$DIR/uvout"; then
      lifted S9 "a uv blob reads back BYTE-IDENTICAL in-process, no fork" \
        "the \`fossil uv export\` fork in viki_cmd_cache_pull_opts(), src/viki_cache.c. THE FIX IS A PATCH TO libfossilsee -- fossilsee_uv_get() wrapping unversioned_content() -- NOT viki-side SQL. Proven: unversioned_content() falls back to a lookup BY HASH (validate16), so \`uv export <hash>\` works and a hand-written WHERE name=? matches zero rows. It needs no output capture, no argv shim and no fossil_main(): it is a read, and fits v0's existing slice."
    else
      held S9 "a uv blob cannot be read back byte-identical without forking"
    fi
  fi
fi

# ---- S10 -------------------------------------------------------------------
# The CLI `fossil finfo` drops deletions: its log-mode query inner-joins blob
# on b.rid=mlink.fid while a deletion has fid==0. We fixed the JSON route in
# fossil-see; the CLI still has it, and the web /finfo page never did.
if [ -z "$FOSSIL" ]; then
  skip S10 "the CLI 'fossil finfo' query omits deletions" "no fossil binary"
else
  ( cd "$CO" && echo a > d.txt && "$FOSSIL" add d.txt >/dev/null 2>&1 \
     && "$FOSSIL" commit -m add-d >/dev/null 2>&1 \
     && "$FOSSIL" rm d.txt >/dev/null 2>&1 \
     && "$FOSSIL" commit -m del-d >/dev/null 2>&1 )
  NCLI=$("$FOSSIL" sql -R "$REPO" --readonly \
    "SELECT count(*) FROM mlink, blob b, event, blob ci, filename
      WHERE filename.name='d.txt' AND mlink.fnid=filename.fnid
        AND b.rid=mlink.fid AND event.objid=mlink.mid AND event.objid=ci.rid;" 2>/dev/null)
  NALL=$("$FOSSIL" sql -R "$REPO" --readonly \
    "SELECT count(*) FROM mlink, event, blob ci, filename
      WHERE filename.name='d.txt' AND mlink.fnid=filename.fnid
        AND event.objid=mlink.mid AND event.objid=ci.rid;" 2>/dev/null)
  if [ "${NALL:-0}" -eq 0 ]; then
    skip S10 "the CLI 'fossil finfo' query omits deletions" "could not build the add/delete fixture"
  elif [ "${NCLI:-0}" -lt "${NALL:-0}" ]; then
    held S10 "the CLI 'fossil finfo' query omits deletions ($NCLI of $NALL rows)"
  else
    lifted S10 "the CLI 'fossil finfo' query now reports deletions" \
      "FINDINGS.md's route table, and the upstream report in fossil-see docs/ -- the CLI half is fixed"
  fi
fi

printf '\n%d limitation(s) still held, %d LIFTED, %d skipped\n' "$nHeld" "$nLifted" "$nSkip"
if [ "$nLifted" -gt 0 ]; then
  printf '\nA LIFTED limitation is not a broken build. It is a substrate that grew a\n'
  printf 'capability viki is still paying to work around. Delete the code named\n'
  printf 'above; do not restore the limitation to make this green.\n'
  exit 1
fi
[ "$nSkip" -eq 0 ] || printf '\n(skips prove nothing -- a limitation not tested is not a limitation confirmed)\n'
exit 0
