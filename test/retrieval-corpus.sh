#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# test/retrieval-corpus.sh -- build the retrieval-evaluation corpus.
#
# WHAT THIS IS FOR
# ----------------
# `test/retrieval-eval.py` measures how well `viki ask` recalls things an
# agent would want to remember from an EARLIER session. It needs a corpus
# that is (a) a real agent-episodic record and (b) exercises every kind of
# state a Fossil repository can hold -- which since 2026-08-23 means MORE
# than `viki index` covers, not the reverse. See the table below.
#
# This script builds that corpus, once, into a work directory:
#
#   $WORK/hub.efossil     an ENCRYPTED (SQLCipher) fossil-see repository
#   $WORK/co/             a checkout of it -- THE INDEXED TREE
#   $WORK/co/.viki/cache.db   what `viki index` produced
#   $WORK/logs/           every capture file, DELIBERATELY outside $WORK/co
#
# The documents are this repo's own episodic record (FINDINGS.md, AGENTS.md,
# CLAUDE.md, VIKI_DESIGN.md, KICKOFF.md, USER_STORIES.md, ARCHITECTURE.md,
# ENCRYPTION.md), committed under CHECK-IN COMMENTS ported verbatim from
# viki's own git log. Those comments are rich episodic prose -- they say why
# a thing was done and what was rejected. This comment used to say NOTHING IN
# VIKI INDEXES THEM TODAY and call it the single largest coverage gap. THAT WAS
# NEVER TRUE: `git log -S` resolves this comment and `index_unversioned()` to
# the SAME commit 4292a0a -- one agent wrote this corpus while another closed
# the gap, and the two halves were integrated without reconciling. Check-in
# comments are indexed as `ckin:` and have been since that commit. The eval
# still scores per-artifact recall as its own number, which is the part that
# was always worth doing.
#
# The pairing of a ported message with the files that commit touches is
# arbitrary, and that is fine: viki has no link from a check-in comment to a
# file, so nothing downstream can depend on the pairing. What matters is
# that the comment text is real prose an agent actually wrote about work an
# agent actually did.
#
# EVERY KIND OF FOSSIL STATE, on purpose:
#
#   indexed by viki today        | NOT indexed by viki today
#   -----------------------------+--------------------------------------
#   checkout files (./*.md)      | historical file versions
#   wiki pages   (wiki:Name)     | tags and branch names
#   tickets      (ticket:UUID)   |
#   forum posts  (forum:UUID)    |
#   check-in comments (ckin:)    |
#   tech notes   (note:)         |
#   ticket changes (tchg:)       |
#   attachments  (attach:)       |
#   unversioned  (uv:)           |
#
#   The right column had SEVEN entries until 2026-08-23 and five of them were
#   already indexed. Do not read a large right column as "the measurement" and
#   go add more un-indexed classes -- check `VIRTUAL_NS[]` in src/viki_index.c
#   against this table first. The authoritative count is whatever
#   test/retrieval-eval.sh's per-artifact report prints, not this comment.
#
# USAGE
#   bash test/retrieval-corpus.sh [work-dir]
#
#   work-dir defaults to $VIKI_EVAL_DIR, else /tmp/viki-retrieval-eval.
#   An existing work dir is REUSED as-is unless VIKI_EVAL_REBUILD=1, because
#   building it costs a few hundred ONNX inferences and the eval harness
#   calls this on every run.
#
# ENVIRONMENT (all optional; defaults are this repo's own build outputs)
#   VIKI_BIN         path to the viki binary
#   VIKI_FOSSIL_BIN  path to a SQLCipher-capable fossil (creates *.efossil)
#   VIKI_MODEL_DIR   path to the embedding model directory
#   VIKI_EVAL_DIR    where to build
#   VIKI_EVAL_REBUILD=1   delete and rebuild an existing work dir
#
# Exit status: 0 if the corpus is present and looks complete.
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

VIKI_BIN="${VIKI_BIN:-$REPO_ROOT/build/dist/viki}"
VIKI_FOSSIL_BIN="${VIKI_FOSSIL_BIN:-$REPO_ROOT/vendor/fossil-see/build/dist/fossil-see}"
VIKI_MODEL_DIR="${VIKI_MODEL_DIR:-$REPO_ROOT/build/dist/model}"
WORK="${1:-${VIKI_EVAL_DIR:-/tmp/viki-retrieval-eval}}"

export VIKI_FOSSIL_BIN VIKI_MODEL_DIR
FOSSIL="$VIKI_FOSSIL_BIN"

die(){ printf 'retrieval-corpus.sh: FATAL: %s\n' "$1" >&2; exit 2; }

case "$WORK" in /*) ;; *) WORK="$PWD/$WORK" ;; esac

[ -x "$VIKI_BIN" ]   || die "no viki binary at '$VIKI_BIN' (run build/build.sh, or set VIKI_BIN)"
[ -x "$FOSSIL" ]     || die "no fossil at '$FOSSIL' (build vendor/fossil-see, or set VIKI_FOSSIL_BIN)"
command -v perl >/dev/null 2>&1 || die "perl is required (url-encoding for the forum POSTs)"

# Reuse an existing corpus unless told otherwise. The marker file is written
# LAST, so a half-built tree (killed mid-index) is never mistaken for a good
# one -- an interrupted run rebuilds instead of silently evaluating against a
# corpus missing its forum posts.
STAMP="$WORK/CORPUS-COMPLETE"
if [ -f "$STAMP" ] && [ "${VIKI_EVAL_REBUILD:-0}" != "1" ]; then
    printf 'retrieval-corpus.sh: reusing existing corpus at %s\n' "$WORK"
    printf '  (VIKI_EVAL_REBUILD=1 to rebuild)\n'
    exit 0
fi
rm -rf "$WORK"
mkdir -p "$WORK"

CO="$WORK/co"                 # THE INDEXED TREE. Nothing else may live here.
LOG="$WORK/logs"; mkdir -p "$LOG"
SRC="$WORK/msg";  mkdir -p "$SRC"   # commit-message files, outside $CO

# ------------------------------------------------------------- environment --
# FOSSIL_HOME: keep scratch remote URLs out of the developer's real
# ~/.fossil / ~/.config/fossil.db (FINDINGS.md: build/m1-e2e-probe.sh does
# not do this and permanently dirties the global config db).
export FOSSIL_HOME="$WORK/fossilhome"; mkdir -p "$FOSSIL_HOME"
# The SQLCipher passphrase. An EMPTY value reads as unset to fossil-see.
export FOSSIL_SEE_KEY="viki-retrieval-eval-key-7d3a"
# Never set this: with stdin open a keyless open blocks forever on a prompt.
unset FOSSIL_SEE_STOCK_PROMPT || true
# TWO levers, not one (FINDINGS.md): FOSSIL_USER for subprocesses viki spawns
# bare, VIKI_FOSSIL_USER for `fossil ticket show`, which viki spawns with an
# explicit --user that beats the environment.
TESTUSER="vikieval"
export FOSSIL_USER="$TESTUSER"
export VIKI_FOSSIL_USER="$TESTUSER"

HUB="$WORK/hub.efossil"

printf 'retrieval-corpus.sh: building corpus at %s\n' "$WORK"

# --------------------------------------------------- 1. encrypted repo + co --
"$FOSSIL" init --admin-user "$TESTUSER" --project-name viki-retrieval-eval \
    "$HUB" >"$LOG/init.out" 2>&1 </dev/null

# Deliberately NOT `grep -q "SQLite format 3"` -- FINDINGS.md documents a
# shell where grep is a binary-skipping ugrep shim, which inverts this exact
# check. A 15-byte string compare depends on no tool's binary-file policy.
is_encrypted(){ [ "$(head -c 15 "$1")" != "SQLite format 3" ]; }
is_encrypted "$HUB" || die "hub.efossil is PLAINTEXT -- is '$FOSSIL' really a SQLCipher build?"

# A real CLONE, not `fossil open hub.efossil`: opening the hub in place
# records no remote URL, which breaks `fossil uv sync` after `uv add` has
# already run locally (FINDINGS.md). The uv leg below needs it.
"$FOSSIL" clone --no-open "$HUB" "$WORK/co.efossil" >"$LOG/clone.out" 2>&1 </dev/null
is_encrypted "$WORK/co.efossil" || die "clone destination is PLAINTEXT -- encryption silently defeated"
mkdir -p "$CO"
( cd "$CO" && "$FOSSIL" open "$WORK/co.efossil" >>"$LOG/clone.out" 2>&1 </dev/null )

# commit_with <message-file> [extra fossil commit args...]
commit_with(){
    local msgfile="$1"; shift
    ( cd "$CO" && "$FOSSIL" commit -M "$msgfile" --no-warnings --no-verify-comment \
        "$@" >>"$LOG/commit.out" 2>&1 </dev/null )
}

# ------------------------------------------------- 2. the documents + commits --
# THE DOCUMENTS COME FROM git HEAD, NOT FROM THE WORKING TREE, and that is
# load-bearing rather than fussiness.
#
# The corpus is this repo's own docs, and the eval's own findings get
# written INTO those docs (FINDINGS.md, AGENTS.md, CLAUDE.md). Sourcing
# from the working tree makes every doc edit silently move the corpus and
# therefore the baseline, so writing up a measurement invalidates it --
# and the next agent cannot reproduce a number from a checkout at all.
# Sourcing from HEAD pins the corpus to a commit: a baseline is quotable as
# "at <sha>", it moves only when somebody commits, and `viki index` still
# sees genuine agent-written episodic prose either way.
#
# Falls back to the working tree, loudly, when there is no git (a release
# tarball). The mode is recorded in CORPUS-INFO.txt so a fingerprint
# mismatch is diagnosable rather than mysterious.
DOC_SOURCE="git HEAD"
if ! ( cd "$REPO_ROOT" && git rev-parse HEAD >/dev/null 2>&1 ); then
    DOC_SOURCE="working tree (NO GIT -- baseline is not pinned to a commit)"
    printf 'retrieval-corpus.sh: WARN: no git here; taking docs from the working\n' >&2
    printf '  tree instead of HEAD. Numbers are not comparable to a pinned baseline.\n' >&2
fi

mkdir -p "$CO/notes"

# doc <name> -- materialize one corpus document into the checkout.
doc(){
    if [ "$DOC_SOURCE" = "git HEAD" ]; then
        ( cd "$REPO_ROOT" && git show "HEAD:$1" ) > "$CO/$1" \
            || die "git show HEAD:$1 failed -- is $1 tracked?"
    else
        [ -f "$REPO_ROOT/$1" ] || die "corpus document missing from the repo: $1"
        cp "$REPO_ROOT/$1" "$CO/$1"
    fi
    [ -s "$CO/$1" ] || die "corpus document $1 came out EMPTY"
}

for f in VIKI_DESIGN.md USER_STORIES.md ARCHITECTURE.md ENCRYPTION.md; do doc "$f"; done
cat > "$CO/TIMELINE.md" <<'EOF'
# TIMELINE.md

One line per check-in, so the tree has something that changes on every
commit. The real episodic content of a commit is its CHECK-IN COMMENT,
which lives in the repository and not in any file.

- Initial commit: viki design docs (renamed from fossil-app)
EOF

cat > "$SRC/c01" <<'EOF'
Initial commit: viki design docs (renamed from fossil-app)

The design docs move over first, ahead of any code: USER_STORIES.md carries
decisions D-1..D-12 as settled and not to be relitigated, ARCHITECTURE.md
pins hub-and-spoke with the fossil protocol as the only transport, and
ENCRYPTION.md fixes Fossil at 2.28 because SQLCipher's baseline is SQLite
3.53.1 and 2.29 would require 3.54.

VIKI_DESIGN.md is the one that constrains code most. Rung 2 is universal:
every device and agent runs the same pinned MiniLM-class ONNX model, which
is what makes an embedding a deterministic function of (content_hash,
model_id, chunk_params) and therefore computable ONCE and shared. Vectors
are projections, never source of truth.
EOF
( cd "$CO" && "$FOSSIL" add VIKI_DESIGN.md USER_STORIES.md ARCHITECTURE.md \
    ENCRYPTION.md TIMELINE.md >"$LOG/add.out" 2>&1 </dev/null )
commit_with "$SRC/c01"

# --- c02: KICKOFF.md ---
doc KICKOFF.md
printf -- '- Vendor fossil-see and sqlite-ndvss\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c02" <<'EOF'
Vendor fossil-see and sqlite-ndvss

Both as git submodules, both read-only upstream: vendor, do not edit.
sqlite-ndvss is statically linked with -DSQLITE_CORE and registered through
sqlite3_auto_extension(), never loaded as a runtime .so, and it ships no
public header at all -- so the entry point is extern-declared by hand.

Rejected: forking sqlite-ndvss to fix its build. KICKOFF.md says do not
fork it, and a bug in a vendored project is that project's bug.
EOF
( cd "$CO" && "$FOSSIL" add KICKOFF.md >>"$LOG/add.out" 2>&1 </dev/null )
commit_with "$SRC/c02"

# --- c03: the skeleton ---
printf -- '- Implement Milestone 1 skeleton: viki CLI (index/ask/cache), BM25-only\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c03" <<'EOF'
Implement Milestone 1 skeleton: viki CLI (index/ask/cache), BM25-only

Rung 0 first, on the theory that a memory layer with no vectors at all is
still useful and that the degraded path has to be a first-class one rather
than an afterthought. Chunking is a naive fixed 40 lines with no overlap and
no token awareness -- a documented placeholder, chosen because
VIKI_DESIGN.md deliberately does not pin a chunking strategy and inventing
one before there is anything to measure against is guesswork.

The cache db lives at .viki/cache.db relative to the current directory,
NOT inside the fossil repo db. The repo holds truth; the cache holds
projections, and a projection you cannot delete is not a projection.
EOF
commit_with "$SRC/c03"

# --- c04: FTS5 implicit-AND ---
printf -- '- viki ask: OR-of-terms FTS5 query, not the raw string\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c04" <<'EOF'
viki ask: OR-of-terms FTS5 query, not the raw string

Symptom: asking for a sentence returned zero results while asking for a
single word from the same sentence worked. SQLite FTS5's default MATCH
syntax treats space-separated barewords as an implicit AND -- every term
must be present or the row is excluded outright, not merely scored lower.
A natural-language query contains stopwords and phrasing the matching text
does not literally have, so implicit-AND is the wrong default for a search
box.

build_or_query() now emits an explicit "term1" OR "term2" OR ... with each
term double-quoted as an FTS5 string literal (embedded quotes doubled) so
punctuation cannot be misparsed as NEAR or a column filter. bm25() then
ranks rows matching more and rarer terms higher, which is the
should-match-any shape Lucene and Elasticsearch default to.

Do not "simplify" this back to passing the raw query through.
EOF
commit_with "$SRC/c04"

# --- c05: rung 2 ---
printf -- '- Implement rung-2 ONNX embeddings + hybrid retrieval\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c05" <<'EOF'
Implement rung-2 ONNX embeddings + hybrid retrieval

tokenize -> ONNX Runtime C API Run -> mean-pool over last_hidden_state using
the attention mask -> L2-normalize, then ndvss_cosine_similarity_f() over
the stored blob, fused with BM25 by reciprocal rank (k=60).

Proven by a semantic property, not by "it ran": cosine("six horses were
grazing near the water trough", "a group of horses stood by the watering
hole") = 0.64 against cosine(..., "the quarterly tax filing deadline is in
April") = -0.05. And end to end, "livestock standing by a drinking pool"
ranks the horses document first over a three-document corpus while sharing
no literal word with it.

Model: the all-MiniLM-L6-v2 arm64-quantization-recipe ONNX export, ~23MB,
picked specifically because it could be verified empirically on the one dev
machine this project has. Not yet verified on x86.
EOF
commit_with "$SRC/c05"

# --- c06: wiki + tickets, and the strtok_r bug ---
printf -- '- Index wiki pages and tickets, not just checkout files\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c06" <<'EOF'
Index wiki pages and tickets, not just checkout files

Wiki pages via `fossil wiki export` (rendered text), tickets via `fossil
ticket show 0 --quote` (TSV), both sunk into the same index_text_blob()
that checkout files use, distinguished by the virtual paths wiki:Name and
ticket:UUID.

Found and fixed a real content-corruption bug on the way. Splitting the
ticket TSV with strtok_r looked reasonable and is wrong: strtok_r treats a
run of consecutive delimiters as ONE delimiter and never yields an empty
token between them. A freshly-added ticket has eight consecutive empty
fields (type through resolution), so every column after the gap shifted
left by six -- the comment text came out labelled "Status:" and the title
vanished entirely. split_preserve_empty() walks the string once and records
a field-start pointer every time, including for zero-length fields.

strtok_r is fine for whitespace-separated tokens where empty fields are
meaningless. It is the wrong tool for any fixed-column format where a
field's POSITION carries meaning.

Also: `fossil ticket` refuses to run without a resolvable user even for
read-only queries, while `fossil wiki` does not. Not root-caused in
Fossil's source, just confirmed and worked around.
EOF
commit_with "$SRC/c06"

# --- c07: forum + AGENTS/FINDINGS ---
doc AGENTS.md; doc FINDINGS.md
printf -- '- Index forum posts; extract shared retrieval into viki_ask_query()\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c07" <<'EOF'
Index forum posts; extract shared retrieval into viki_ask_query()

There is no `fossil forum` CLI subcommand at all -- forum.c's only COMMAND:
is test-forumthread, which merely displays a thread -- so forum posts are
extracted with `fossil sql --readonly` against event/blob and the raw
manifest is parsed here. That makes forum the ONE indexed type that arrives
as a raw manifest rather than as rendered text, which brings card escaping
and artifact versioning along with it.

Retrieval moves into a public viki_ask_query() so `viki serve` and the CLI
provably cannot diverge; viki_cmd_ask is now a thin printing wrapper.

Also adds AGENTS.md and FINDINGS.md. AGENTS.md is the current-state
snapshot a brand-new agent orients from; FINDINGS.md records every surprise
with a repro, newest at top.
EOF
( cd "$CO" && "$FOSSIL" add AGENTS.md FINDINGS.md >>"$LOG/add.out" 2>&1 </dev/null )
commit_with "$SRC/c07"

# --- c08: the forum-status note, VERSION 1. Superseded two commits later, so
#          its text survives ONLY as a historical file version. ---
cat > "$CO/notes/forum-status.md" <<'EOF'
# Forum indexing -- status note

Status: index_forum() has never been round-tripped against a live forum
post. Creating one appears to need browser automation. Posting to /forume1
with the scraped csrf token keeps re-rendering the empty New Forum Thread
form instead of confirming creation, which is consistent with a JS-driven
submit flow that static form scraping does not capture.

Estimated cost to close: half a day of browser-driver work, and that is
the reason it is being deferred rather than any doubt about the parser.
Owner: unassigned. Confidence in the forum leg: low.
EOF
printf -- '- Note the forum-verification gap\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c08" <<'EOF'
Note the forum-verification gap

index_forum() claims coverage it has not earned. Writing the gap down in
notes/forum-status.md rather than leaving it implicit, because structural
similarity to the already-verified wiki path is NOT evidence -- wiki content
arrives pre-rendered from `fossil wiki export`, forum content arrives as a
raw manifest, and any bug will live precisely in that difference.
EOF
( cd "$CO" && "$FOSSIL" add notes/forum-status.md >>"$LOG/add.out" 2>&1 </dev/null )
commit_with "$SRC/c08"

# --- c09: viki serve ---
printf -- '- Add viki serve: agent-friendly local HTTP search server\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c09" <<'EOF'
Add viki serve: agent-friendly local HTTP search server

Plain POSIX sockets, single-threaded, one request per connection, no
third-party HTTP library. This is a personal-scale local tool, not a
production web server, so the concurrency and keep-alive corners plain
sockets cut do not matter.

The design decision worth recording is not the server plumbing, it is that
the / search page renders every server-supplied field via DOM textContent
and never innerHTML. Indexed content is not trusted markup: it is free text
from checkout files, wiki pages, tickets and forum posts, any of which can
legitimately contain a literal <script> (a ticket describing a bug IN some
HTML, say). Building the page with innerHTML would be a reflected XSS
through your own index.

Also deliberate: no static-file serving anywhere. Every response is either
one constant HTML string or JSON built from a db query, so there is no
"read a file whose name came from the request" code path and therefore no
path-traversal surface to sanitize.
EOF
commit_with "$SRC/c09"

# --- c10: Windows CI, on a branch, with a tag ---
printf -- '- CI: fix Windows ONNX path for real; mark linux-arm64 experimental\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c10" <<'EOF'
CI: fix Windows ONNX path for real; mark linux-arm64 experimental

The first embed.c fix was a silent no-op. onnxruntime_c_api.h's ORTCHAR_T
typedef, and my guard, both key off #ifdef _WIN32 -- but MSYS2's plain MSYS
environment, used here specifically because it (unlike MINGW64) provides
fork()/pipe()/BSD sockets, does not define _WIN32, the same way Cygwin does
not, since both aim to present a POSIX-like environment. So the guard
evaluated false, windows.h was never included, and CreateSession got the
same narrow char* as before -- byte-for-byte the same mojibake in the CI log
as the first failure.

Fixed properly: build.sh defines -DVIKI_WIN_ORT_PATH explicitly when linking
against the Windows onnxruntime build, independent of whatever _WIN32 or
__MSYS__ state this particular compiler happens to have.

Second layer, only visible once the first was fixed: `pwd` under MSYS2
returns an MSYS-style path (/d/a/viki/viki/...) which only an MSYS-runtime
binary can interpret. viki.exe's own file I/O works because msys-2.0.dll
translates it transparently; the native onnxruntime.dll calls raw Win32 file
APIs with whatever bytes it is handed, with no MSYS awareness at all. Fixed
at the source by running SCRIPT_DIR and REPO_ROOT through `cygpath -m`.

Also: linux-arm64's first CI run failed in vendor/sqlite-ndvss, not in
viki's own code. Marked experimental rather than spending more time in a
different project's build.
EOF
commit_with "$SRC/c10" --branch ci-hardening \
    --tag windows-green --tag onnx-path-fix

# --- c11: forum-status.md REWRITTEN. The v1 text above now exists only in
#          history -- an un-indexed artifact by construction. ---
cat > "$CO/notes/forum-status.md" <<'EOF'
# Forum indexing -- status note

Status: CLOSED. Live forum posts round-trip through viki index and viki ask.

The blocker was never browser automation. forumnew_page() gates creation on
P("submit") && cgi_csrf_safe(2), and cgi_csrf_safe() wants all three of a
Referer that prefix-matches the base URL, REQUEST_METHOD POST, and a csrf
parameter. Miss any one and Fossil does not error -- it silently re-renders
the empty form, which reads exactly like a UI refusing to be scripted. The
missing header was Referer.

Doing it for real exposed three parser bugs, all now fixed. Four artifact
shapes were exercised: thread-start, reply, card-shaped reply, and edit.
Post deletion, moderation-pending posts, and edit chains longer than one hop
remain untested.
EOF
# mtime-only staleness detection (FINDINGS.md): a file rewritten within the
# same second as its last index is invisible to `viki index`. Nothing has
# indexed this tree yet, but backdate anyway so the rule is never subtly
# depended on.
touch -t 202001010000 "$CO/notes/forum-status.md"
printf -- '- Forum posts round-trip for real; three parser bugs fixed\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c11" <<'EOF'
Forum posts round-trip for real; three parser bugs fixed

FINDINGS.md recorded that creating a post needed Fossil's "AJAX-driven web
UI"; that was wrong -- /forume1 is an ordinary POST form and the blocker was
a missing Referer: header. Real posts round-trip now, and doing it exposed
three real bugs:

  1. Editing a post appends a new artifact and leaves the superseded one in
     `event` with type='f' forever, so every historical revision was indexed
     and served as current. Fixed via NOT IN (SELECT fprev FROM forumpost).
  2. find_line_card() searched the whole manifest unbounded, so a reply --
     which carries no H card -- adopted any body line starting "H " as its
     title. Reproduced against a live reply whose water-chemistry text
     became its title.
  3. The H card is escaped (\s for space) and was never run through
     unquote_fossil(), so FTS5 tokenized `\sseal` as one word and every
     title word after the first was unsearchable.

Two live posts were not enough to exercise the path; four were.
EOF
commit_with "$SRC/c11"

# --- c12: CLAUDE.md + the invalidation and mixed-epoch fixes ---
doc CLAUDE.md
printf -- '- Close Milestone 1: DoD test, invalidation, mixed-epoch scoring\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c12" <<'EOF'
Close Milestone 1: DoD test, invalidation, mixed-epoch scoring

test/m1.sh is KICKOFF.md's owed `make test`: 90 assertions proving the
definition-of-done chain end to end on a scratch ENCRYPTED repo. It grew
from 54 assertions to 90 when the fixes below landed. It is non-vacuous: a
binary built from pre-fix sources fails 36 of the 90.

`viki index` never invalidated anything: a deleted file's text stayed at
rank 1 under its original path. AGENTS.md called this harmless
accumulation; it was a retrieval correctness bug. Now sweeps dead sources
and GCs unreferenced chunks from viki_chunk AND chunk_fts, scoped so that
indexing a subdirectory cannot delete anything outside it and so that a
machine with no fossil binary cannot delete every wiki page, ticket and
forum post -- empty extractor output otherwise looks identical to "this
repo has none".

Mixed-epoch caches double-counted the BM25 leg (rrf 0.0489 =
1/61+1/62+1/61), reordering 9 of 12 queries' top-K on a 15-doc corpus. Each
leg now scores a chunk at most once, at its best rank. Deliberately NOT
fixed by filtering FTS on model_id: a no-model peer has no model_id to
filter by and must still search a cache a model-having peer built, so that
would trade a scoring bug for a silent recall bug.

Also adds CLAUDE.md: how to work in this tree, as opposed to AGENTS.md's
current-state snapshot and KICKOFF.md's milestone brief.
EOF
( cd "$CO" && "$FOSSIL" add CLAUDE.md >>"$LOG/add.out" 2>&1 </dev/null )
commit_with "$SRC/c12"

# --- c13: the release-automation commit. Its message carries a rejected
#          alternative that appears in NO document anywhere in this corpus. ---
printf -- '- Add release.yml: automate future v* tag releases\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c13" <<'EOF'
Add release.yml: automate future v* tag releases

v0.1.0 itself was published by hand -- downloading build.yml's CI-verified
artifacts and repackaging them with `gh release create` -- since this
workflow did not exist yet at tag time. This automates that same recipe for
v0.2.0 and on.

Not yet triggered by an actual tag push; YAML-syntax-checked only.
Deliberately NOT testing it against the real v0.1.0 tag right now:
softprops/action-gh-release would very likely update, not duplicate, that
already-published release, risking overwriting its hand-written notes with
auto-generated ones. Verify with the next real version bump instead.

Known gap, not fixed here: release.yml does not depend on the m1 job, so a
v* tag can publish artifacts for a commit whose Milestone 1 test never ran.
EOF
commit_with "$SRC/c13"

# --- c14: the CI container fix, and the ignore rule ---
printf -- '- CI: git safe.directory in containers\n' >> "$CO/TIMELINE.md"
cat > "$SRC/c14" <<'EOF'
CI: git safe.directory in containers; ignore the connect helper

First CI run: macos-arm64 passed outright. linux-x86_64 and linux-arm64
both failed at submodule init with "detected dubious ownership" -- container
jobs bind-mount the checkout from the host, and actions/checkout's own
safe.directory fix only covers its own internal git calls, not later steps
in the same job. Fixed by setting safe.directory '*' before this workflow's
own submodule-fetch step.

Separately, `connect` is now ignored. It is a one-line local convenience
wrapper carrying a specific hostname and root login -- not something other
checkouts want.
EOF
commit_with "$SRC/c14"

# ------------------------------------------------------------ 3. wiki pages --
# Body from a FILE argument: the non-interactive form. Without it fossil
# opens $EDITOR.
cat > "$WORK/wiki-chunking.txt" <<'EOF'
Chunking is fixed 40-line splits with no overlap and no token awareness,
and no awareness of the model's 512-token limit for very long lines.
That is known-naive BY CHOICE, not by oversight.

Why it has not been "fixed": an embedding is a deterministic function of
(content_hash, model_id, chunk_params) SHARED between peers, per D-11.
Changing the chunk size therefore changes what EVERY peer computes for the
same bytes, which is an epoch bump against viki-manifest -- a rare,
fleet-wide, deliberately scheduled event -- and not a free local tweak.
Anyone proposing a better chunker has to say the words "epoch bump" out
loud and mean them.

The rejected shortcut was to add overlap "since it only affects new
content". It does not only affect new content: two peers running different
chunk parameters write different chunk_ix rows for identical bytes into the
same latest-wins cache blob, and neither one is wrong.
EOF
( cd "$CO" && "$FOSSIL" wiki create ChunkingIsDeliberatelyNaive \
    "$WORK/wiki-chunking.txt" -M text/x-markdown >>"$LOG/plant.out" 2>&1 </dev/null )

cat > "$WORK/wiki-epoch.txt" <<'EOF'
What an epoch bump costs, so nobody proposes one casually.

The storage side of two-epoch coexistence already exists: the primary key
is (content_hash, model_id, chunk_ix) and the vector leg filters WHERE
model_id = the asker's. Two models' vectors can therefore sit in one cache
without colliding or being mixed at query time.

What is MISSING is the migration driver. Nothing detects that the pin
moved, nothing re-embeds existing content under the new model_id, and
nothing garbage-collects the old epoch's vectors afterwards. Today a pin
bump means new content is embedded under the new id while old content stays
queryable only under the old one.

That is deliberately post-milestone, not laziness. One universal pinned
model is what makes an epoch bump rare, and the working fallback is to
delete the cache and re-index, which costs minutes at personal scale.
Building migration machinery before a second model exists means designing
against an imaginary model_id.
EOF
( cd "$CO" && "$FOSSIL" wiki create EpochBumpChecklist \
    "$WORK/wiki-epoch.txt" -M text/x-markdown >>"$LOG/plant.out" 2>&1 </dev/null )

# --------------------------------------------------------------- 4. tickets --
# `fossil ticket add` is fully non-interactive as FIELD VALUE pairs. Its
# failure mode is a silent "0 ticket(s)", which is why the eval's ticket
# queries exist at all.
( cd "$CO" && "$FOSSIL" ticket add \
    title "viki serve's HTML page shows no content_hash" \
    comment "The CLI hit line and /api/ask's hash field both carry the citable content_hash, but the embedded search page renders only rank, rrf, source and chunk_ix. So the one HUMAN surface cannot cite a source. The hash is already in the JSON the page fetches; it is simply not displayed. Nothing in test/m1.sh covers viki serve at all." \
    >>"$LOG/plant.out" 2>&1 </dev/null )

( cd "$CO" && "$FOSSIL" ticket add \
    title "release.yml does not depend on the m1 job" \
    comment "A v* tag can publish release artifacts for a commit on which the Milestone 1 definition-of-done test never ran. The m1 job exists and gates main, but nothing gates a release on it." \
    >>"$LOG/plant.out" 2>&1 </dev/null )

( cd "$CO" && "$FOSSIL" ticket add \
    title "linux-arm64 build fails inside vendor/sqlite-ndvss" \
    comment "sqlite3_ndvss_init picks a SIMD backend by #if defined(__aarch64__) && defined(__linux__) and then does a RUNTIME check on HWCAP2_SVE2, but the sve2 function bodies are only compiled in when the COMPILE-TIME macro __ARM_FEATURE_SVE is defined, which viki's build.sh never passes. The dispatch line therefore references cosine_similarity_f_sve2, which does not exist in that translation unit." \
    >>"$LOG/plant.out" 2>&1 </dev/null )

# A fourth ticket exists ONLY to be changed, and the change REPLACES its
# comment rather than appending to it.
#
# The distinction is load-bearing and was measured, not assumed. `fossil
# ticket change UUID +comment TEXT` APPENDS to the ticket's own `comment`
# field, so `fossil ticket show 0` reports it and `viki index` picks it up
# -- an appended comment is NOT a coverage gap. A change that REPLACES a
# field writes the new value into `ticket` and the per-change record into
# `ticketchng`, which nothing in viki reads. So the previous value of a
# replaced field is exactly the un-indexed thing, and "what did this ticket
# say before it was closed" is exactly the episodic question about it.
( cd "$CO" && "$FOSSIL" ticket add \
    title "Add overlap between chunks" \
    comment "First pass at a fix: add a ten-line overlap window between adjacent chunks and re-index everything. Should be a couple of hours of work and it will stop answers being cut in half at a boundary." \
    >>"$LOG/plant.out" 2>&1 </dev/null )

tkt_uuid_matching(){ # $1 = extended regex to match against the title column
    ( cd "$CO" && "$FOSSIL" ticket show 0 --quote --user "$TESTUSER" </dev/null 2>/dev/null ) \
    | awk -F'\t' -v pat="$1" \
        'NR==1{for(i=1;i<=NF;i++){if($i=="tkt_uuid")u=i;if($i=="title")t=i}next}
         u && t && $t ~ pat {print $u; exit}'
}

OVERLAP_UUID="$(tkt_uuid_matching 'overlap')"
if [ -n "${OVERLAP_UUID:-}" ]; then
    ( cd "$CO" && "$FOSSIL" ticket change "$OVERLAP_UUID" \
        status "Closed" \
        resolution "Wont_Fix" \
        comment "Closed as wont-fix. Chunk parameters are part of the epoch pin, so changing them is a fleet-wide epoch bump and not a local tweak; see the EpochBumpChecklist wiki page." \
        >>"$LOG/plant.out" 2>&1 </dev/null ) || true
else
    printf 'retrieval-corpus.sh: WARN: could not resolve the overlap ticket uuid; ticket-change history not planted\n' >&2
fi

# The arm64 ticket gets an APPENDING change, as the control: this text stays
# retrievable, which is what proves the gap above is about replacement and
# not about `ticket change` in general.
ARM_UUID="$(tkt_uuid_matching 'linux-arm64')"
if [ -n "${ARM_UUID:-}" ]; then
    ( cd "$CO" && "$FOSSIL" ticket change "$ARM_UUID" \
        status "Deferred" \
        resolution "Wont_Fix" \
        +comment "Deferred rather than fixed: vendor/sqlite-ndvss is a separate repo and a bug in a vendored project is that project's bug, so the CI leg is marked experimental instead." \
        >>"$LOG/plant.out" 2>&1 </dev/null ) || true
fi

# ------------------------------------------------------------- 5. tech note --
# event.type='e'. `fossil wiki list` does not list technotes and
# index_wiki() only walks that list, so a technote is invisible to viki.
cat > "$WORK/technote.txt" <<'EOF'
Retrieval-quality baseline session, 2026-08-16.

Recorded here as a tech note rather than as a wiki page because it is dated
work, not a standing document -- which is exactly what a tech note is for
and exactly the artifact type nothing in viki reads.

Findings from the session, in the order they mattered:

The keyword leg is not filtered by model_id and must never be. A chunk may
exist only under model_id='none', written by a peer with no model at all,
and an asker with no model has no model_id to filter by in the first place
yet still has to search a cache that a model-having peer built.

Reciprocal rank fusion weights each leg's best hit identically, so a single
stopword match from BM25 is worth exactly as much as a correct semantic
match from the vector leg. That is what RRF is, not a bug, but it makes
small corpora fragile: one standalone "a" in an unrelated document flips
the top hit.

The rank-1 score is a free diagnostic because the fusion constant is 60.
A score of 0.0164 = 1/61 is ONE leg at rank 1, so a supposedly semantic-only
query really did get nothing from BM25. A score of 0.0328 = 1/61 + 1/62 is
both legs contributing.
EOF
( cd "$CO" && "$FOSSIL" wiki create \
    "Retrieval baseline session notes" "$WORK/technote.txt" \
    --technote "2026-08-16 09:00:00" --technote-tags "retrieval eval" \
    -M text/x-markdown >>"$LOG/plant.out" 2>&1 </dev/null ) || \
    printf 'retrieval-corpus.sh: WARN: technote creation failed (see logs)\n' >&2

# ------------------------------------------------------------ 6. attachment --
# Attached to a wiki page. Attachment CONTENT is a separate artifact that
# index_wiki() never sees -- `fossil wiki export` returns the page body only.
cat > "$WORK/attach-bench.txt" <<'EOF'
Chunk-size bench, raw numbers, attached rather than pasted inline.

  chunk lines | chunks | index wall | recall@5 (12 hand queries)
  ------------+--------+------------+---------------------------
      20      |   214  |   9.8 s    |  0.58
      40      |   107  |   5.1 s    |  0.50
      80      |    54  |   3.4 s    |  0.33

Smaller chunks won on recall and lost on wall time, which is the trade
everyone expects. The number nobody expected: at 80 lines the ANSWER SPAN
was split across a chunk boundary in 4 of the 12 queries, and in 3 of those
4 the half carrying the question's vocabulary was not the half carrying the
answer.
EOF
# `fossil attachment add` takes PAGENAME FILENAME and nothing else here --
# there is no -m/--comment option, and passing one is a usage error that
# exits nonzero with the attachment never created.
( cd "$CO" && "$FOSSIL" attachment add ChunkingIsDeliberatelyNaive \
    "$WORK/attach-bench.txt" \
    >>"$LOG/plant.out" 2>&1 </dev/null ) || \
    printf 'retrieval-corpus.sh: WARN: attachment add failed (see logs)\n' >&2

# --------------------------------------------------- 7. unversioned content --
# uv blobs are latest-wins and carry no history. `viki index` DOES read them
# (index_unversioned(), as `uv:NAME`), skipping viki's own viki-cache.db /
# viki-model/* names. This comment claimed the opposite until 2026-08-23.
cat > "$WORK/uv-runbook.txt" <<'EOF'
Hub runbook, unversioned because it changes faster than the repo does.

To rotate the SQLCipher passphrase you cannot simply re-clone into a fresh
key over a local path. A file:// clone opens source and destination on one
connection under one FOSSIL_SEE_KEY, and the destination inherits the
source's PBKDF2 salt -- verified by comparing the first 16 bytes of five
repos in one clone family. Only an http:// clone gets a fresh salt, so key
rotation is a NETWORK-transport operation and nothing in the test tree
exercises it.

Worse, a cross-mode clone exits 1 AFTER writing a complete, readable,
PLAINTEXT repository under the .efossil name. The suffix is only an
instruction to apply a key on open; it is not a property of the file.
EOF
( cd "$CO" && "$FOSSIL" unversioned add "$WORK/uv-runbook.txt" \
    --as hub-runbook.txt >>"$LOG/plant.out" 2>&1 </dev/null ) || \
    printf 'retrieval-corpus.sh: WARN: uv add failed (see logs)\n' >&2

# Wiki/ticket/forum writes do not autosync the way a commit does.
( cd "$CO" && "$FOSSIL" sync >>"$LOG/plant.out" 2>&1 </dev/null ) || true

# ----------------------------------------------------------- 8. forum posts --
# Fossil ships no `fossil forum` CLI, so posts are created by driving
# /forume1 and /forume2 through `fossil http` -- one request read from a
# file, one response written to a file. No port, no background process.
#
# forumnew_page() gates creation on P("submit") && cgi_csrf_safe(2), which
# needs ALL THREE of a Referer prefix-matching the base URL, REQUEST_METHOD
# POST, and a matching csrf parameter. Miss one and Fossil does NOT error --
# it re-renders the empty form. That missing Referer is what defeated the
# first attempt at this; see FINDINGS.md. So never assert on exit status or
# HTTP 200 here, only on the Location: .../forumpost/<hash> redirect.
urlenc(){ perl -0777 -pe 's/([^A-Za-z0-9_.~-])/sprintf("%%%02X",ord($1))/ge'; }

http1(){ # $1 = request file -> $WORK/resp
  "$FOSSIL" http --localauth --nossl --ipaddr 127.0.0.1 --host 127.0.0.1 \
      --in "$1" --out "$WORK/resp" "$WORK/co.efossil" </dev/null
}
csrf_for(){ # $1 = path to GET
  printf 'GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n' "$1" > "$WORK/req"
  http1 "$WORK/req"
  sed -n 's/.*name="csrf" value="\([^"]*\)".*/\1/p' "$WORK/resp" | head -1
}
forum_post(){ # $1 POST path, $2 extra encoded fields, $3 body file, $4 GET path for token
  local tok body len
  tok="$(csrf_for "$4")"
  [ -n "$tok" ] || { printf 'retrieval-corpus.sh: WARN: no csrf token from %s\n' "$4" >&2; return 1; }
  body="csrf=$(printf %s "$tok" | urlenc)&$2&mimetype=text%2Fx-markdown"
  body="$body&content=$(urlenc < "$3")&submit=Submit"
  len="$(printf %s "$body" | wc -c | tr -d ' ')"
  { printf 'POST %s HTTP/1.0\r\n' "$1"
    printf 'Host: 127.0.0.1\r\n'
    printf 'Referer: http://127.0.0.1%s\r\n' "$1"   # <-- the load-bearing header
    printf 'Content-Type: application/x-www-form-urlencoded\r\n'
    printf 'Content-Length: %s\r\n\r\n' "$len"
    printf '%s' "$body"; } > "$WORK/req"
  http1 "$WORK/req"
  sed -n 's|^Location:.*/forumpost/\([0-9a-f]*\).*|\1|p' "$WORK/resp" | head -1
}

cat > "$WORK/forum-thread.txt" <<'EOF'
Two peers pushed to the same cache blob and every score in `viki ask` moved,
without a single document or query changing. Worth writing down before
anyone else chases it.

The db held two model_id epochs of identical content. On a fifteen-document
corpus, 9 of 12 queries came back in a different ORDER, several with a
different SET of documents in the top 8. One document climbed from rank 8 to
rank 4 and two fell out of the top 8 altogether, and the scores at those
boundaries are not tied, so this is real reordering rather than tie-break
noise.

An earlier note called this harmless because the cache is derived and
rebuildable. That judgement was made on a three-document corpus where every
score happened to move the same way, and it is wrong.
EOF
FORUM1="$(forum_post /forume1 "title=$(printf %s 'Mixed-epoch caches silently reorder the top-K' | urlenc)" \
          "$WORK/forum-thread.txt" /forume1 || true)"

cat > "$WORK/forum-reply.txt" <<'EOF'
Root cause and, more usefully, the fix that was REJECTED.

chunk_fts holds one row per (content_hash, model_id, chunk_ix) and the
candidate pool keys on (content_hash, chunk_ix) only, so two epochs of the
same content put two identical rows in front of BM25. They merged into one
candidate and collected the keyword leg's 1/(k+rank) contribution twice,
while the vector leg, correctly filtered by model_id, contributed once. The
fusion silently reweights itself toward keywords in proportion to how many
peers have pushed.

The obvious fix is to add the asker's model_id to the keyword query. Do NOT
do that. A chunk may exist only under model_id='none', written by a peer
that had no model, and an asker with no model has no model_id to filter by
at all yet must still search a cache a model-having peer built -- which is
the entire point of pulling one. Filtering would drop exactly the rows the
pull was for and would convert a scoring bug into a silent recall bug.

BM25 does not depend on model_id in the first place: the only indexed column
is chunk_text, byte-identical across epochs, so the duplicate rows carry no
information and collapsing them loses nothing.
EOF
if [ -n "${FORUM1:-}" ]; then
    FORUM2="$(forum_post /forume2 "fpid=$FORUM1&reply=1" "$WORK/forum-reply.txt" \
              "/forume2?fpid=$FORUM1" || true)"
else
    FORUM2=""
    printf 'retrieval-corpus.sh: WARN: thread-start post failed; no reply planted\n' >&2
fi

# An EDIT. The superseded artifact stays in `event` with type='f' forever,
# so this is also the standing check that index_forum()'s fprev exclusion
# still works -- the pre-edit wording below must NOT be retrievable.
cat > "$WORK/forum-edit.txt" <<'EOF'
Fixed, and here is what "fixed" means precisely.

A leg_hit() helper now records a per-leg bit on each candidate, so every
retrieval leg scores a given (content_hash, chunk_ix) at most once, at its
best rank -- and since both legs feed rows best-first, the first row a leg
reports for a chunk IS that chunk's best rank. Ranks count distinct chunks
rather than rows, so the fused scores are IDENTICAL to a single-epoch
cache's rather than merely deduplicated.

Verified by comparing ranked lists from 1-, 2-, 3- and 5-epoch caches: byte
identical, in both hybrid and degraded mode, and identical to what the
pre-fix binary produced on a single-epoch cache, so single-epoch behaviour
did not move. All 12 queries that previously disagreed now agree.

One residual, written down rather than hidden: the BM25 leg over-fetches
four rows per candidate slot and stops at poolSize DISTINCT chunks, so a
cache holding more than four epochs of one chunk hands the fusion fewer
candidates than it asked for. Scores stay correct; deep-tail recall shrinks.
Nothing tests that -- nothing in the tree builds more than two epochs.
EOF
if [ -n "${FORUM2:-}" ]; then
    forum_post /forume2 "fpid=$FORUM2&edit=1" "$WORK/forum-edit.txt" \
        "/forume2?fpid=$FORUM2" >/dev/null || \
        printf 'retrieval-corpus.sh: WARN: forum edit failed\n' >&2
fi

( cd "$CO" && "$FOSSIL" sync >>"$LOG/plant.out" 2>&1 </dev/null ) || true

# ---------------------------------------------------------------- 9. index --
# Backdate every corpus file. `viki index` skips a file whose (path, mtime)
# matches its viki_source row and never re-hashes it, so a rebuild that
# reuses paths inside one second would silently index nothing (FINDINGS.md).
find "$CO" -type f -not -path "$CO/.viki/*" -exec touch -t 202001010000 {} + 2>/dev/null || true

printf 'retrieval-corpus.sh: indexing...\n'
( cd "$CO" && "$VIKI_BIN" index . ) >"$LOG/index.out" 2>&1 || true
sed 's/^/  /' "$LOG/index.out"

# The counts are the assertion. Every one of these can be zero while `viki
# index` exits 0 (FINDINGS.md: a nonexistent dir, an unresolvable fossil
# user, and a missing fossil binary all produce plausible zeros and rc=0).
grep -qE '[1-9][0-9]* file\(s\) scanned'   "$LOG/index.out" || die "no files indexed -- see $LOG/index.out"
grep -qE '[1-9][0-9]* wiki page\(s\)'      "$LOG/index.out" || die "no wiki pages indexed -- see $LOG/index.out"
grep -qE '[1-9][0-9]* ticket\(s\)'         "$LOG/index.out" || die "no tickets indexed -- see $LOG/index.out"
grep -qE '[1-9][0-9]* forum post\(s\)'     "$LOG/index.out" || die "no forum posts indexed -- see $LOG/index.out"
[ -f "$CO/.viki/cache.db" ] || die "no cache db at $CO/.viki/cache.db"

# ---------------------------------------------------- 10. provenance record --
# Written so a later agent can tell at a glance whether a stored baseline
# was measured against the same corpus they are looking at.
{
  printf 'corpus built:      %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf 'viki binary:       %s\n' "$VIKI_BIN"
  printf 'viki build mtime:  %s\n' "$(ls -l "$VIKI_BIN" | awk '{print $6, $7, $8}')"
  printf 'fossil:            %s\n' "$("$FOSSIL" version 2>/dev/null | head -1)"
  printf 'model dir:         %s\n' "$VIKI_MODEL_DIR"
  printf 'doc source:        %s\n' "$DOC_SOURCE"
  printf 'source repo HEAD:  %s\n' "$( cd "$REPO_ROOT" && git rev-parse HEAD 2>/dev/null || echo '(not a git repo)')"
  printf '\n--- viki index ---\n'
  cat "$LOG/index.out"
  printf '\n--- fossil timeline (check-in comments live HERE, not in any file) ---\n'
  ( cd "$CO" && "$FOSSIL" timeline -n 40 -v </dev/null 2>&1 ) || true
} > "$WORK/CORPUS-INFO.txt"

touch "$STAMP"
printf 'retrieval-corpus.sh: corpus ready at %s\n' "$WORK"
printf '  indexed tree: %s\n' "$CO"
printf '  cache db:     %s\n' "$CO/.viki/cache.db"
printf '  provenance:   %s\n' "$WORK/CORPUS-INFO.txt"
