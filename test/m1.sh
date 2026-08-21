#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# test/m1.sh -- the Milestone 1 definition-of-done test (KICKOFF.md).
#
# KICKOFF.md's "Definition of done for milestone 1" asks for a test that
# proves, end to end, on a scratch ENCRYPTED repo:
#
#     init -> add sample docs + wiki -> `viki index` -> `viki ask` returns the
#     planted answer BOTH with and without the model present -> `viki cache
#     push` -> fresh clone + `viki cache pull` -> `viki ask` works WITHOUT
#     re-indexing (the compute-once claim, D-11).
#
# This script is that test. It is deliberately self-contained in the
# aesthetic of build/build.sh: no Makefile, no test framework, no state
# outside one mktemp -d scratch tree, and nothing assumed to be on PATH.
#
# THE POINT OF THIS FILE IS THAT IT CANNOT PASS VACUOUSLY. Almost every
# `viki` failure mode in this codebase is *silent* -- `viki index` on a
# nonexistent directory exits 0, `viki ask` with no hits exits 0, an
# unresolvable Fossil user makes ticket indexing report a plausible
# "0 ticket(s)" and exit 0, and `viki ask`'s "hybrid mode" banner is printed
# whenever a model FILE loaded, whether or not a single vector was compared
# (all four written up in FINDINGS.md). So:
#
#   * assertions grep observed output, never bare exit status, wherever a
#     command can "succeed" at doing nothing;
#   * every positive claim is paired with a control that must come out the
#     OTHER way -- the encryption check is run against a deliberately
#     plaintext repo, the semantic query is run in both modes, the D-11
#     witness is queried in the fresh clone BEFORE the pull as well as
#     after, and each "X is retrieved" has a query for which X must be
#     absent.
#
# Usage:
#     test/m1.sh                       # run everything
#     VIKI_TEST_KEEP=1 test/m1.sh      # keep the scratch tree for post-mortem
#
# Environment (all optional, defaults are this repo's own build outputs):
#     VIKI_BIN         path to the viki binary
#     VIKI_FOSSIL_BIN  path to a fossil-compatible binary (needs SQLCipher
#                      support: this test creates *.efossil repos)
#     VIKI_MODEL_DIR   path to the embedding model directory
#
# Exit status: 0 only if every assertion passed. Skipped assertions (model
# genuinely absent, no stock `sqlite3` available) do not fail the run, but
# they are printed loudly and counted in the summary -- a run with skips is
# NOT a full definition-of-done pass and says so.
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

VIKI_BIN="${VIKI_BIN:-$REPO_ROOT/build/dist/viki}"
VIKI_FOSSIL_BIN="${VIKI_FOSSIL_BIN:-$REPO_ROOT/vendor/fossil-see/build/dist/fossil-see}"
VIKI_MODEL_DIR="${VIKI_MODEL_DIR:-$REPO_ROOT/build/dist/model}"
export VIKI_FOSSIL_BIN
FOSSIL="$VIKI_FOSSIL_BIN"

# WHY a hard-coded absolute grep: FINDINGS.md ("`grep -q \"SQLite format 3\"`
# is not a safe encryption check in an agent's shell") documents an
# environment where `grep` is a shell function wrapping ugrep with -I
# (skip-binary), which inverts the result of the single most load-bearing
# assertion in this file. A script is not supposed to inherit shell
# functions, but BASH_ENV and exported functions can leak them in, and this
# costs nothing to rule out.
GREP=grep
for g in /usr/bin/grep /bin/grep; do
    if [ -x "$g" ]; then GREP="$g"; break; fi
done

# ---------------------------------------------------------------- scoreboard --
N_PASS=0
N_FAIL=0
N_SKIP=0

pass_(){ N_PASS=$((N_PASS+1)); printf '  PASS  %s\n' "$1"; }
fail_(){ N_FAIL=$((N_FAIL+1)); printf '  FAIL  %s\n' "$1"; }
skip_(){ N_SKIP=$((N_SKIP+1)); printf '  SKIP  %s\n          reason: %s\n' "$1" "$2"; }

# check <label> <shell expression> [file to dump on failure]
#
# The expression is single-quoted at the call site and expanded here by
# eval, so $LOG/$WORK/... resolve at assertion time without a thicket of
# backslash escapes. Running it as the condition of `if` also suspends
# errexit for its duration, which is exactly what we want: a failing
# assertion must be recorded and reported, not abort the run.
check(){
    local label="$1" expr="$2" dump="${3:-}"
    if eval "$expr"; then
        pass_ "$label"
    else
        fail_ "$label"
        printf '          expr: %s\n' "$expr"
        if [ -n "$dump" ] && [ -f "$dump" ]; then
            printf '          --- %s (first 15 lines) ---\n' "$dump"
            sed -n '1,15p' "$dump" | sed 's/^/          | /'
        fi
    fi
}

section(){ printf '\n== %s ==\n' "$1"; }

# CONVENTION -- the trailing `|| true` on every `viki index`/`viki ask`
# invocation below is deliberate, not sloppiness. `set -e` is right for the
# SETUP steps (a repo that would not clone is a broken run, not a test
# result), but it is wrong for the steps that merely produce output for an
# assertion to judge: an early exit there aborts the run before the
# scoreboard prints, hiding every later assertion behind one line of shell
# noise. Measured while writing this file: injecting a no-op `viki cache
# push` killed the run at the first read of the never-pulled cache db, and
# the operator saw four FAIL lines and no summary at all. So the commands
# whose failure IS a result never decide control flow -- a check() does.

# --------------------------------------------------------------- preflight --
section "0. preflight"

die(){ printf 'test/m1.sh: FATAL: %s\n' "$1" >&2; exit 2; }

[ -x "$VIKI_BIN" ]   || die "no viki binary at '$VIKI_BIN' (run build/build.sh, or set VIKI_BIN)"
[ -x "$FOSSIL" ]     || die "no fossil binary at '$FOSSIL' (build vendor/fossil-see, or set VIKI_FOSSIL_BIN)"

# The model is the one dependency this test is allowed to do without: CI
# runs on platforms where build.sh's pinned model may not be materialized,
# and VIKI_DESIGN.md makes BM25-only a REQUIRED path, not a failure. But
# "absent" must mean absent -- if the model directory is there and merely
# broken, that is a real failure and the hybrid assertions below will say
# so rather than being skipped.
HAVE_MODEL=0
MODEL_ID=""
if [ -d "$VIKI_MODEL_DIR" ] \
   && [ -f "$VIKI_MODEL_DIR/model.onnx" ] \
   && [ -f "$VIKI_MODEL_DIR/vocab.txt" ] \
   && [ -f "$VIKI_MODEL_DIR/viki-manifest.json" ]; then
    HAVE_MODEL=1
    # Read model_id out of the manifest rather than hard-coding it, so a
    # build/versions.env model bump doesn't silently turn every hybrid
    # assertion into a string mismatch.
    MODEL_ID="$(sed -n 's/.*"model_id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
                  "$VIKI_MODEL_DIR/viki-manifest.json" | head -1)"
    [ -n "$MODEL_ID" ] || die "model at '$VIKI_MODEL_DIR' has a viki-manifest.json with no model_id"
    printf '  model present: %s (%s)\n' "$MODEL_ID" "$VIKI_MODEL_DIR"
else
    printf '\n'
    printf '  ###############################################################\n'
    printf '  ## NO EMBEDDING MODEL at %s\n' "$VIKI_MODEL_DIR"
    printf '  ## Rung-2 (vector) assertions will be SKIPPED, including the\n'
    printf '  ## semantic-only retrieval proof -- the ONLY assertion that\n'
    printf '  ## shows the vector leg is real. This run therefore does NOT\n'
    printf '  ## satisfy Milestone 1 on its own; it only proves the\n'
    printf '  ## BM25-only degraded path and the D-11 cache loop.\n'
    printf '  ## Run build/build.sh (it downloads + verifies the pinned\n'
    printf '  ## model) or set VIKI_MODEL_DIR to get the full run.\n'
    printf '  ###############################################################\n'
    printf '\n'
fi

# Stock sqlite3 is used only for corroborating assertions (the SQLCipher
# "this file is not a database" proof, and the embedding blob shape). Its
# absence weakens the run but does not invalidate it.
HAVE_SQLITE3=0
command -v sqlite3 >/dev/null 2>&1 && HAVE_SQLITE3=1
[ "$HAVE_SQLITE3" = 1 ] || printf '  note: no stock `sqlite3` on PATH -- corroborating db assertions will be skipped\n'

# PRESENCE IS NOT CAPABILITY, and assuming it was cost six commits of a red
# `m1 (macos-arm64)` nobody chased. Three assertions (H11, H11b, J1) query
# `chunk_fts`, a PLAIN FTS5 virtual table -- a `sqlite3` built without the
# fts5 module cannot even PREPARE that statement. On the GitHub macOS
# runner it cannot: `Error: in prepare, no such module: fts5`. Because the
# only gate was `command -v sqlite3`, those three ran anyway, the failed
# call substituted an EMPTY STRING, and the comparison failed with
# `[: : integer expression expected` -- which reads as "viki did not
# withdraw the chunks", the exact opposite of the truth. H8/H9/H10 assert
# the same withdrawal through `viki ask` and passed in the same run.
#
# So probe what is actually needed. Note this is a stricter gate than
# HAVE_SQLITE3 and deliberately separate from it: E3/E3b/B2/C11 only need
# sqlite3 to OPEN a database and must not be skipped over a missing module
# they never use.
HAVE_FTS5=0
if [ "$HAVE_SQLITE3" = 1 ] \
   && sqlite3 :memory: "CREATE VIRTUAL TABLE t USING fts5(x);" >/dev/null 2>&1; then
    HAVE_FTS5=1
fi
[ "$HAVE_SQLITE3" != 1 ] || [ "$HAVE_FTS5" = 1 ] || \
    printf '  note: `sqlite3` on PATH has no fts5 module -- chunk_fts assertions will be skipped\n'

# An INDEPENDENT sha256, used to prove that the content_hash `viki ask`
# prints is the real sha256 of the source bytes and not merely something
# 64 hex characters long. Deliberately not viki's own sha256.c: a hash
# checked against the implementation that produced it proves nothing.
# `shasum` is stock on macOS, `sha256sum` on GNU/Linux; neither is
# guaranteed, so its absence is a skip like sqlite3's.
SHA256=""
if command -v shasum >/dev/null 2>&1; then
    SHA256="shasum -a 256"
elif command -v sha256sum >/dev/null 2>&1; then
    SHA256="sha256sum"
fi
[ -n "$SHA256" ] || printf '  note: no `shasum`/`sha256sum` on PATH -- the independent content_hash check will be skipped\n'

# hash_of <file>  -- bare 64-hex sha256, no filename column.
hash_of(){ $SHA256 "$1" | cut -d' ' -f1; }

# hit_hash <ask-output-file> [rank]  -- the content_hash from a result
# header line. Field 3 is `<hash>#<chunk_ix>`; splitting on '#' is safe
# because neither field can contain one. Reads the header line by its
# `^[N] rrf=` anchor rather than by line number: snippet lines carry raw
# newlines from the source text (see the RANK1 comment).
hit_hash(){
    $GREP -E "^\[${2:-1}\] rrf=" "$1" | head -1 | awk '{print $3}' | cut -d'#' -f1
}

# ----------------------------------------------------------- scratch setup --
WORK="$(mktemp -d "${TMPDIR:-/tmp}/viki-m1.XXXXXXXX")"
SUMMARY_PRINTED=0
cleanup(){
    local rc=$?
    # Setup failures (a repo that would not init, a checkout that would not
    # open) abort under errexit before any scoreboard exists. Say so out
    # loud: a bare nonzero exit with no RESULT line is indistinguishable
    # from a crash, and this test's whole purpose is to not be ambiguous.
    if [ "$rc" -ne 0 ] && [ "$SUMMARY_PRINTED" -eq 0 ]; then
        printf '\n--------------------------------------------------------------\n'
        printf 'test/m1.sh: ABORTED (exit %d) before reaching the summary.\n' "$rc"
        printf 'This is a SETUP failure, not an assertion failure -- the run\n'
        printf 'never got far enough to judge Milestone 1. Scoreboard so far:\n'
        printf '  %d passed, %d failed, %d skipped\n' "$N_PASS" "$N_FAIL" "$N_SKIP"
        printf 'Re-run with VIKI_TEST_KEEP=1 to inspect %s\n' "$WORK"
    fi
    if [ "${VIKI_TEST_KEEP:-0}" = "1" ]; then
        printf '\nVIKI_TEST_KEEP=1 -- scratch tree left at %s\n' "$WORK"
    else
        case "$WORK" in
            /*) rm -rf "$WORK" ;;   # paranoia: never rm -rf a relative path
        esac
    fi
    exit $rc
}
trap cleanup EXIT

# All log/capture files live OUTSIDE every indexed tree. FINDINGS.md trap
# #5: `walk()` has no exclusion for a test's own output, and `viki ask`
# captures contain result lines naming the very documents being searched
# for -- so a capture file inside the corpus plants near-duplicate decoys
# and shifts every ranking assertion.
LOG="$WORK/logs"
mkdir -p "$LOG"

# WHY FOSSIL_HOME: without it, every scratch repo's remote URL (and any
# saved password) is written into the developer's REAL ~/.fossil global
# config db. This test must leave no trace outside $WORK.
export FOSSIL_HOME="$WORK/fossilhome"
mkdir -p "$FOSSIL_HOME"

# The SQLCipher passphrase. fossil-see reads it from the environment; the
# *.efossil filename suffix is what triggers encryption at all (ENCRYPTION.md).
export FOSSIL_SEE_KEY="viki-m1-test-key-2a7f91c4"

# WHY NOT FOSSIL_SEE_STOCK_PROMPT: with it set and stdin open, any keyless
# open blocks forever on a password prompt (measured at rc=124 under
# `timeout`). Every fossil call below also gets </dev/null as belt and
# braces, so a prompt EOFs instead of hanging a CI job.
unset FOSSIL_SEE_STOCK_PROMPT || true

# Fossil validates a user name against the REPO's user table, so the
# ambient $USER (whatever the developer/CI runner is called) resolves to
# nothing in a repo created with --admin-user. TWO separate levers are
# needed and both are set here:
#   FOSSIL_USER       -- read by every fossil subprocess, including the
#                        `fossil uv add` that `viki cache push` spawns with
#                        no --user argument at all.
#   VIKI_FOSSIL_USER  -- read by viki_fossil_user(), which passes an
#                        EXPLICIT `--user` to `fossil ticket show`. An
#                        explicit argument beats the environment, so
#                        FOSSIL_USER cannot rescue this one.
# MEASURED, not assumed -- FINDINGS.md's older claim that "the single lever
# that fixes all of it is FOSSIL_USER" does not hold for the ticket leg.
# Deleting the VIKI_FOSSIL_USER line below and re-running this script gives
# `50 passed, 4 failed`: A6, C2 and C14 go red with a silent "0 ticket(s)",
# and B2 follows because the ticket's chunk never exists. That is also the
# standing proof that A6 is not a vacuous assertion.
TESTUSER="vikitest"
export FOSSIL_USER="$TESTUSER"
export VIKI_FOSSIL_USER="$TESTUSER"

# WHY an explicitly nonexistent directory rather than `unset VIKI_MODEL_DIR`
# or VIKI_MODEL_DIR="": open_embedder_if_available() (viki.c) falls back to
# the RELATIVE path "build/dist/model" when the variable is unset OR empty,
# so both silently re-enable rung 2 if the cwd happens to contain that
# path. Only a stat()-failing path forces degraded mode. FINDINGS.md.
NOMODEL="$WORK/no-such-model-directory"

# The two model directories the D-12 model round trip needs. Both live
# OUTSIDE every indexed tree, for the same reason the logs do: a 23 MB
# model.onnx dropped inside a corpus would be walked by `viki index`.
#   PULLED_MODEL -- where `viki cache pull` in the fresh clone is told to
#                   materialize the model. Must not exist beforehand;
#                   that is assertion M3's whole point.
#   EMPTYMODEL   -- an existing but EMPTY directory. Distinct from
#                   NOMODEL on purpose: NOMODEL fails stat(), so it only
#                   exercises viki.c's directory check, whereas this one
#                   forces viki_embedder_open() itself to fail. It is the
#                   control that says the hybrid answers below came from
#                   the PULLED FILES and not from a "the path exists, so
#                   assume a model" shortcut anywhere in the chain.
PULLED_MODEL="$WORK/pulled-model"
EMPTYMODEL="$WORK/empty-model-directory"
mkdir -p "$EMPTYMODEL"

HUB="$WORK/hub.efossil"

# ------------------------------------------------ 1. encrypted scratch repo --
section "1. encrypted scratch repo"

"$FOSSIL" init --admin-user "$TESTUSER" --project-name viki-m1 "$HUB" >"$LOG/init.out" 2>&1 </dev/null

# The load-bearing encryption assertion. Deliberately NOT
# `grep -q "SQLite format 3"` -- see FINDINGS.md: in a shell where grep is
# a binary-skipping ugrep shim, that form reports "encrypted" for a
# plaintext repo, i.e. it passes hardest exactly when the property is
# false. A 15-byte string compare depends on no tool's binary-file policy.
is_encrypted(){ [ "$(head -c 15 "$1")" != "SQLite format 3" ]; }

check "E1 hub repo is really ciphertext, not just named .efossil" \
      'is_encrypted "$HUB"'

# NEGATIVE CONTROL for E1. The .efossil suffix is only an instruction to
# apply a key; it is not a property of the file, and FINDINGS.md documents
# a clone path that leaves a readable PLAINTEXT repository under an
# .efossil name. So prove the check can still FAIL: same binary, same key,
# same command, only the suffix differs.
"$FOSSIL" init --admin-user "$TESTUSER" "$WORK/plaintext-control.fossil" >>"$LOG/init.out" 2>&1 </dev/null
check "E2 CONTROL: same binary+key writes plaintext to a .fossil name (E1 can fail)" \
      '! is_encrypted "$WORK/plaintext-control.fossil"'

if [ "$HAVE_SQLITE3" = 1 ]; then
    # Corroboration from a tool with no SQLCipher support at all: it must
    # reject the file outright (SQLITE_NOTADB), while happily reading the
    # plaintext control.
    check "E3 stock sqlite3 cannot open the encrypted repo" \
          '! sqlite3 "$HUB" "select count(*) from sqlite_master;" >/dev/null 2>&1'
    check "E3b CONTROL: stock sqlite3 CAN open the plaintext control repo" \
          'sqlite3 "$WORK/plaintext-control.fossil" "select count(*) from sqlite_master;" >/dev/null 2>&1'
else
    skip_ "E3 stock sqlite3 cannot open the encrypted repo" "no sqlite3 on PATH"
    skip_ "E3b CONTROL: stock sqlite3 CAN open the plaintext control repo" "no sqlite3 on PATH"
fi

# The key must actually be load-bearing, and must fail FAST and
# non-interactively -- a hung CI job is a worse outcome than a red one.
# `timeout` is not on every platform (macOS without coreutils), so only
# wrap when it exists.
TIMEOUT=""
command -v timeout >/dev/null 2>&1 && TIMEOUT="timeout 30"
check "E4 a wrong FOSSIL_SEE_KEY is rejected (non-interactively)" \
      '! env FOSSIL_SEE_KEY=definitely-not-the-key $TIMEOUT "$FOSSIL" timeline -n 1 -R "$HUB" >"$LOG/wrongkey.out" 2>&1 </dev/null' \
      "$LOG/wrongkey.out"

# ------------------------------------------------------ 2. plant the corpus --
section "2. plant sample docs + wiki + ticket"

# fclone <name> -- clone the hub into $WORK/<name>.efossil and open it at
# $WORK/<name>. Every checkout in this test is a real CLONE, never
# `fossil open hub.efossil` directly: `fossil uv sync` (what `viki cache
# push/pull` calls) needs a recorded remote URL, and opening the hub in
# place records none -- which fails AFTER `uv add` has already run
# locally, i.e. a half-completed push. FINDINGS.md.
fclone(){
    local name="$1"
    "$FOSSIL" clone --no-open "$HUB" "$WORK/$name.efossil" \
        >"$LOG/clone.$name.out" 2>&1 </dev/null
    # Re-assert encryption on every clone destination. FINDINGS.md: a
    # cross-mode clone exits nonzero but still leaves a complete,
    # READABLE, plaintext repository behind under the .efossil name. A
    # script that trusts the exit code alone can end up testing an
    # unencrypted repo that is named as if it were encrypted.
    is_encrypted "$WORK/$name.efossil" \
        || die "clone destination $name.efossil is PLAINTEXT -- encryption silently defeated"
    mkdir -p "$WORK/$name"
    ( cd "$WORK/$name" && "$FOSSIL" open "$WORK/$name.efossil" >>"$LOG/clone.$name.out" 2>&1 </dev/null )
}

fclone work

# The corpus is three documents, not one, so that "the planted answer was
# retrieved" is a real discrimination rather than a tautology -- with a
# single document every query returns it.
#
# grocery.md deliberately says "one bag" and not "a bag". FINDINGS.md:
# chunk_fts uses `porter unicode61` with NO stopword list, and
# build_or_query() ORs every query term, so a single standalone "a"
# anywhere in the corpus is a full-weight BM25 hit. AGENTS.md's original
# horses/water-trough example was one such stopword away from asserting
# nothing.
mkdir -p "$WORK/work/docs"
cat > "$WORK/work/docs/barn.md" <<'EOF'
# Barn notes

Six horses were grazing near the water trough behind the north barn.
The trough was refilled on Tuesday morning and the pump ran without
complaint. Bring the halters in from the fence before the rain.
EOF
cat > "$WORK/work/docs/grocery.md" <<'EOF'
# Grocery list

milk, eggs, sourdough bread, two lemons, coffee beans, olive oil,
one bag of frozen peas, and dish soap.
EOF
cat > "$WORK/work/docs/tax.md" <<'EOF'
# Tax note

The quarterly estimated tax filing deadline is in April. Keep the
receipts for the office deduction in the blue folder.
EOF

( cd "$WORK/work" \
  && "$FOSSIL" add docs >"$LOG/plant.out" 2>&1 </dev/null \
  && "$FOSSIL" commit -m "plant the m1 test corpus" --no-warnings >>"$LOG/plant.out" 2>&1 </dev/null )

# Wiki page: KICKOFF.md names "add sample docs + wiki" explicitly. Body
# comes from a FILE argument, which is the non-interactive form -- without
# it fossil opens $EDITOR.
printf 'The north pasture windmill pumps water into the stock tank.\nRamona services the gearbox every spring.\n' \
    > "$WORK/wikibody.txt"
( cd "$WORK/work" && "$FOSSIL" wiki create PumpMaintenance "$WORK/wikibody.txt" \
    >>"$LOG/plant.out" 2>&1 </dev/null )

# Ticket: not named by KICKOFF.md, but `viki index` has a ticket leg and
# recon confirmed `ticket add` is fully non-interactive as FIELD VALUE
# pairs -- so it is cheap to cover, and its failure mode (a silent
# "0 ticket(s)") is exactly the kind this test exists to catch.
( cd "$WORK/work" && "$FOSSIL" ticket add \
    title "Gearbox rattles on the north pump" \
    comment "Intermittent metallic knock under load." \
    >>"$LOG/plant.out" 2>&1 </dev/null )

# DELIBERATELY NOT PLANTED: a forum post -- and this is a scope decision,
# not an oversight. KICKOFF.md's definition of done names "sample docs +
# wiki"; the ticket above is a cheap extra because `fossil ticket add` is a
# one-line non-interactive command. A forum post is not: Fossil ships no
# `fossil forum` CLI at all, so creating one means POSTing to `/forume1`
# through `fossil http` with a scraped CSRF token AND a `Referer:` header
# (whose absence silently re-renders the empty form instead of erroring --
# FINDINGS.md), plus a URL-encoder. That belongs in a focused probe, and
# one exists: `build/forum-e2e-probe.sh`, which is where the three
# `index_forum()` parsing bugs were found and where their fixes are
# asserted. This script therefore indexes a corpus with no forum content,
# `viki index` reports "0 forum post(s)", and nothing here claims anything
# about forum indexing either way.
#
# Wiki/ticket writes do not necessarily autosync the way a commit does;
# push them to the hub explicitly so the later clones see them.
( cd "$WORK/work" && "$FOSSIL" sync >>"$LOG/plant.out" 2>&1 </dev/null )

check "S1 the commit reached the encrypted hub" \
      '"$FOSSIL" timeline -n 5 -R "$HUB" </dev/null 2>&1 | $GREP -q "plant the m1 test corpus"' \
      "$LOG/plant.out"
check "S2 the wiki page reached the encrypted hub" \
      '"$FOSSIL" wiki list -R "$HUB" </dev/null 2>&1 | $GREP -qx "PumpMaintenance"' \
      "$LOG/plant.out"
check "S3 the ticket exists in the repo" \
      '"$FOSSIL" ticket show 0 -R "$HUB" --user "$TESTUSER" </dev/null 2>&1 | $GREP -q "Gearbox rattles"' \
      "$LOG/plant.out"

# The D-11 witness. This file is created in the work checkout and
# DELIBERATELY NEVER COMMITTED, so it exists in exactly one place other
# than this disk: the embedding cache computed from it. A fresh clone
# cannot re-derive it from the repo by any means. If the fresh clone can
# answer a question about it after `viki cache pull` alone, the answer
# provably travelled through the uv cache -- that is the whole
# compute-once claim, and it is not provable with a committed file.
cat > "$WORK/work/docs/uncommitted-witness.md" <<'EOF'
# Witness document

The zeppelin mooring mast at Cardington was repainted in ochre.
EOF
check "S4 CONTROL: the witness doc is genuinely uncommitted (fossil sees it as extra)" \
      'cd "$WORK/work" && "$FOSSIL" extras </dev/null 2>&1 | $GREP -q "uncommitted-witness.md"'

# --------------------------------------------------------- shared queries --
# Q_KEYWORD    -- every term appears literally in barn.md. Works in BOTH
#                 modes; this is "viki ask returns the planted answer".
# Q_KEYWORD_CTL-- every term appears literally in tax.md and NONE appear in
#                 barn.md, so barn.md must be absent from the results. This
#                 is what stops the keyword assertions being vacuous.
# Q_SEMANTIC   -- THE vector-leg proof. Not one of these tokens appears
#                 anywhere in the corpus (docs, wiki page, or ticket), and
#                 porter stemming does not bridge any of them, so the BM25
#                 leg is provably empty. Run in both modes it must give
#                 opposite answers, and that difference can only come from
#                 rung 2. FINDINGS.md: the "hybrid mode" stderr banner
#                 proves a model FILE loaded, NOT that any vector was
#                 compared, so this behavioural two-sided test is the only
#                 sound proof available.
# Q_SEMANTIC_CTL-- also zero literal overlap, but semantically about the
#                 tax note. Its job is to show the vector leg
#                 DISCRIMINATES: a vector search always returns its top-K,
#                 so "barn.md came back" is only meaningful if some other
#                 semantic query does not return barn.md first.
Q_KEYWORD="horses water trough"
Q_KEYWORD_CTL="quarterly estimated deduction receipts"
Q_SEMANTIC="equine hydration paddock"
Q_SEMANTIC_CTL="levy paperwork owed annually"
# Witness queries, for the fresh clone after `viki cache pull`.
# Q_WITNESS         -- literal terms, so it works in either mode.
# Q_WITNESS_SEMANTIC-- zero literal overlap again ("airship"/"docking"/
#                      "tower" appear nowhere, and porter stemming does not
#                      bridge them to "zeppelin"/"mooring"/"mast"). Measured
#                      rrf for the top hit is 0.0164 = 1/61 exactly -- a
#                      SINGLE leg contributing at rank 1, i.e. the BM25 leg
#                      really did return nothing. Deliberately not something
#                      like "repaint", which porter-stems onto the witness's
#                      "repainted" and turns the vector proof into a keyword
#                      match (measured: 0.0328, both legs).
Q_WITNESS="Cardington zeppelin mooring mast"
Q_WITNESS_SEMANTIC="airship docking tower"

# Stale-content queries, for section 7. Each is a set of literal terms
# that occurs in EXACTLY ONE place in the whole corpus, so an empty result
# set is an unambiguous statement that that one place is gone -- there is
# nothing else for BM25 to match, in any document, wiki page or ticket.
# Q_OLD_BARN -- only in barn.md's ORIGINAL wording ("halters"/"fence"/
#               "rain"). Checked against grocery.md (milk/eggs/bread/
#               lemons/coffee/oil/peas/soap), tax.md, the wiki page
#               (windmill/stock tank/gearbox/spring) and the ticket
#               (gearbox/rattles/pump/knock).
# Q_NEW_BARN -- only in the REPLACEMENT wording written in section 7,
#               and absent from the original, so it cannot answer before
#               the rewrite is indexed.
# Q_GROCERY  -- only in grocery.md, which section 7 never touches. This is
#               the "the sweep did not eat the cache" control.
Q_OLD_BARN="halters fence rain"
Q_NEW_BARN="cider press gaskets hydraulic"
Q_GROCERY="sourdough lemons frozen peas"

# A rank-1 result line. viki_cmd_ask() (src/viki_ask.c) prints
#   printf("[%d] rrf=%.4f  %s#%d  %s\n    %s\n\n",
#          rank, rrf, content_hash, chunk_ix, source, snippet)
# -- note EXACTLY TWO spaces between every pair of fields. The snippet on
# the following line carries RAW newlines from the source text and FTS5's
# snippet() highlight markers are square brackets, so unindented snippet
# lines can themselves start with '['. That is why every rank check below
# anchors on `head -1` and the full `^\[N\] rrf=` prefix, and every count
# uses `grep -cE '^\[[0-9]+\] rrf='` rather than counting lines starting
# with '['. FINDINGS.md measured 4 vs 3 on exactly that mistake.
#
# The content_hash was ADDED to this line (it used to read
# `rrf=<score>  <source>#<ix>`), so RANK1 now carries the hash and the
# chunk_ix, and each call site's tail is just the source with a `$`
# anchor. Baking `[0-9a-f]{64}#0` into RANK1 rather than a lax `.*`
# keeps two properties every one of these assertions used to have: the
# hit is chunk 0 of its document, and the hash field is a real 64-char
# lowercase sha256 rather than the "(source path unknown)" placeholder
# that the pre-fix line printed when the viki_source join missed. G1-G3
# below prove the value is the REAL content hash, not merely hex-shaped.
RANK1='^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  '
ANYRANK='^\[[0-9]+\] rrf='
# The hash field on its own, for assertions that only care that a hit is
# citable (any chunk index).
HASHFIELD='^\[[0-9]+\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#[0-9]+  '

# -------------------------------------------- 3. degraded mode (BM25 only) --
section "3. degraded mode -- no model present (VIKI_DESIGN.md rung 0)"

# WHY its own clone rather than re-indexing the work checkout: chunk_fts is
# not filtered by model_id and find_or_add() keys candidates on
# (content_hash, chunk_ix) only, so a cache holding two model epochs merges
# one document into one candidate and adds the BM25 leg's RRF contribution
# TWICE -- every score shifts for reasons unrelated to the code under test.
# Each mode gets a virgin .viki/cache.db. FINDINGS.md.
fclone bm25
D="$WORK/bm25"

( cd "$D" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
    >"$LOG/a.index.out" 2>"$LOG/a.index.err" </dev/null ) || true

check "A1 index prints nothing on stdout (the whole summary is one stderr line)" \
      '[ ! -s "$LOG/a.index.out" ]' "$LOG/a.index.out"
check "A2 index is honest about running without a model" \
      '$GREP -q "viki index: no embedding model found" "$LOG/a.index.err"' "$LOG/a.index.err"
# WHY the (re)chunked count and not the scanned count: index_file()
# increments nFiles BEFORE the binary-content check, so the checkout's own
# .fslckout is "scanned" and then dropped. Only the chunked count is a
# deterministic property of the corpus.
check "A3 index chunked all 3 planted documents" \
      '$GREP -qE "^viki index: [0-9]+ file\(s\) scanned, 3 \(re\)chunked" "$LOG/a.index.err"' "$LOG/a.index.err"
check "A4 index reports model_id=none" \
      '$GREP -q "(model_id=none)\$" "$LOG/a.index.err"' "$LOG/a.index.err"
check "A5 the wiki page was indexed" \
      '$GREP -q "1 wiki page(s), 1 (re)chunked" "$LOG/a.index.err"' "$LOG/a.index.err"
# NONZERO, not merely rc=0. An unresolvable fossil user makes this leg
# report "0 ticket(s), 0 (re)chunked" and exit 0 -- indistinguishable from
# a repo that genuinely has no tickets. FINDINGS.md.
check "A6 the ticket was indexed (NONZERO -- a silent 0 is the known failure)" \
      '$GREP -q "1 ticket(s), 1 (re)chunked" "$LOG/a.index.err"' "$LOG/a.index.err"

( cd "$D" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD" \
    >"$LOG/a.kw.out" 2>"$LOG/a.kw.err" </dev/null ) || true
check "A7 ask announces degraded mode" \
      '$GREP -q "viki ask: degraded mode (BM25 keyword search only" "$LOG/a.kw.err"' "$LOG/a.kw.err"
check "A8 ask does NOT claim hybrid mode without a model" \
      '! $GREP -q "hybrid mode" "$LOG/a.kw.err"' "$LOG/a.kw.err"
check "A9 PLANTED ANSWER (no model): keyword query ranks barn.md first" \
      'head -1 "$LOG/a.kw.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/a.kw.out"

( cd "$D" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD_CTL" \
    >"$LOG/a.kwctl.out" 2>"$LOG/a.kwctl.err" </dev/null ) || true
check "A10 CONTROL: an unrelated keyword query ranks tax.md first" \
      'head -1 "$LOG/a.kwctl.out" | $GREP -qE "${RANK1}\./docs/tax\.md\$"' "$LOG/a.kwctl.out"
check "A11 CONTROL: that query does NOT return barn.md at all" \
      '! $GREP -q "docs/barn\.md" "$LOG/a.kwctl.out"' "$LOG/a.kwctl.out"

# Half one of the two-sided vector-leg proof. Every token is absent from
# the corpus, so with no model there is nothing for BM25 to match and the
# result set must be EMPTY. `viki ask` exits 0 when it finds nothing, so
# this asserts on the streams, not on status.
( cd "$D" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_SEMANTIC" \
    >"$LOG/a.sem.out" 2>"$LOG/a.sem.err" </dev/null ) || true
check "A12 VECTOR PROOF (half 1): semantic query finds NOTHING without a model" \
      '[ ! -s "$LOG/a.sem.out" ]' "$LOG/a.sem.out"
check "A13 ... and says so on stderr" \
      '$GREP -qx "(no matches)" "$LOG/a.sem.err"' "$LOG/a.sem.err"

( cd "$D" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
    >"$LOG/a.index2.out" 2>"$LOG/a.index2.err" </dev/null ) || true
check "A14 re-indexing an unchanged tree re-chunks nothing (incremental)" \
      '$GREP -qE "^viki index: [0-9]+ file\(s\) scanned, 0 \(re\)chunked" "$LOG/a.index2.err"' "$LOG/a.index2.err"

# ------------------------------------------------------- 4. hybrid mode --
section "4. hybrid mode -- model present (VIKI_DESIGN.md rung 0 + rung 2)"

if [ "$HAVE_MODEL" = 0 ]; then
    for t in \
        "B1 index records the pinned model_id" \
        "B2 every chunk carries a real embedding blob" \
        "B3 ask announces hybrid mode naming the model_id" \
        "B4 PLANTED ANSWER (with model): keyword query ranks barn.md first" \
        "B5 VECTOR PROOF (half 2): semantic-only query retrieves barn.md" \
        "B6 CONTROL: a different semantic-only query does NOT rank barn.md first" \
        "B7 --k caps the number of results"
    do
        skip_ "$t" "no embedding model at $VIKI_MODEL_DIR"
    done
else
    fclone vec
    H="$WORK/vec"

    ( cd "$H" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" index . \
        >"$LOG/b.index.out" 2>"$LOG/b.index.err" </dev/null ) || true
    check "B1 index records the pinned model_id" \
          '$GREP -q "(model_id=$MODEL_ID)\$" "$LOG/b.index.err"' "$LOG/b.index.err"

    if [ "$HAVE_SQLITE3" = 1 ]; then
        # Corroborates that indexing really embedded, rather than writing
        # NULLs: the model dim comes from the manifest, and a float32
        # vector is 4 bytes per dimension. Anything short/NULL means the
        # ONNX leg quietly did nothing.
        MODEL_DIM="$(sed -n 's/.*"dim"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' \
                      "$VIKI_MODEL_DIR/viki-manifest.json" | head -1)"
        EXPECT_BYTES=$(( MODEL_DIM * 4 ))
        check "B2 every chunk carries a real embedding blob ($MODEL_DIM floats)" \
              '[ "$(sqlite3 "$H/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE model_id='"'"'$MODEL_ID'"'"' AND (embedding IS NULL OR length(embedding) <> $EXPECT_BYTES);")" = "0" ] && [ "$(sqlite3 "$H/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE model_id='"'"'$MODEL_ID'"'"';")" -ge 5 ]'
    else
        skip_ "B2 every chunk carries a real embedding blob" "no sqlite3 on PATH"
    fi

    ( cd "$H" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_KEYWORD" \
        >"$LOG/b.kw.out" 2>"$LOG/b.kw.err" </dev/null ) || true
    check "B3 ask announces hybrid mode naming the model_id" \
          '$GREP -q "viki ask: hybrid mode (FTS5 BM25 + vec0 cosine, model_id=$MODEL_ID)" "$LOG/b.kw.err"' "$LOG/b.kw.err"
    check "B4 PLANTED ANSWER (with model): keyword query ranks barn.md first" \
          'head -1 "$LOG/b.kw.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/b.kw.out"

    # HALF TWO of the vector-leg proof, and the single most important
    # assertion in this file. Same query, same corpus, same binary as A12
    # -- which returned nothing at all. The only difference is that a model
    # is loaded. Nothing but rung 2 can account for the result.
    ( cd "$H" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_SEMANTIC" \
        >"$LOG/b.sem.out" 2>"$LOG/b.sem.err" </dev/null ) || true
    check "B5 VECTOR PROOF (half 2): semantic-only query retrieves barn.md (A12 found nothing)" \
          'head -1 "$LOG/b.sem.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/b.sem.out"

    # Non-vacuity for B5: cosine search always returns SOMETHING, so
    # "barn.md came back" only means anything if a differently-aimed
    # zero-overlap query comes back with something else.
    ( cd "$H" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_SEMANTIC_CTL" \
        >"$LOG/b.semctl.out" 2>"$LOG/b.semctl.err" </dev/null ) || true
    check "B6 CONTROL: a tax-flavoured semantic query ranks tax.md first, not barn.md" \
          'head -1 "$LOG/b.semctl.out" | $GREP -qE "${RANK1}\./docs/tax\.md\$"' "$LOG/b.semctl.out"

    check "B7 --k caps the number of results" \
          '[ "$( cd "$H" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_SEMANTIC" --k 2 2>/dev/null </dev/null | $GREP -cE "'"$ANYRANK"'" )" = "2" ]'
fi

# ------------------------------- 5. D-11: compute once, share via fossil uv --
section "5. D-11 compute-once -- cache push / fresh clone / cache pull"

# The work checkout is the one that computes. It indexes WITH the model
# when one is available, so the uv round trip carries real embeddings and
# not just FTS rows; without a model the loop is still proven, just at
# rung 0.
if [ "$HAVE_MODEL" = 1 ]; then PUSH_MODEL="$VIKI_MODEL_DIR"; else PUSH_MODEL="$NOMODEL"; fi

# CONTROL, before anything is pushed: `viki cache pull` must FAIL in a
# checkout where no cache has ever been published. Without this, C7's
# "pull exited 0" would be consistent with pull being a no-op that always
# succeeds.
pull_before_rc=0
( cd "$D" && "$VIKI_BIN" cache pull >"$LOG/c.pull0.out" 2>"$LOG/c.pull0.err" </dev/null ) || pull_before_rc=$?
check "C0 CONTROL: cache pull fails when nothing has been pushed yet" \
      '[ "$pull_before_rc" -ne 0 ]' "$LOG/c.pull0.err"

( cd "$WORK/work" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" index . \
    >"$LOG/c.index.out" 2>"$LOG/c.index.err" </dev/null ) || true
# 4 = the 3 corpus documents plus the uncommitted witness.
check "C1 work checkout indexed the corpus + the uncommitted witness" \
      '$GREP -qE "^viki index: [0-9]+ file\(s\) scanned, 4 \(re\)chunked" "$LOG/c.index.err"' "$LOG/c.index.err"
check "C2 work checkout indexed the wiki page and the ticket (both NONZERO)" \
      '$GREP -q "1 wiki page(s), 1 (re)chunked" "$LOG/c.index.err" && $GREP -q "1 ticket(s), 1 (re)chunked" "$LOG/c.index.err"' \
      "$LOG/c.index.err"

# `viki cache push` is the one command here whose failure IS visible in the
# exit status -- and whose own error text lands on STDOUT, because
# viki_cache.c's run() inherits both streams (FINDINGS.md). Assert status.
push_rc=0
( cd "$WORK/work" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" cache push \
    >"$LOG/c.push.out" 2>"$LOG/c.push.err" </dev/null ) || push_rc=$?
check "C3 cache push succeeded" '[ "$push_rc" -eq 0 ]' "$LOG/c.push.out"
check "C4 the encrypted hub now holds the uv blob viki-cache.db" \
      '"$FOSSIL" uv list -R "$HUB" </dev/null 2>&1 | $GREP -q "viki-cache.db"' "$LOG/c.push.out"

# ---- D-12: THE MODEL TRAVELS TOO ----
# VIKI_DESIGN.md D-12: "Embedding caches -- and the pinned model file
# itself -- travel as Fossil unversioned files ... a fresh clone pulls
# corpus + model + embeddings from one endpoint, no third-party downloads
# at runtime." Until this round only the cache db moved, so the second
# half of that sentence was false and nothing said so. `viki cache push`
# now publishes the model directory by default (--no-model opts out) and
# these assert the whole round trip, ending in a hybrid `ask` in a clone
# that has never seen a model.
#
# WHY `uv list` is parsed with awk and not `grep -q '^name$'`: FINDINGS.md
# -- `fossil uv list` ALWAYS prints the detail columns (`-l` is implied
# when 'list' is used), so an anchored line match can never fire, and its
# negation is an assertion that cannot fail. The name is the LAST field.
uvname(){ "$FOSSIL" uv list -R "$HUB" </dev/null 2>&1 | awk -v n="$1" '$NF==n{f=1} END{exit !f}'; }
if [ "$HAVE_MODEL" = 1 ]; then
    check "M1 push published the model as uv blobs (all three files, exact names)" \
          'uvname viki-model/model.onnx && uvname viki-model/vocab.txt && uvname viki-model/viki-manifest.json' \
          "$LOG/c.push.err"
    check "M2 push said so, naming the epoch it published" \
          '$GREP -q "publishing model epoch .$MODEL_ID." "$LOG/c.push.err"' "$LOG/c.push.err"
    # CONTROL for M1: uvname() must be able to answer NO. Without this,
    # M1 would also pass against an awk expression that matched anything.
    check "M1b CONTROL: uvname() reports absent for a blob nobody published" \
          '! uvname viki-model/definitely-not-published'
else
    skip_ "M1 push published the model as uv blobs (all three files, exact names)" \
          "no embedding model at $VIKI_MODEL_DIR"
    skip_ "M2 push said so, naming the epoch it published" \
          "no embedding model at $VIKI_MODEL_DIR"
    skip_ "M1b CONTROL: uvname() reports absent for a blob nobody published" \
          "no embedding model at $VIKI_MODEL_DIR"
fi

fclone fresh
F="$WORK/fresh"

# CONTROL, before the pull: the destination the fresh clone will be told
# to use holds nothing at all. Everything found there afterwards was put
# there by `viki cache pull`.
check "M3 CONTROL: the fresh clone's model directory does not exist before the pull" \
      '[ ! -e "$PULLED_MODEL" ]'

check "C5 the fresh clone has NO cache db before the pull" \
      '[ ! -e "$F/.viki/cache.db" ]'
check "C6 the witness document does not exist on disk in the fresh clone" \
      '[ ! -e "$F/docs/uncommitted-witness.md" ]'

# CONTROL: ask the witness question in the fresh clone BEFORE pulling. It
# must find nothing -- this clone genuinely knows nothing about the
# witness, from the repo or from anywhere else.
( cd "$F" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" ask "$Q_WITNESS" \
    >"$LOG/c.pre.out" 2>"$LOG/c.pre.err" </dev/null ) || true
check "C7 CONTROL: before the pull, the fresh clone cannot answer the witness query" \
      '[ ! -s "$LOG/c.pre.out" ]' "$LOG/c.pre.out"

# That control ask created an empty .viki/cache.db (viki_db_open opens with
# SQLITE_OPEN_CREATE). Remove it so the pull writes into a pristine
# checkout and C8's byte-comparison is meaningful.
rm -rf "$F/.viki"

pull_rc=0
( cd "$F" && VIKI_MODEL_DIR="$PULLED_MODEL" "$VIKI_BIN" cache pull \
    >"$LOG/c.pull.out" 2>"$LOG/c.pull.err" </dev/null ) || pull_rc=$?
check "C8 cache pull succeeded" '[ "$pull_rc" -eq 0 ]' "$LOG/c.pull.err"
check "C9 cache pull materialized the cache db" '[ -s "$F/.viki/cache.db" ]'
# Byte identity needs no sqlite3 and is the strongest available statement
# that the uv round trip is lossless. Nothing has written to the work
# checkout's cache since the push.
check "C10 the pulled cache is byte-identical to the pushed one" \
      'cmp -s "$WORK/work/.viki/cache.db" "$F/.viki/cache.db"'

if [ "$HAVE_MODEL" = 1 ] && [ "$HAVE_SQLITE3" = 1 ]; then
    # Byte identity of the file already implies this, but state it in the
    # terms D-11 is actually about: the VECTORS survived, addressed by
    # content hash, so the fresh peer never has to recompute them.
    FP_SQL="SELECT group_concat(substr(content_hash,1,12)||':'||hex(substr(embedding,1,8)),'|') FROM (SELECT * FROM viki_chunk WHERE embedding IS NOT NULL ORDER BY content_hash, chunk_ix);"
    # `|| true` for the same reason as the viki calls above: when an earlier
    # assertion has already failed there may be no cache db here at all, and
    # sqlite3's error must be a FAIL line rather than an abort. The `-n`
    # test is what makes an empty/failed read count as a failure instead of
    # matching another empty read and passing vacuously.
    SPOKE_FP="$(sqlite3 "$WORK/work/.viki/cache.db" "$FP_SQL" 2>/dev/null || true)"
    FRESH_FP="$(sqlite3 "$F/.viki/cache.db" "$FP_SQL" 2>/dev/null || true)"
    check "C11 embeddings round-tripped through fossil uv unchanged" \
          '[ -n "$SPOKE_FP" ] && [ "$SPOKE_FP" = "$FRESH_FP" ]'
else
    skip_ "C11 embeddings round-tripped through fossil uv unchanged" \
          "needs both a model and sqlite3"
fi

# ---- D-12, the other half: the MODEL came back too ----
if [ "$HAVE_MODEL" = 1 ]; then
    check "M4 pull materialized exactly the three model files and nothing else" \
          '[ "$(ls -A "$PULLED_MODEL" 2>/dev/null | sort | tr "\n" " ")" = "model.onnx viki-manifest.json vocab.txt " ]' \
          "$LOG/c.pull.err"
    # Byte identity, not "a file appeared". A truncated 23 MB export is
    # the failure this is really guarding, and it would still leave a
    # plausible-looking model.onnx behind.
    check "M5 the pulled model.onnx/vocab.txt/manifest are byte-identical to the pushed ones" \
          'cmp -s "$VIKI_MODEL_DIR/model.onnx" "$PULLED_MODEL/model.onnx" \
           && cmp -s "$VIKI_MODEL_DIR/vocab.txt" "$PULLED_MODEL/vocab.txt" \
           && cmp -s "$VIKI_MODEL_DIR/viki-manifest.json" "$PULLED_MODEL/viki-manifest.json"'
    # viki must have CHECKED, not just copied: the manifest's model_sha256
    # is the only integrity statement viki has about a binary it is then
    # going to run inference with, and nothing else in the codebase reads
    # it. Assert the verification was performed for both blobs.
    check "M6 pull verified both blobs' sha256 against the epoch pin before installing it" \
          '[ "$($GREP -c "sha256 verified against the epoch pin" "$LOG/c.pull.err")" -ge 2 ]' \
          "$LOG/c.pull.err"

    # THE D-12 CLAIM ITSELF. A clone that has never had a model asks in
    # HYBRID mode using only what `viki cache pull` fetched. Note this
    # names $PULLED_MODEL, never $VIKI_MODEL_DIR -- the repo's own model
    # is not in the picture.
    ( cd "$F" && VIKI_MODEL_DIR="$PULLED_MODEL" "$VIKI_BIN" ask "$Q_WITNESS_SEMANTIC" \
        >"$LOG/m.hyb.out" 2>"$LOG/m.hyb.err" </dev/null ) || true
    check "M7 SELF-CONTAINED: the fresh clone runs HYBRID retrieval on the PULLED model" \
          '$GREP -q "viki ask: hybrid mode (FTS5 BM25 + vec0 cosine, model_id=$MODEL_ID)" "$LOG/m.hyb.err" \
           && head -1 "$LOG/m.hyb.out" | $GREP -qE "${RANK1}\./docs/uncommitted-witness\.md\$"' \
          "$LOG/m.hyb.err"

    # CONTROL for M7, and the reason EMPTYMODEL exists. Same clone, same
    # query, same binary; the only difference is a model directory with
    # nothing in it. If this also answered, M7 would be evidence of
    # nothing -- some other model, or some non-vector path, would be
    # doing the work.
    ( cd "$F" && VIKI_MODEL_DIR="$EMPTYMODEL" "$VIKI_BIN" ask "$Q_WITNESS_SEMANTIC" \
        >"$LOG/m.empty.out" 2>"$LOG/m.empty.err" </dev/null ) || true
    check "M8 CONTROL: with an EMPTY model dir the same query degrades and finds nothing" \
          '$GREP -q "viki ask: degraded mode" "$LOG/m.empty.err" && [ ! -s "$LOG/m.empty.out" ]' \
          "$LOG/m.empty.out"

    # "no third-party downloads at runtime" (D-12's words) made
    # observable: the fresh clone's ONLY sync peer -- the thing `fossil
    # uv sync` talked to for both the cache and the model -- is a local
    # file:// path to the encrypted hub. No http(s) endpoint is
    # configured, so nothing could have been fetched from one.
    ( cd "$F" && "$FOSSIL" remote </dev/null >"$LOG/c.remote.out" 2>&1 ) || true
    check "M9 NO THIRD-PARTY FETCH: the clone's only sync peer is the local file:// hub" \
          '$GREP -qE "^file://.*/hub\.efossil\$" "$LOG/c.remote.out" && ! $GREP -qiE "https?://" "$LOG/c.remote.out"' \
          "$LOG/c.remote.out"
else
    for t in \
        "M4 pull materialized exactly the three model files and nothing else" \
        "M5 the pulled model.onnx/vocab.txt/manifest are byte-identical to the pushed ones" \
        "M6 pull verified both blobs' sha256 against the epoch pin before installing it" \
        "M7 SELF-CONTAINED: the fresh clone runs HYBRID retrieval on the PULLED model" \
        "M8 CONTROL: with an EMPTY model dir the same query degrades and finds nothing" \
        "M9 NO THIRD-PARTY FETCH: the clone's only sync peer is the local file:// hub"
    do
        skip_ "$t" "no embedding model at $VIKI_MODEL_DIR"
    done
fi

# ---- THE COMPUTE-ONCE CLAIM ----
# `viki index` is never invoked in $F. Anything retrievable here was
# computed in the work checkout and travelled as a fossil unversioned file.
# The witness makes this airtight: its text is not in the repository at
# all, so no amount of re-derivation from the clone could produce it.
( cd "$F" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" ask "$Q_WITNESS" \
    >"$LOG/c.wit.out" 2>"$LOG/c.wit.err" </dev/null ) || true
check "C12 COMPUTE-ONCE: the fresh clone answers from the pulled cache, having never indexed" \
      'head -1 "$LOG/c.wit.out" | $GREP -qE "${RANK1}\./docs/uncommitted-witness\.md\$"' "$LOG/c.wit.out"

# The non-file artifact types must survive the round trip too, with their
# virtual paths intact. These assert RANK 1, not mere presence: the whole
# corpus is only six chunks and `viki ask` defaults to k=5, so "appears
# somewhere in the results" is very nearly free and would be a vacuous
# assertion. (Measured: both of these are genuinely rank 1, scoring
# 0.0328 = 1/61 + 1/61, i.e. top of BOTH legs.)
( cd "$F" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" ask "windmill stock tank pasture" \
    >"$LOG/c.wiki.out" 2>"$LOG/c.wiki.err" </dev/null ) || true
check "C13 the wiki page is retrievable from the pulled cache, at rank 1 (wiki: virtual path)" \
      'head -1 "$LOG/c.wiki.out" | $GREP -qE "${RANK1}wiki:PumpMaintenance\$"' "$LOG/c.wiki.out"
( cd "$F" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" ask "gearbox rattles metallic knock" \
    >"$LOG/c.tkt.out" 2>"$LOG/c.tkt.err" </dev/null ) || true
check "C14 the ticket is retrievable from the pulled cache, at rank 1 (ticket: virtual path)" \
      'head -1 "$LOG/c.tkt.out" | $GREP -qE "${RANK1}ticket:[0-9a-f]+\$"' "$LOG/c.tkt.out"

if [ "$HAVE_MODEL" = 1 ]; then
    # The pulled VECTORS are usable, not merely present -- and proved the
    # same two-sided way A12/B5 proved the vector leg in the first place,
    # because a one-sided "it came back" would not distinguish rung 2 from
    # a stray keyword hit. Half 1: with no model, this checkout's pulled
    # cache answers nothing at all for a zero-overlap query.
    ( cd "$F" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_WITNESS_SEMANTIC" \
        >"$LOG/c.witsem0.out" 2>"$LOG/c.witsem0.err" </dev/null ) || true
    check "C15 CONTROL: the semantic witness query finds nothing without a model" \
          '[ ! -s "$LOG/c.witsem0.out" ]' "$LOG/c.witsem0.out"
    # Half 2: with the model, the PULLED embeddings -- computed in another
    # checkout, for a file that does not exist here -- put the witness first.
    ( cd "$F" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_WITNESS_SEMANTIC" \
        >"$LOG/c.witsem.out" 2>"$LOG/c.witsem.err" </dev/null ) || true
    check "C16 the PULLED vectors work: semantic-only witness query ranks it 1, no re-indexing" \
          'head -1 "$LOG/c.witsem.out" | $GREP -qE "${RANK1}\./docs/uncommitted-witness\.md\$"' "$LOG/c.witsem.out"
else
    skip_ "C15 CONTROL: the semantic witness query finds nothing without a model" \
          "no embedding model at $VIKI_MODEL_DIR"
    skip_ "C16 the PULLED vectors work: semantic-only witness query ranks it 1, no re-indexing" \
          "no embedding model at $VIKI_MODEL_DIR"
fi

# Degraded mode must also work off a pulled cache -- the FTS mirror travels
# with it, so a peer with no model still gets rung 0 for free.
( cd "$F" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD" \
    >"$LOG/c.deg.out" 2>"$LOG/c.deg.err" </dev/null ) || true
check "C17 the pulled cache also serves a peer with no model at all" \
      'head -1 "$LOG/c.deg.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/c.deg.out"

# STRUCTURAL PROOF of "WITHOUT re-indexing". The script never calls `viki
# index` in $F, but that is a claim about this file rather than about the
# run. This is the observable version: after every query above, the cache
# db is STILL byte-for-byte what `viki cache pull` wrote. Not one byte was
# produced locally, so no indexing (and no embedding) can have happened
# here -- every answer came from the work checkout's computation.
check "C18 nothing in the fresh clone ever wrote to the cache (no local indexing)" \
      'cmp -s "$WORK/work/.viki/cache.db" "$F/.viki/cache.db"'

# ------------------------- 6. the ask line's content_hash is a REAL hash --
section "6. every result line is citable -- content_hash on the ask line"

# KICKOFF.md deliverable 2 asks `viki ask` for "results with source
# content_hash, snippet, score" and VIKI_DESIGN.md's agent contract has
# agents CITING content_hashes. Until this round the CLI printed neither:
# the line carried only the viki_source path, which is a best-effort join
# that renders "(source path unknown)" whenever the path row is stale or
# missing -- i.e. a hit with nothing citable on it at all.
#
# These run against $D (the degraded-mode clone), whose cache db is
# viki's own UNENCRYPTED local cache, readable by stock sqlite3.
G_HASH="$(hit_hash "$LOG/a.kw.out")"
G_HASH_TAX="$(hit_hash "$LOG/a.kwctl.out")"

check "G1 EVERY result line carries a <64-hex>#<ix> citation, not just the top one" \
      '[ "$($GREP -cE "'"$ANYRANK"'" "$LOG/a.kw.out")" -ge 2 ] \
       && [ "$($GREP -cE "'"$ANYRANK"'" "$LOG/a.kw.out")" = "$($GREP -cE "'"$HASHFIELD"'" "$LOG/a.kw.out")" ]' \
      "$LOG/a.kw.out"
# The pre-fix line printed "(source path unknown)#0" for exactly the hits
# that most needed an identity. Nothing may print that any more.
check "G2 no result line falls back to an uncitable placeholder" \
      '! $GREP -q "(source path unknown)" "$LOG/a.kw.out"' "$LOG/a.kw.out"
check "G3 two different documents cite two different hashes" \
      '[ -n "$G_HASH" ] && [ -n "$G_HASH_TAX" ] && [ "$G_HASH" != "$G_HASH_TAX" ]'

if [ -n "$SHA256" ]; then
    # THE assertion that the hash is REAL and not merely hex-shaped:
    # recompute it with a tool that shares no code with viki. barn.md is
    # small enough to be a single chunk, so the chunk's content_hash is
    # the sha256 of the whole file -- viki_index.c hashes the file's
    # bytes, then splits the SAME bytes into chunks under that one hash.
    check "G4 the printed content_hash IS sha256(docs/barn.md), computed independently" \
          '[ "$G_HASH" = "$(hash_of "$D/docs/barn.md")" ]'
    # Non-vacuity for G4: hash_of must be able to disagree. Same tool,
    # a different file from the same corpus.
    check "G4b CONTROL: it is NOT sha256 of a different document (G4 can fail)" \
          '[ "$G_HASH" != "$(hash_of "$D/docs/tax.md")" ]'
else
    skip_ "G4 the printed content_hash IS sha256(docs/barn.md), computed independently" \
          "no shasum/sha256sum on PATH"
    skip_ "G4b CONTROL: it is NOT sha256 of a different document (G4 can fail)" \
          "no shasum/sha256sum on PATH"
fi

if [ "$HAVE_SQLITE3" = 1 ]; then
    # ... and that the same value addresses the chunk inside the cache
    # db, which is what makes `<hash>#<ix>` usable as a citation: it is
    # exactly the pair /api/chunk?hash=&ix= takes.
    check "G5 the printed hash addresses a real chunk in the cache db" \
          '[ "$(sqlite3 "$D/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE content_hash='"'"'$G_HASH'"'"' AND chunk_ix=0;")" = "1" ]'
    check "G6 and viki_source maps that hash to the document the line named" \
          '[ "$(sqlite3 "$D/.viki/cache.db" "SELECT path FROM viki_source WHERE content_hash='"'"'$G_HASH'"'"';")" = "./docs/barn.md" ]'
    # Non-vacuity for G5/G6: the same queries must come back EMPTY for a
    # hash that differs by one character. Without this, G5/G6 would also
    # pass against a query that matched any row at all.
    G_BOGUS="$(printf '%s' "$G_HASH" | sed 's/^./0/;s/^0\(.\)/1\1/')"
    check "G5b CONTROL: a one-character-different hash addresses nothing (G5/G6 can fail)" \
          '[ "$G_BOGUS" != "$G_HASH" ] \
           && [ "$(sqlite3 "$D/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE content_hash='"'"'$G_BOGUS'"'"';")" = "0" ] \
           && [ "$(sqlite3 "$D/.viki/cache.db" "SELECT count(*) FROM viki_source WHERE content_hash='"'"'$G_BOGUS'"'"';")" = "0" ]'
else
    skip_ "G5 the printed hash addresses a real chunk in the cache db" "no sqlite3 on PATH"
    skip_ "G6 and viki_source maps that hash to the document the line named" "no sqlite3 on PATH"
    skip_ "G5b CONTROL: a one-character-different hash addresses nothing (G5/G6 can fail)" "no sqlite3 on PATH"
fi

# ------------------------------ 7. withdrawn content stops being served --
section "7. stale-content invalidation -- edits and deletions are withdrawn"

# Before this round `viki index` only ever INSERTED. Rewriting a document
# left its old chunks answering queries forever, and deleting a document
# left its text retrievable under its own full path -- measured, both at
# RANK 1. That is not a cosmetic leak: it is viki serving content that was
# deliberately withdrawn, with a citation that looks current.
#
# Its own clone, because these assertions destroy corpus documents.
fclone gc
G="$WORK/gc"

# WHY every mutation below is followed by `touch -t`: viki_index.c skips a
# file whose viki_source mtime is unchanged, and mtime has 1-second
# granularity. This script rewrites barn.md within the same second it was
# first indexed on any fast machine, so without an explicitly different
# stamp the re-index is a no-op and the whole section passes vacuously by
# testing nothing. A fixed past date is deterministic where `touch` (now)
# is a race.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
    >"$LOG/g.index0.out" 2>"$LOG/g.index0.err" </dev/null ) || true

# Capture the two hashes BEFORE mutating, so the db-level assertions below
# know what to look for after the rows are gone.
GC_BARN_HASH=""
GC_TAX_HASH=""
if [ -n "$SHA256" ]; then
    GC_BARN_HASH="$(hash_of "$G/docs/barn.md")"
    GC_TAX_HASH="$(hash_of "$G/docs/tax.md")"
fi

# Baseline: both documents ARE retrievable right now. Without this, "the
# old text is gone" would be consistent with it never having been there.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_OLD_BARN" \
    >"$LOG/g.old0.out" 2>"$LOG/g.old0.err" </dev/null ) || true
check "H1 BASELINE: barn.md's original wording is retrievable before the edit" \
      'head -1 "$LOG/g.old0.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/g.old0.out"
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD_CTL" \
    >"$LOG/g.tax0.out" 2>"$LOG/g.tax0.err" </dev/null ) || true
check "H2 BASELINE: tax.md is retrievable before the deletion" \
      'head -1 "$LOG/g.tax0.out" | $GREP -qE "${RANK1}\./docs/tax\.md\$"' "$LOG/g.tax0.out"

# ---- (a) an EDIT withdraws the old text and publishes the new ----
cat > "$G/docs/barn.md" <<'EOF'
# Barn notes

The cider press was overhauled in the equipment shed on Thursday.
Marguerite ordered replacement gaskets for the hydraulic ram.
EOF
touch -t 202001010000 "$G/docs/barn.md"
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
    >"$LOG/g.index1.out" 2>"$LOG/g.index1.err" </dev/null ) || true

check "H3 the re-index reports retiring the superseded chunk" \
      '$GREP -qE "^viki index: [0-9]+ stale source\(s\) retired, [1-9][0-9]* orphan chunk\(s\) removed\$" "$LOG/g.index1.err"' \
      "$LOG/g.index1.err"

# The withdrawal itself. Q_OLD_BARN's terms exist nowhere else in the
# corpus (checked against grocery.md, tax.md, the wiki page and the
# ticket), so a non-empty result set here can only be the superseded text.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_OLD_BARN" \
    >"$LOG/g.old1.out" 2>"$LOG/g.old1.err" </dev/null ) || true
check "H4 EDIT: the OLD text is no longer retrievable at all (H1 found it)" \
      '[ ! -s "$LOG/g.old1.out" ] && $GREP -qx "(no matches)" "$LOG/g.old1.err"' "$LOG/g.old1.out"

# The other side: invalidation must not be a euphemism for losing the
# document. The NEW wording has to answer, under the same path.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_NEW_BARN" \
    >"$LOG/g.new1.out" 2>"$LOG/g.new1.err" </dev/null ) || true
check "H5 EDIT: the NEW text IS retrievable, at rank 1, under the same path" \
      'head -1 "$LOG/g.new1.out" | $GREP -qE "${RANK1}\./docs/barn\.md\$"' "$LOG/g.new1.out"
# ... and its citation is the NEW content, not the old hash left in place.
if [ -n "$SHA256" ]; then
    check "H6 EDIT: the citation now names the NEW content hash, not the old one" \
          '[ "$(hit_hash "$LOG/g.new1.out")" = "$(hash_of "$G/docs/barn.md")" ] \
           && [ "$(hit_hash "$LOG/g.new1.out")" != "$GC_BARN_HASH" ]' "$LOG/g.new1.out"
else
    skip_ "H6 EDIT: the citation now names the NEW content hash, not the old one" \
          "no shasum/sha256sum on PATH"
fi

# ---- (b) a DELETION withdraws the document ----
rm -f "$G/docs/tax.md"
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
    >"$LOG/g.index2.out" 2>"$LOG/g.index2.err" </dev/null ) || true

check "H7 the re-index reports retiring the deleted file's source row" \
      '$GREP -qE "^viki index: [1-9][0-9]* stale source\(s\) retired, [1-9][0-9]* orphan chunk\(s\) removed\$" "$LOG/g.index2.err"' \
      "$LOG/g.index2.err"

( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD_CTL" \
    >"$LOG/g.tax1.out" 2>"$LOG/g.tax1.err" </dev/null ) || true
check "H8 DELETE: the deleted document's text is no longer retrievable (H2 found it)" \
      '[ ! -s "$LOG/g.tax1.out" ] && $GREP -qx "(no matches)" "$LOG/g.tax1.err"' "$LOG/g.tax1.out"

# THE CONTROL that makes H4/H8 mean "withdrawn", not "the sweep ate the
# cache". grocery.md was never touched by either mutation and must still
# answer, at rank 1, after both sweeps.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_GROCERY" \
    >"$LOG/g.ctl.out" 2>"$LOG/g.ctl.err" </dev/null ) || true
check "H9 CONTROL: an untouched document still answers after both sweeps" \
      'head -1 "$LOG/g.ctl.out" | $GREP -qE "${RANK1}\./docs/grocery\.md\$"' "$LOG/g.ctl.out"
# And the non-file sources must survive: the sweep is scoped, and wiping
# wiki:/ticket: rows while indexing a filesystem tree would be exactly the
# over-deletion this scoping exists to prevent.
( cd "$G" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "gearbox rattles metallic knock" \
    >"$LOG/g.tkt.out" 2>"$LOG/g.tkt.err" </dev/null ) || true
check "H10 CONTROL: the wiki page and ticket survived the sweeps too" \
      'head -1 "$LOG/g.tkt.out" | $GREP -qE "${RANK1}ticket:[0-9a-f]+\$" \
       && $GREP -q "wiki:PumpMaintenance" "$LOG/g.tkt.out"' "$LOG/g.tkt.out"

if [ "$HAVE_FTS5" = 1 ] && [ -n "$SHA256" ]; then
    # Corroboration below the retrieval layer. chunk_fts is a PLAIN FTS5
    # table (not external-content), so a row deleted from viki_chunk and
    # left in chunk_fts would still be fully searchable -- assert BOTH.
    check "H11 the withdrawn chunks are gone from viki_chunk AND chunk_fts" \
          '[ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE content_hash IN ('"'"'$GC_BARN_HASH'"'"','"'"'$GC_TAX_HASH'"'"');")" = "0" ] \
           && [ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM chunk_fts WHERE content_hash IN ('"'"'$GC_BARN_HASH'"'"','"'"'$GC_TAX_HASH'"'"');")" = "0" ] \
           && [ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM viki_source WHERE path='"'"'./docs/tax.md'"'"';")" = "0" ]'
    # CONTROL for H11: the same three queries against the UNTOUCHED
    # document must all come back nonzero, so "0" is a fact about the
    # withdrawn hashes and not about the queries.
    check "H11b CONTROL: the untouched document's rows are all still present" \
          '[ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM viki_chunk WHERE content_hash='"'"'$(hash_of "$G/docs/grocery.md")'"'"';")" -ge 1 ] \
           && [ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM chunk_fts WHERE content_hash='"'"'$(hash_of "$G/docs/grocery.md")'"'"';")" -ge 1 ] \
           && [ "$(sqlite3 "$G/.viki/cache.db" "SELECT count(*) FROM viki_source WHERE path='"'"'./docs/grocery.md'"'"';")" = "1" ]'
else
    skip_ "H11 the withdrawn chunks are gone from viki_chunk AND chunk_fts" \
          "needs shasum/sha256sum and an fts5-capable sqlite3"
    skip_ "H11b CONTROL: the untouched document's rows are all still present" \
          "needs shasum/sha256sum and an fts5-capable sqlite3"
fi

# ------------------------- 8. a mixed-epoch cache scores each chunk once --
section "8. mixed-epoch cache -- the BM25 leg is counted once, not per epoch"

# chunk_fts carries one row per (content_hash, model_id, chunk_ix) and
# `viki ask` deliberately does NOT filter it by model_id -- it must not,
# or a peer with no model could never search a cache a model-having peer
# built (that is C17). So a cache holding two model epochs returns the
# SAME chunk twice from the keyword leg, and before this round both copies
# were scored, inflating every BM25 contribution by the number of epochs
# present. Not cosmetic: it reorders the top-K and changes its membership.
#
# Needs a model to make a second epoch at all: with none, both index runs
# write model_id='none' and there is nothing to double-count.
if [ "$HAVE_MODEL" = 0 ]; then
    for t in \
        "J1 SETUP: the cache really holds two epochs of the same chunk" \
        "J2 a mixed-epoch cache answers IDENTICALLY to a single-epoch one" \
        "J3 the top hit scores 1/61 -- one leg, counted once" \
        "J4 no chunk appears twice in a hybrid result list"
    do
        skip_ "$t" "no embedding model at $VIKI_MODEL_DIR (cannot create a second epoch)"
    done
else
    fclone mixed
    X="$WORK/mixed"
    # Same tree, same cache db, two epochs: model_id='none' then the real
    # model. $D above is the single-epoch control -- same corpus, same
    # commit, indexed once at model_id='none'.
    ( cd "$X" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" index . \
        >"$LOG/j.index0.out" 2>"$LOG/j.index0.err" </dev/null ) || true
    ( cd "$X" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" index . \
        >"$LOG/j.index1.out" 2>"$LOG/j.index1.err" </dev/null ) || true

    if [ "$HAVE_FTS5" = 1 ]; then
        # THE PRECONDITION. If this fails, everything below is vacuous:
        # there would be no duplicate rows to mis-score.
        check "J1 SETUP: the cache really holds two epochs of the same chunk" \
              '[ "$(sqlite3 "$X/.viki/cache.db" "SELECT count(DISTINCT model_id) FROM viki_chunk;")" = "2" ] \
               && [ "$(sqlite3 "$X/.viki/cache.db" "SELECT count(*) FROM chunk_fts WHERE content_hash='"'"'$G_HASH'"'"' AND chunk_ix=0;")" = "2" ] \
               && [ "$(sqlite3 "$D/.viki/cache.db" "SELECT count(*) FROM chunk_fts WHERE content_hash='"'"'$G_HASH'"'"' AND chunk_ix=0;")" = "1" ]'
    else
        skip_ "J1 SETUP: the cache really holds two epochs of the same chunk" \
              "no fts5-capable sqlite3 on PATH"
    fi

    # Both asks run WITHOUT a model, so only the BM25 leg is live and the
    # comparison is about that leg alone. $D and $X are clones of the same
    # commit with the same corpus, so a correct implementation must give
    # byte-identical output -- same order, same scores, same snippets.
    ( cd "$X" && VIKI_MODEL_DIR="$NOMODEL" "$VIKI_BIN" ask "$Q_KEYWORD" \
        >"$LOG/j.mixed.out" 2>"$LOG/j.mixed.err" </dev/null ) || true
    check "J2 a mixed-epoch cache answers IDENTICALLY to a single-epoch one" \
          '[ -s "$LOG/j.mixed.out" ] && cmp -s "$LOG/a.kw.out" "$LOG/j.mixed.out"' "$LOG/j.mixed.out"

    # The same claim as an absolute number, so a future regression that
    # broke BOTH caches the same way could not hide behind J2's
    # comparison. Reciprocal-rank fusion with k=60 gives a single leg's
    # rank-1 contribution as 1/(60+1) = 0.016393 -> "0.0164". Two epochs
    # scored twice measured 0.0325 before the fix.
    check "J3 the top hit scores 1/61 -- one leg, counted once" \
          'head -1 "$LOG/j.mixed.out" | $GREP -qE "^\[1\] rrf=0\.0164  "' "$LOG/j.mixed.out"

    # The vector leg goes through the same guard. With the model loaded,
    # both legs run over a cache with duplicate FTS rows; every hit in the
    # list must still name a distinct chunk.
    ( cd "$X" && VIKI_MODEL_DIR="$VIKI_MODEL_DIR" "$VIKI_BIN" ask "$Q_KEYWORD" --k 5 \
        >"$LOG/j.hyb.out" 2>"$LOG/j.hyb.err" </dev/null ) || true
    check "J4 no chunk appears twice in a hybrid result list" \
          '[ "$($GREP -cE "'"$ANYRANK"'" "$LOG/j.hyb.out")" -ge 3 ] \
           && [ "$($GREP -E "'"$ANYRANK"'" "$LOG/j.hyb.out" | awk "{print \$3}" | wc -l)" \
                = "$($GREP -E "'"$ANYRANK"'" "$LOG/j.hyb.out" | awk "{print \$3}" | sort -u | wc -l)" ]' \
          "$LOG/j.hyb.out"
fi

# --------------------------------------------- 9. selftests / CLI contract --
section "9. selftests and CLI contract"

cd "$WORK"
check "D1 sqlite-vec is really statically linked (not a stub)" \
      '"$VIKI_BIN" vec-selftest </dev/null | $GREP -q "^vec-selftest: PASS$"'
if [ "$HAVE_MODEL" = 1 ]; then
    check "D2 embed-selftest passes its semantic property check" \
          '"$VIKI_BIN" embed-selftest "$VIKI_MODEL_DIR" </dev/null | $GREP -qx "embed-selftest: PASS"'
else
    skip_ "D2 embed-selftest passes its semantic property check" \
          "no embedding model at $VIKI_MODEL_DIR"
fi
check "D3 version is reported on stdout" \
      '[ -n "$("$VIKI_BIN" version </dev/null)" ]'
check "D4 an unknown subcommand exits nonzero" \
      '! "$VIKI_BIN" definitely-not-a-subcommand >/dev/null 2>&1 </dev/null'
check "D5 ask with no query exits nonzero" \
      '! "$VIKI_BIN" ask >/dev/null 2>&1 </dev/null'

# `viki cache push`'s third argument used to be UNCONDITIONALLY a db path,
# so the moment the model leg gained a `--no-model` opt-out, the obvious
# invocation `viki cache push --no-model` would have been read as "push
# the cache db named --no-model" -- a flag that silently does the exact
# opposite of what it says. viki.c now matches the flag by name and takes
# the last non-flag argument as the path. Run LAST, after every C/M
# assertion, because it publishes to the hub again.
#
# The observable that separates flag-parsed from path-swallowed is which
# file lands on the `uv add` line: the DEFAULT .viki/cache.db, not
# "--no-model".
if [ "$HAVE_MODEL" = 1 ]; then
    nm_rc=0
    ( cd "$WORK/work" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" cache push --no-model \
        >"$LOG/d.nomodel.out" 2>"$LOG/d.nomodel.err" </dev/null ) || nm_rc=$?
    check "D6 'cache push --no-model' is parsed as a FLAG, not swallowed as the db path" \
          '[ "$nm_rc" -eq 0 ] \
           && $GREP -q "uv add .viki/cache.db --as viki-cache.db" "$LOG/d.nomodel.err" \
           && ! $GREP -q "\-\-no-model --as" "$LOG/d.nomodel.err" \
           && $GREP -q "\-\-no-model: publishing the embedding cache only" "$LOG/d.nomodel.err" \
           && ! $GREP -q "viki-model/" "$LOG/d.nomodel.err"' \
          "$LOG/d.nomodel.err"
    # CONTROL: the model leg is alive in this very checkout -- drop the
    # flag and the same command talks about viki-model/ again. Without
    # this, D6's silence would be consistent with the model leg simply
    # never running here.
    nm2_rc=0
    ( cd "$WORK/work" && VIKI_MODEL_DIR="$PUSH_MODEL" "$VIKI_BIN" cache push \
        >"$LOG/d.model.out" 2>"$LOG/d.model.err" </dev/null ) || nm2_rc=$?
    check "D7 CONTROL: without the flag the same push does address viki-model/ (D6 can fail)" \
          '[ "$nm2_rc" -eq 0 ] && $GREP -q "viki-model/" "$LOG/d.model.err"' "$LOG/d.model.err"
else
    skip_ "D6 'cache push --no-model' is parsed as a FLAG, not swallowed as the db path" \
          "no embedding model at $VIKI_MODEL_DIR"
    skip_ "D7 CONTROL: without the flag the same push does address viki-model/ (D6 can fail)" \
          "no embedding model at $VIKI_MODEL_DIR"
fi

# ------------------------------------------------------------------ summary --
SUMMARY_PRINTED=1
printf '\n--------------------------------------------------------------\n'
printf 'test/m1.sh: %d passed, %d failed, %d skipped\n' "$N_PASS" "$N_FAIL" "$N_SKIP"

if [ "$N_FAIL" -ne 0 ]; then
    printf 'RESULT: FAIL -- Milestone 1 definition of done is NOT met.\n'
    [ "${VIKI_TEST_KEEP:-0}" = "1" ] || printf 'Re-run with VIKI_TEST_KEEP=1 to keep the scratch tree and logs.\n'
    exit 1
fi
if [ "$N_SKIP" -ne 0 ]; then
    printf 'RESULT: PASS WITH SKIPS -- everything attempted passed, but %d\n' "$N_SKIP"
    printf 'assertion(s) were not attempted (see the SKIP lines above). This is\n'
    printf 'NOT a full Milestone 1 pass on its own.\n'
    exit 0
fi
printf 'RESULT: PASS -- Milestone 1 definition of done proven end to end\n'
printf 'on a scratch encrypted repo, with and without the model.\n'
exit 0
