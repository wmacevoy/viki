#!/bin/sh
#
# forum-e2e-probe.sh -- end-to-end probe for viki's FORUM indexing path,
# against REAL forum artifacts created by Fossil's own forum_post().
#
# WHY THIS EXISTS
# ---------------
# `viki index` claims four content types: files, wiki pages, tickets and
# forum posts. The first three were round-tripped against a live repo
# early on; forum posts were not, because Fossil 2.28 has no `fossil forum`
# CLI subcommand (`fossil help -a` lists none; src/forum.c's only COMMAND:
# is `test-forumthread`, which merely *displays* a thread). The only code
# path that creates a forum artifact is forum_post(), reachable solely from
# the WEBPAGE: entry points /forume1 (new thread) and /forume2
# (reply/edit). So this script drives those pages.
#
# That matters because forum content is the ONE indexed type that arrives
# as a raw Fossil manifest rather than as rendered text: `fossil wiki
# export` hands back a rendered body, `fossil ticket show` hands back TSV,
# but there is no forum exporter at all, so viki_index.c parses the
# manifest itself. Manifest parsing brings card escaping and artifact
# versioning along with it, and every bug this script asserts against
# lives precisely in that difference. Structural similarity to the
# verified wiki path was NOT evidence -- see FINDINGS.md.
#
# WHAT IT ASSERTS
# ---------------
# The three defects found by doing this for real, all of them invisible
# to any test that only checks `viki index` exit status or a nonzero
# count:
#
#   F8/F9  the H (thread title) card must be un-escaped before indexing.
#          Fossil stores `H Gearbox\sseal\sweeping\sat\sthe\saeromotor`;
#          indexed verbatim, FTS5 tokenizes `\sseal` as one word and every
#          title word after the first becomes unsearchable.
#   F7/F10 an EDITED post leaves its superseded artifact in `event` with
#          type='f' forever. Selecting on event.type alone indexes every
#          historical revision and returns stale text as current.
#   F11    single-line card lookup must stop at the W card's counted body.
#          A reply has no H card, so an unbounded scan runs into the body
#          and adopts any body line starting "H " as the post's title.
#
# USAGE
#   sh build/forum-e2e-probe.sh <empty-work-dir> [path-to-viki]
#
# Exits 0 only if every assertion passes. Against a viki built before the
# viki_index.c forum fix, F7, F8, F10 and F11 fail and F9 fails -- that is
# the point: the assertions are proven able to fail.
#
set -eu

WORK=${1:?usage: forum-e2e-probe.sh <empty-work-dir> [path-to-viki]}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
VIKI=${2:-$REPO_ROOT/build/dist/viki}
FOSSIL=${VIKI_FOSSIL_BIN:-$REPO_ROOT/vendor/fossil-see/build/dist/fossil-see}

# This probe cd's into $WORK, so a RELATIVE path here resolves against the
# wrong directory from that point on. Measured: `build/dist/viki` as $2
# does not fail with "not found" -- the early checks pass, then B4 goes red
# and the run dies mid-way with `unable to open database .../co/.viki/
# cache.db`, which reads like a viki bug and is not one. Fail loudly here
# instead.
case "$VIKI"   in /*) ;; *) echo "ERR: viki path must be ABSOLUTE (got '$VIKI')" >&2; exit 2 ;; esac
case "$FOSSIL" in /*) ;; *) echo "ERR: fossil path must be ABSOLUTE (got '$FOSSIL')" >&2; exit 2 ;; esac
[ -x "$VIKI" ]   || { echo "ERR: no executable viki at '$VIKI'" >&2; exit 2; }
[ -x "$FOSSIL" ] || { echo "ERR: no executable fossil at '$FOSSIL'" >&2; exit 2; }

mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)

# ---------------------------------------------------------------------
# Environment. Four variables carry the whole headless lifecycle.
# ---------------------------------------------------------------------
# FOSSIL_HOME keeps scratch remote URLs and passwords out of the
# developer's real ~/.fossil.
FOSSIL_HOME="$WORK/fossilhome"; mkdir -p "$FOSSIL_HOME"
# The SQLCipher passphrase. An EMPTY value is treated as unset by
# fossil-see, so it must be non-empty.
FOSSIL_SEE_KEY='forum-e2e-probe-key'
# Fossil refuses to write any artifact unless the acting user exists in
# the repo's user table; $USER usually does not. FOSSIL_USER is the one
# lever that covers both fossil itself and viki's subprocesses.
FOSSIL_USER=warren
VIKI_FOSSIL_BIN="$FOSSIL"
VIKI_FOSSIL_USER=warren
export FOSSIL_HOME FOSSIL_SEE_KEY FOSSIL_USER VIKI_FOSSIL_BIN VIKI_FOSSIL_USER

# NEVER set FOSSIL_SEE_STOCK_PROMPT: with stdin open, a keyless open
# blocks forever on a password prompt. Every fossil call below also reads
# from /dev/null as belt and braces.

# Model handling. VIKI_MODEL_DIR unset or "" both fall back to the
# RELATIVE path build/dist/model, so the only deterministic way to force
# degraded (BM25-only) mode is to point it at a path that fails stat().
MODEL=${VIKI_MODEL_DIR:-$REPO_ROOT/build/dist/model}
NOMODEL="$WORK/no-such-model-dir"

LOG="$WORK/logs"; mkdir -p "$LOG"   # logs MUST live outside the indexed tree

PASS=0; FAIL=0
ok(){   PASS=$((PASS+1)); echo "  PASS  $1"; }
bad(){  FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
chk(){  if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }

# ---------------------------------------------------------------------
# Forum posting over `fossil http`: one request read from a file, one
# response written to a file. No port to allocate, no background process
# to reap, no start-up race -- safe under CI and under an agent.
#
# forumnew_page() gates creation on `P("submit") && cgi_csrf_safe(2)`,
# and cgi_csrf_safe() needs ALL THREE of: a Referer that prefix-matches
# g.zBaseURL, REQUEST_METHOD==POST, and a csrf parameter equal to
# g.zCsrfToken. Miss any one and Fossil does not error -- it silently
# re-renders the empty form, which reads exactly like a UI refusing to be
# scripted. The missing Referer is what defeated the first attempt at
# this; see FINDINGS.md. So: never assert on exit status or HTTP 200 here,
# only on the `Location: .../forumpost/<hash>` redirect.
# ---------------------------------------------------------------------
urlenc(){ perl -0777 -pe 's/([^A-Za-z0-9_.~-])/sprintf("%%%02X",ord($1))/ge'; }

http1(){ # $1 = request file -> writes $WORK/resp
  # --localauth grants loopback requests setup capability (which includes
  # WrForum) with no password, so there is no login/cookie dance. It only
  # applies while the repo's own `localauth` setting is off, the default.
  "$FOSSIL" http --localauth --nossl --ipaddr 127.0.0.1 --host 127.0.0.1 \
      --in "$1" --out "$WORK/resp" "$WORK/hub.efossil" </dev/null
}

csrf_for(){ # $1 = path to GET
  printf 'GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n' "$1" > "$WORK/req"
  http1 "$WORK/req"
  sed -n 's/.*name="csrf" value="\([^"]*\)".*/\1/p' "$WORK/resp" | head -1
}

forum_post(){ # $1 = POST path, $2 = extra url-encoded fields, $3 = body file,
              # $4 = path to GET for the token. The GET path is separate on
              # purpose: /forume2 renders nothing (and hands out no csrf
              # token) unless it is told which post it is acting on, so a
              # reply/edit must fetch /forume2?fpid=<hash> even though it
              # POSTs to bare /forume2.
  _tok=$(csrf_for "$4")
  [ -n "$_tok" ] || { echo "no csrf token from $4 -- not authorized?" >&2; exit 1; }
  _body="csrf=$(printf %s "$_tok" | urlenc)&$2&mimetype=text%2Fx-markdown"
  _body="$_body&content=$(urlenc < "$3")&submit=Submit"
  # Content-Length must be the exact byte count; printf '%s' avoids the
  # trailing newline `echo` would append into the last form value.
  _len=$(printf %s "$_body" | wc -c | tr -d ' ')
  { printf 'POST %s HTTP/1.0\r\n' "$1"
    printf 'Host: 127.0.0.1\r\n'
    printf 'Referer: http://127.0.0.1%s\r\n' "$1"   # <-- the load-bearing header
    printf 'Content-Type: application/x-www-form-urlencoded\r\n'
    printf 'Content-Length: %s\r\n\r\n' "$_len"
    printf '%s' "$_body"; } > "$WORK/req"
  http1 "$WORK/req"
  sed -n 's|^Location:.*/forumpost/\([0-9a-f]*\).*|\1|p' "$WORK/resp" | head -1
}

fsql(){ "$FOSSIL" sql --readonly -R "$WORK/hub.efossil" "$1" </dev/null; }

# A viki result line is `[N] rrf=X.XXXX  <content_hash>#<ix>  <source>`
# followed by a snippet that contains RAW newlines, so snippet lines can
# themselves start with '['. Count and rank checks must anchor on the rrf=
# prefix, never on '^\['. (The content_hash field was added to that line
# this round -- it used to read `rrf=X.XXXX  <source>#<ix>` -- so the
# virtual path is now the LAST field, not the third. See C6.)
rank1(){ head -1 "$1"; }
nres(){ grep -cE '^\[[0-9]+\] rrf=' "$1" || true; }

echo "== A. encrypted repo with real forum artifacts =="

"$FOSSIL" init --admin-user warren --project-name viki-forum-probe \
    "$WORK/hub.efossil" </dev/null >/dev/null

# Deliberately NOT `grep -q "SQLite format 3"`: in some shells grep is a
# ugrep wrapper that skips binary files, so that form reports "encrypted"
# hardest when the repo is plaintext. String-compare the first 15 bytes.
is_enc(){ [ "$(head -c 15 "$1")" != "SQLite format 3" ]; }
chk "A1 hub.efossil is really ciphertext" 'is_enc "$WORK/hub.efossil"'
# Negative control: the same check must FAIL on a known-plaintext file,
# otherwise A1 could be passing vacuously.
chk "A2 the encryption check can fail (plaintext control)" '! is_enc "$WORK/fossilhome/.fossil"'

cat > "$WORK/thread.md" <<'EOF'
The gearbox on the north tower has been leaving an oil ring on the
platform every few days. Nothing dramatic yet, but the drip rate is
climbing and the reservoir is down about a quarter inch since spring.

Best guess is the lower shaft packing rather than the case gasket,
since the stain starts under the shaft and not along the split line.
EOF

cat > "$WORK/reply.md" <<'EOF'
Pull the tail vane pin first or the whole assembly will swing while
you are on the ladder. The replacement packing we used last time was
nitrile, not the graphite rope the manual calls for, and it has held
up through two winters without hardening.
EOF

# A reply whose BODY contains a line shaped exactly like a manifest
# single-line card. Nothing exotic -- a water-chemistry readout.
cat > "$WORK/cardish.md" <<'EOF'
Ran the well panel while I was out there. Numbers from the tap:

H 7.9 pH at the wellhead and 8.2 at the stock tank
Fe 0.31 mg/L
Hardness 340 mg/L as calcium carbonate

None of that explains the leak but it is worth having on record.
EOF

# The edited text drops "graphite rope" -- the token that then exists ONLY
# in the superseded artifact, which makes it a perfect probe for whether
# viki is serving history as if it were current.
cat > "$WORK/reply-edited.md" <<'EOF'
Pull the tail vane pin first or the whole assembly will swing while
you are on the ladder. Correction on the packing: we used viton, not
nitrile, and it has held up through two winters without hardening.
EOF

TITLE='Gearbox seal weeping at the aeromotor sightglass'
ROOT=$(forum_post /forume1 "title=$(printf %s "$TITLE" | urlenc)" "$WORK/thread.md" /forume1)
chk "A3 thread-start post created (302 to /forumpost)" '[ -n "$ROOT" ]'
[ -n "$ROOT" ] || { echo "cannot continue without a thread root"; exit 1; }

REPLY=$(forum_post /forume2 "fpid=$ROOT&reply=1" "$WORK/reply.md" "/forume2?fpid=$ROOT")
chk "A4 reply created" '[ -n "$REPLY" ]'
CARDISH=$(forum_post /forume2 "fpid=$ROOT&reply=1" "$WORK/cardish.md" "/forume2?fpid=$ROOT")
chk "A5 card-shaped-body reply created" '[ -n "$CARDISH" ]'
# An edit POSTs edit=1 with the SAME fpid; the Submit button on that form
# is added by client-side JS, but the server only checks that a `submit`
# parameter is present, so sending it unconditionally is enough.
EDITED=$(forum_post /forume2 "fpid=$REPLY&edit=1" "$WORK/reply-edited.md" "/forume2?fpid=$REPLY&edit=1")
chk "A6 edit created (supersedes the first reply)" '[ -n "$EDITED" ]'

# Fossil's own tooling must agree these are genuine forum artifacts --
# the artifact is assembled by forum_post(), run back through
# manifest_parse(), and asserted CFTYPE_FORUM before storing.
"$FOSSIL" timeline --type f -R "$WORK/hub.efossil" -n 10 </dev/null > "$LOG/timeline.txt" 2>&1
chk "A7 fossil timeline --type f shows the thread-start" 'grep -q "Post: $TITLE" "$LOG/timeline.txt"'
chk "A8 fossil timeline --type f shows a reply"          'grep -q "Reply: $TITLE" "$LOG/timeline.txt"'

fsql "SELECT fpid,froot,fprev,firt FROM forumpost;" > "$LOG/forumpost.txt"
chk "A9 exactly one artifact is recorded as superseded" \
    '[ "$(fsql "SELECT count(*) FROM forumpost WHERE fprev IS NOT NULL;")" = 1 ]'
# 4 artifacts exist; only 3 are current. The naive event.type='f' selector
# cannot tell them apart.
chk "A10 event holds 4 forum artifacts but only 3 are current" \
    '[ "$(fsql "SELECT count(*) FROM event WHERE type=char(102);")" = 4 ]'

echo "== B. viki index over the checkout =="

mkdir -p "$WORK/co"
( cd "$WORK/co" && "$FOSSIL" open "$WORK/hub.efossil" </dev/null >/dev/null )

# viki's forum extraction shells out to `fossil sql` with no -R, so it must
# run from inside an open checkout.
( cd "$WORK/co" && VIKI_MODEL_DIR="$MODEL" "$VIKI" index . ) \
    > "$LOG/index.out" 2> "$LOG/index.err" || true

# `viki index` writes its whole summary to STDERR and exits 0 even when it
# indexed nothing, so assert on the counts, never on the exit status.
chk "B1 index stdout is empty (summary goes to stderr)" '[ ! -s "$LOG/index.out" ]'
chk "B2 index reports a NONZERO forum count" \
    '! grep -q "0 forum post(s)" "$LOG/index.err"'
chk "B3 forum posts were chunked, not merely counted" \
    'grep -qE "[1-9][0-9]* forum post\(s\), [1-9][0-9]* \(re\)chunked" "$LOG/index.err"'
# THE bug-2 assertion: 3 current posts, not the 4 artifacts in `event`.
chk "B4 index sees 3 CURRENT posts, not 4 artifacts (superseded excluded)" \
    'grep -q "3 forum post(s)" "$LOG/index.err"'

# What actually landed in the cache, per post.
CACHE="$WORK/co/.viki/cache.db"
stored(){ # $1 = uuid prefix
  sqlite3 "$CACHE" \
    "SELECT chunk_text FROM viki_chunk JOIN viki_source USING(content_hash)
      WHERE path LIKE 'forum:$1%';"
}
stored "$ROOT"    > "$LOG/stored-root.txt"
stored "$CARDISH" > "$LOG/stored-cardish.txt"

# THE bug-1 assertion, at the storage layer: the title must be decoded.
chk "B5 the H title card is stored UN-escaped" \
    'head -1 "$LOG/stored-root.txt" | grep -qx "$TITLE"'
chk "B6 no Fossil card escaping survived into the chunk" \
    '! grep -q "\\\\s" "$LOG/stored-root.txt"'
# THE bug-3 assertion: a reply has no H card, so nothing may be prepended.
chk "B7 card-shaped body line was NOT adopted as a title" \
    'head -1 "$LOG/stored-cardish.txt" | grep -q "^Ran the well panel"'
chk "B8 that body line is still present once, in the body" \
    '[ "$(grep -c "^H 7.9 pH" "$LOG/stored-cardish.txt")" = 1 ]'

echo "== C. viki ask: retrieval and attribution =="

ask(){ # $1 = model dir, $2 = out prefix, $3.. = query
  _md=$1; _out=$2; shift 2
  ( cd "$WORK/co" && VIKI_MODEL_DIR="$_md" "$VIKI" ask "$*" --k 5 ) \
      > "$LOG/$_out.out" 2> "$LOG/$_out.err" || true
}

# BM25-only, so these speak about the FTS leg alone. `aeromotor` appears
# ONLY in the thread title -- nowhere in any body.
ask "$NOMODEL" q_title aeromotor
chk "C1 a title-only word retrieves the thread-start (BM25)" \
    'rank1 "$LOG/q_title.out" | grep -q "forum:$ROOT"'
ask "$NOMODEL" q_escaped saeromotor
chk "C2 the \\s-mangled token is NOT in the index" \
    '[ "$(nres "$LOG/q_escaped.out")" = 0 ]'
ask "$NOMODEL" q_titlewords weeping sightglass
chk "C3 title words after the first are searchable" \
    'rank1 "$LOG/q_titlewords.out" | grep -q "forum:$ROOT"'
# `graphite rope` exists only in the SUPERSEDED artifact; Fossil's own
# /forumthread page renders it zero times.
ask "$NOMODEL" q_stale graphite rope
chk "C4 superseded text is NOT retrievable" \
    '[ "$(nres "$LOG/q_stale.out")" = 0 ]'
ask "$NOMODEL" q_current viton correction packing
chk "C5 the CURRENT edited text IS retrievable" \
    'rank1 "$LOG/q_current.out" | grep -q "forum:$EDITED"'

# Attribution: every result must carry BOTH a citable content_hash#ix and
# the forum:<full-uuid> virtual path. The forum uuid is a PATH, not a
# hash -- it identifies the artifact, whereas the leading 64-hex field is
# sha256 of the post's text -- so this asserts two separate 64-hex fields
# in their two separate positions rather than one.
chk "C6 results are attributed <content_hash>#<ix> then forum:<64-hex-uuid>" \
    'rank1 "$LOG/q_current.out" | grep -qE "^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#[0-9]+  forum:[0-9a-f]{64}$"'

# Two-sided vector proof. This query shares no token (and, since chunk_fts
# uses the `porter unicode61` tokenizer, no STEM either) with the corpus:
# BM25 must return nothing, so any hybrid hit can only come from rung 2.
VQ='synthetic elastomer survived subzero temperatures'
ask "$NOMODEL" q_vec_off "$VQ"
ask "$MODEL"   q_vec_on  "$VQ"
chk "C7 BM25 alone finds nothing for the semantic query" \
    '[ ! -s "$LOG/q_vec_off.out" ] && grep -q "^(no matches)$" "$LOG/q_vec_off.err"'
if grep -q "hybrid mode" "$LOG/q_vec_on.err"; then
  chk "C8 VECTOR LEG: the same query retrieves forum posts with a model" \
      '[ "$(nres "$LOG/q_vec_on.out")" -ge 1 ] && grep -q "forum:" "$LOG/q_vec_on.out"'
else
  echo "  SKIP  C8 (no embedding model at $MODEL -- vector leg not exercised)"
fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
