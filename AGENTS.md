# AGENTS.md -- orient here first

A brand-new agent session should be able to pick up work from this file
alone (US-2c). Read `KICKOFF.md` for the full Milestone 1 brief; this file
is the current-state snapshot, kept up to date in the same commit as any
code change that makes it stale (KICKOFF.md's process rule).

## What exists right now

A real, working `viki` CLI with **both** retrieval rungs implemented:
FTS5 BM25 (rung 0) and ONNX sentence embeddings + `sqlite-ndvss` cosine
search (rung 2), fused by reciprocal rank fusion when a model is present,
degrading honestly to BM25-only when it isn't (VIKI_DESIGN.md's required
standalone path). `viki index` covers all four Fossil-native content
types: checkout files, wiki pages, tickets, and forum posts (not just
files). See `FINDINGS.md` for what was actually verified and how --
forum extraction has now been round-tripped against real, live forum
posts, which uncovered three real defects in it (escaped thread titles,
superseded post versions indexed as current, and card lookup running off
into the post body). All three are fixed in `src/viki_index.c` **and are
now in `build/dist/viki`** -- the shipped binary is the 2026-08-13
**19:48:15** build (`stat -f '%Sm %N' -t '%Y-%m-%d %H:%M:%S'
build/dist/viki`), newer than every file in `src/` (newest is
`src/viki.c` at 19:28:03), and `sh build/forum-e2e-probe.sh <empty-dir>`
is `PASS=26 FAIL=0` against it. Forum posts are nonetheless the **least**
verified of the four types -- four artifact shapes exercised, not in CI,
and not touched by `test/m1.sh`. See the forum bullet under "Verified
working end to end" for exactly what is and is not proven before you
trust that leg.

**The Milestone 1 definition-of-done test is `test/m1.sh`** -- 90
assertions, `90 passed, 0 failed, 0 skipped`, exit 0, re-measured
first-hand 2026-08-13 against that same 19:48:15 binary. Run it first
when you want to know whether this tree still works. It grew from 54 to
90 when the three fixes below landed: sections 6/7/8 cover the citable
`content_hash`, stale-content invalidation, and mixed-epoch scoring, and
section 5 gained the D-12 model round trip (`M1`-`M9`).

Non-vacuity is not assumed: a binary compiled from **all of git `HEAD`'s
`src/`** (say it that way -- the bare phrase "a pre-fix binary" is
ambiguous here and has already misled once, see the forum bullet) scores
`54 passed, 36 failed, 0 skipped` on the current file.
**Do not read those two 36s as the same 36** -- the added assertions and
the failing assertions are different sets, measured by `comm` over the
two id lists, not inferred:
- 26 of the 36 *added* assertions fail against the pre-fix binary
  (`D6 D7 G1 G4 G5 G6 H1-H11 J1 J2 J3 M1 M2 M4 M5 M6 M7`);
- 10 of the 36 *added* assertions **pass** against it
  (`G2 G3 G4b G5b H11b J4 M1b M3 M8 M9`) -- mostly controls, which are
  supposed to come out the same either way;
- the remaining 10 failures are *pre-existing* assertions
  (`A9 A10 B4 B5 B6 C12 C13 C14 C16 C17`) that go red only because the
  fixes changed `viki ask`'s hit-line format.

Most new positive assertions are paired with a control that must come out
the other way, but not all: `J2`/`J3` have none (`J1` is a setup
precondition and `J4` an independent property, neither is a control for
them).

It is now wired into CI (`.github/workflows/build.yml`, job `m1`) on
linux-x86_64 and macos-arm64; the other two platforms get a matrix leg
whose only purpose is to announce, loudly and by name, that they are not
covered. That job was reproduced green end to end in a local
`debian:bookworm` (linux/amd64) container **on 2026-08-13, in the round
that wrote it, against the 54-assertion `test/m1.sh` of that moment** --
it has not been re-run since (it needs Docker plus network), so nothing
here says the 90-assertion file has ever run in a container. The workflow
itself has still never executed on GitHub's runners.

There's now also a `viki serve` local HTTP server: an HTML search page
for humans at `/`, plus a JSON API (`/api/ask`, `/api/chunk`,
`/api/health`) explicitly meant for agents/scripts to call directly,
sharing the exact same `viki_ask_query()` retrieval the CLI uses.
`viki_serve.c` itself stays loopback-only with no auth by design; for
internet exposure, `server/setup-viki-serve.sh` puts it behind the same
Caddy (TLS + Basic Auth) instance `server/SERVER_SETUP.md` already runs
for the Fossil hub, rather than hand-rolling TLS/auth in C -- see
FINDINGS.md.

## Layout

```
src/            viki CLI source (C)
  viki.c          subcommand dispatch (index / ask / serve / cache push|pull / version / ndvss-selftest / embed-selftest)
  viki_db.c/.h    local cache db: schema, ndvss static registration
  viki_index.c/.h `viki index <dir>`: walk+chunk+hash+embed checkout files, plus `fossil wiki`/`fossil ticket` subprocess extraction and `fossil sql` extraction (no CLI export exists for forum posts) for wiki pages, tickets, and forum posts (virtual paths `wiki:Name`/`ticket:UUID`/`forum:UUID`). Also INVALIDATES: retires stale `viki_source` rows, then deletes chunks nothing references from `viki_chunk` + `chunk_fts`. Read `sweep_sources()`'s scoping comment before touching that -- it is what keeps a subdirectory index, or a machine with no `fossil`, from wiping the cache
  viki_ask.c/.h   `viki ask "<query>"`: FTS5 BM25 + ndvss cosine, reciprocal rank fusion (OR-of-terms FTS query, see FINDINGS.md). Retrieval logic lives in public `viki_ask_query()` (viki_ask.h) so `viki serve` can share it; `viki_cmd_ask` is a thin CLI-printing wrapper around it.
                  Each leg scores a given `(content_hash, chunk_ix)` at most once, at its best rank (`leg_hit()`), so a cache holding several `model_id` epochs of
                  the same content -- the normal steady state of D-11 sharing -- ranks identically to a single-epoch one instead of double-counting the BM25 leg (FINDINGS.md).
                  CLI hit lines are `[<rank>] rrf=<score>  <content_hash>#<chunk_ix>  <source>` then the snippet indented 4 spaces; content_hash is the citable
                  identity (KICKOFF.md deliverable 2), the same value `/api/ask` returns as `hash` and what `/api/chunk?hash=&ix=` takes, and `<source>` is a
                  best-effort path hint that goes last because it can be `(source path unknown)` or contain spaces.
  viki_serve.c/.h `viki serve`: single-threaded POSIX-sockets HTTP server (no external HTTP library). `/` = static HTML search page (renders results via textContent, never innerHTML -- indexed content isn't trusted markup). `/api/ask`, `/api/chunk`, `/api/health` = JSON, for agents. No static-file serving (every response is generated, not read off disk), so no path-traversal surface.
  viki_cache.c/.h `viki cache push|pull`: fossil uv wrappers (subprocess). BOTH halves of D-12 travel: the embedding cache as uv blob `viki-cache.db`,
                  and the pinned model as `viki-model/{model.onnx,vocab.txt,viki-manifest.json}` -- so a fresh clone gets hybrid retrieval from one
                  endpoint with no third-party download. Model publishing is the DEFAULT (`VIKI_CACHE_NO_MODEL` is the opt-out, see viki_cache.h for why).
                  `viki-manifest.json` is the epoch pin: push skips all three model blobs when the published manifest already matches the local one, and
                  pull verifies each blob's sha256 against it before installing it. Both directions resolve the model directory through `viki_model_dir()`
                  (`$VIKI_MODEL_DIR`, else `build/dist/model`) so pull writes exactly where `viki ask` will look.
  sha256.c/.h     content_hash keying, standalone vendored SHA-256 (see FINDINGS.md -- used to be a LibreSSL
                  EVP wrapper, decoupled so viki's build has no other project as a prerequisite)
  tokenizer.c/.h  BERT WordPiece tokenization against vocab.txt (ASCII-scoped, see FINDINGS.md/tokenizer.h)
  embed.c/.h      ONNX Runtime C API: session, tokenize -> Run -> mean-pool -> L2-normalize (Windows model-path
                  handling is a real wrinkle here -- see FINDINGS.md)
build/
  build.sh        self-contained: downloads/verifies the SQLite amalgamation, ONNX Runtime, and the pinned
                  model, no other project needs to be built first (see FINDINGS.md); see its header comment
  versions.env    pins: SQLite amalgamation, onnxruntime release (4 platforms incl. Windows), and the
                  embedding model/vocab, all SHA256-verified
  forum-e2e-probe.sh  the FORUM leg's standing proof, which test/m1.sh deliberately omits. Drives
                  Fossil's own /forume1 + /forume2 to create four real artifact shapes, then
                  indexes and asks. `sh build/forum-e2e-probe.sh <empty-dir>` -> PASS=26 FAIL=0
                  against the current build/dist/viki; PASS=17 FAIL=9 against a binary compiled
                  from git HEAD's src/ (8 of those 9 are the forum fixes, the 9th is the ask
                  output-format change -- see the forum bullet), so the fix assertions are
                  provably able to fail. Its optional second argument (a viki binary) must be
                  an ABSOLUTE path; it cd's into the work dir.
                  NOT in CI -- run it by hand alongside test/m1.sh.
  m1-e2e-probe.sh the earlier, pre-test/m1.sh proof-of-recipe (PASS=37). Also runs an ENCRYPTED
                  hub and the full D-11 loop -- it, not test/m1.sh, was the first proof of both.
                  Superseded for the M1 claim (hard-coded absolute paths, not in CI, and some
                  assertions are weaker -- its C7 has no non-empty guard). Prefer test/m1.sh.
                  Also writes into the developer's real ~/.config/fossil.db (FINDINGS.md) --
                  so run it as `FOSSIL_HOME=$(mktemp -d) sh build/m1-e2e-probe.sh <dir>`, which
                  reproduces PASS=37 FAIL=0 with the real fossil.db provably untouched.
  model-uv-e2e-probe.sh  the D-12 MODEL-as-uv-blob proof, 43 checks over an encrypted hub/spoke
                  pair: publish, skip-if-unchanged, checksum verify, and a fresh clone running
                  HYBRID retrieval on the pulled model. Also NOT in CI, and it goes further than
                  test/m1.sh's M1-M9 do (those cover the happy path; this one also covers the
                  no-model hub, the unchanged-epoch skip, and a deliberately corrupted checksum).
vendor/
  fossil-see/     git submodule -- NOT a build dependency of viki itself (see FINDINGS.md); kept only so a
                  fossil-see binary is available at runtime if $VIKI_FOSSIL_BIN/PATH don't already have one
  sqlite-ndvss/   git submodule -- Warren's fork of JarkkoPar/sqlite-ndvss, statically linked (has a real
                  aarch64-Linux-only build bug -- see FINDINGS.md, CI's linux-arm64 job is marked experimental)
  download-cache/ gitignored -- cached onnxruntime tarball + model/vocab downloads (build/build.sh's fetch_verify)
test/
  m1.sh           THE Milestone 1 definition-of-done test (KICKOFF.md): 90 assertions over an encrypted
                  scratch repo -- degraded/hybrid retrieval, the two-sided vector proof, the full
                  D-11 push/fresh-clone/pull compute-once loop, the D-12 model round trip, the citable
                  content_hash, stale-content withdrawal, and mixed-epoch scoring. exit 0 == M1 met.
                  Self-contained (one mktemp tree, own FOSSIL_HOME, no state outside it). Does NOT
                  cover the forum leg on purpose -- build/forum-e2e-probe.sh owns that.
experiments/      FFI_RISK.md's proof-of-concept (in-process fossil), inherited from fossil-app.
                  FROZEN/superseded -- do not run it, do not believe it. Its build recipe needs
                  `objcopy` + GNU-ld `-Wl,--wrap=exit`, NEITHER of which exists on macOS
                  (verified: no objcopy, no llvm-objcopy, `xcrun -f objcopy` fails), and the
                  wrap requirement is obsolete upstream anyway. KICKOFF.md's "re-run
                  experiments/harness.c" gate is closed BY REFERENCE instead -- see the last
                  bullet of "Not yet built" for the argument and the measurements.
                  The live harness is ../fossil-sqlcipher-libressl/embed/harness.c (ALL PASS).
server/           hub deployment scripts: setup-hub.sh (fossil server + Caddy TLS, D-8) and
                  setup-viki-serve.sh (adds viki serve behind the same Caddy, Basic Auth) -- see
                  SERVER_SETUP.md; inherited from fossil-app (pre-encryption; see ENCRYPTION.md TODOs)
.github/workflows/
  build.yml       TWO jobs.
                  `build` -- the release matrix: linux-x86_64, linux-arm64 (experimental, see
                  FINDINGS.md), macos-arm64, windows-x86_64 (MSYS2's plain MSYS environment, not
                  MINGW64 -- see FINDINGS.md for why). Each green leg uploads build/dist/ as a
                  build artifact. Deliberately does NOT touch vendor/fossil-see: that job is the
                  standing proof viki builds standalone.
                  `m1` -- runs test/m1.sh, on linux-x86_64 and macos-arm64 ONLY. It builds
                  vendor/fossil-see first (cached, keyed on the pinned submodule commit) because
                  m1.sh hard-requires a SQLCipher fossil -- a stock `fossil` writes a PLAINTEXT
                  repo to a .efossil name and the test aborts in setup. linux-arm64 and
                  windows-x86_64 get announce-only legs named "-- NOT RUN" that print why (log +
                  ::warning:: annotation + job summary), so an uncovered platform can never read
                  as green. The job also FAILS a "PASS WITH SKIPS" run: m1.sh exits 0 for that,
                  and a runner missing sqlite3 or the model would otherwise be green having
                  skipped the entire vector proof. See FINDINGS.md.
  release.yml     Publishes a GitHub release on a v* tag: builds the same four platforms and
                  stages build/dist/ (including the bundled DLLs). Does NOT depend on the `m1`
                  job, so a tag can currently publish artifacts for a commit whose Milestone 1
                  test never ran -- a real gap, listed under "Not yet built".
```

## Build and run

```sh
# self-contained -- no other project needs to be built first (see
# FINDINGS.md). Downloads + SHA256-verifies the SQLite amalgamation,
# onnxruntime, and the model on first run, cached after that:
build/build.sh

export VIKI_MODEL_DIR=build/dist/model   # or wherever; unset/absent = BM25-only
build/dist/viki index <dir>          # walk dir, populate .viki/cache.db (relative to cwd)
build/dist/viki ask "<query>"        # hybrid top-5 (--k N to change); each hit prints
                                    #   [<rank>] rrf=<score>  <content_hash>#<chunk_ix>  <source>
                                    # then the snippet indented 4 spaces
build/dist/viki cache push [db-path] [--no-model]
                                    # publish cache + pinned model as uv blobs (run from an open
                                    #   fossil-see checkout). --no-model = cache only; the model
                                    #   leg is ON by default (viki_cache.h says why)
build/dist/viki cache pull [db-path] [--no-model]
                                    # fetch both back; the model lands in $VIKI_MODEL_DIR
build/dist/viki ndvss-selftest       # proves sqlite-ndvss is really statically linked
build/dist/viki embed-selftest [model-dir]  # proves the ONNX pipeline produces real embeddings (semantic property check)
build/dist/viki serve [--host H] [--port N]  # http://127.0.0.1:8080 by default; / = search page, /api/* = JSON
```

`viki cache push`/`pull`/`index` (for wiki+ticket extraction) need a
`fossil`-compatible binary on PATH, or set `VIKI_FOSSIL_BIN` explicitly
(e.g. to `vendor/fossil-see/build/dist/fossil-see`). Ticket extraction
additionally needs a resolvable Fossil user -- `$VIKI_FOSSIL_USER` if
set, else `$USER`, else the literal string `"viki"` (see FINDINGS.md:
`fossil ticket` commands refuse to run at all, even read-only, without
one; `fossil wiki` commands don't have this requirement).

`viki index`/`viki ask` look for the model at `$VIKI_MODEL_DIR`, falling
back to `build/dist/model` (what `build/build.sh` populates). No model
found there is not an error -- indexing/asking silently proceed BM25-only.

`viki cache push` publishes that same directory to the hub, and `viki cache
pull` writes it back to the same resolved path -- so on a fresh clone,
`VIKI_MODEL_DIR=<somewhere> viki cache pull` is all it takes to get hybrid
retrieval, with `build/build.sh` (and its ~23 MB third-party download) never
run there. Absence stays non-fatal in both directions: pushing without a
model publishes the cache alone and says so; pulling from a hub that has no
model published leaves you BM25-only and says so. A model whose bytes
disagree with the manifest's `model_sha256`/`vocab_sha256` is the one hard
failure -- pull refuses to install the epoch pin rather than hand unverified
bytes to ONNX Runtime. `build/model-uv-e2e-probe.sh <empty-dir>` proves the
whole loop (43 checks) against an encrypted hub/spoke pair.

## Test

```sh
# THE definition-of-done test. Needs a SQLCipher fossil (it creates
# *.efossil repos and asserts they are really ciphertext), so build
# vendor/fossil-see once, or point VIKI_FOSSIL_BIN at another one:
git submodule update --init --recursive vendor/fossil-see
vendor/fossil-see/build/build.sh

bash test/m1.sh                             # exit 0 AND "0 failed, 0 skipped"
VIKI_TEST_KEEP=1 bash test/m1.sh            # keep the scratch tree for post-mortem
sh build/forum-e2e-probe.sh <empty-dir>     # the forum leg, which m1.sh omits
```

`test/m1.sh` needs nothing else from the environment: it sets its own
`FOSSIL_HOME`, `FOSSIL_SEE_KEY`, `FOSSIL_USER`/`VIKI_FOSSIL_USER` and
scratch tree, and overrides hostile inherited values of all of them
(measured). `VIKI_BIN`, `VIKI_FOSSIL_BIN` and `VIKI_MODEL_DIR` are the
only knobs; each defaults to this repo's own build output.

**Read the count line, not the exit status.** m1.sh exits 0 for a run
that skipped assertions, and *which* dependency is missing decides how
many. All four measured 2026-08-13 against the same binary, same file:

- model + `sqlite3`, the full run: `90 passed, 0 failed, 0 skipped`, exit 0
- no model, `sqlite3` present: `64 passed, 0 failed, 26 skipped`, exit 0
- model present, no `sqlite3` on PATH: `80 passed, 0 failed, 10 skipped`, exit 0
- neither: `57 passed, 0 failed, 33 skipped`, exit 0

The skip sets overlap (`B2`, `C11`, `J1` need both), so the two
degradations do not add. Note also that the no-model run keeps **half**
the vector proof: `A12` ("semantic query finds NOTHING without a model")
runs and PASSES there -- it is the half whose premise is *absence* -- and
only `B5` is skipped. `RESULT: PASS WITH SKIPS` is not a Milestone 1 pass
and says so; CI greps for `0 failed, 0 skipped`.

**And check the binary is current before believing a green run.**
`test/m1.sh` does not rebuild anything -- it tests whatever
`build/dist/viki` is, which is gitignored and can lag `src/` arbitrarily.
It has already scored a full green -- 54/54, in the 54-assertion era of
this file -- against a binary missing three fixes that were sitting in
`src/` at the time. See FINDINGS.md.

## Verified working end to end (not just "should work")

- **The whole Milestone 1 definition of done, on an ENCRYPTED repo:
  `test/m1.sh`, `90 passed, 0 failed, 0 skipped`, exit 0** (re-run
  first-hand 2026-08-13, not carried over from a report).
  Be precise about what is new here, because this file previously
  overstated it in the other direction: the *encrypted* M1 loop was first
  proven by `build/m1-e2e-probe.sh` (`PASS=37 FAIL=0`, last measured
  2026-08-13 19:50), which was undocumented, while the hub/spoke
  bullet at the bottom of this section described an older, weaker,
  **plaintext** `.fossil` loop. `FOSSIL_SEE_KEY` did not appear anywhere
  in AGENTS.md or CLAUDE.md until this pass. What `test/m1.sh` adds over
  that probe is that it is portable (no hard-coded absolute paths, so it
  runs on any checkout and is what the CI `m1` job invokes), it is
  hermetic, and several of its
  assertions are strictly stronger -- e.g. the probe's C7 compares two
  embedding fingerprints with no non-empty guard, so two empty reads would
  pass it; m1.sh's C11 requires `[ -n "$SPOKE_FP" ]` first.
  The encryption is asserted, not assumed: the hub's
  first 15 bytes are not `SQLite format 3` (E1), a plaintext control repo
  built by the *same binary and key* fails that same check so it is
  provably able to fail (E2), a stock `sqlite3` cannot open the hub but
  can open the control (E3/E3b), and a wrong `FOSSIL_SEE_KEY` is rejected
  non-interactively (E4). On top of that: degraded-mode and hybrid-mode
  retrieval of the same planted answer, the two-sided vector proof (a
  semantic query with no keyword overlap finds *nothing* without a model
  and the right document with one), and the full D-11 push / fresh-clone /
  pull compute-once loop.
  It is not vacuous, verified by injection rather than by inspection.
  Both injections re-measured against the 90-assertion file on
  2026-08-13, using a shell wrapper that forwards everything else to the
  real binary: making `cache pull` a silent exit-0 no-op turns it
  `76 passed, 14 failed` (`C0 C9 C10 C11 M4 M5 M6 M7 C12 C13 C14 C16
  C17 C18`); forcing `ask` into degraded mode while indexing normally
  turns it `83 passed, 7 failed` and reddens the rung-2 assertions and
  their dependents (`B3 B5 B6 B7 M7 C16 J4`). (The earlier `44/10` and
  `49/5` figures were the same two injections against the 54-assertion
  file; they are history, not current.)
  Hermetic: three consecutive runs byte-identical, passes from a
  different cwd, leaves no `~/.fossil`, no `.viki`, no scratch tree.
  It deliberately does NOT cover the forum leg -- treat it and
  `build/forum-e2e-probe.sh` as a pair.

- **KICKOFF.md deliverable 2 ("results with source content_hash") is now
  actually met on the CLI and the JSON API** -- and it was not before
  2026-08-13, which is the honest half of this bullet: a hit line whose
  source path was unknown printed `(source path unknown)#0`, i.e. a
  result with nothing citable in it at all. Every hit now leads with
  `<content_hash>#<chunk_ix>`. Proven by `test/m1.sh` `G1`-`G6` in the
  green run above: the printed hash is 64 lowercase hex, equals an
  independent `shasum -a 256` of the file, and addresses a real row in
  `.viki/cache.db` -- with controls `G4b` and `G5b`, the latter flipping
  one hex character to show the db lookup discriminates. **Not** met on
  the one human surface: `viki serve`'s HTML page still renders only
  rank/rrf/source/chunk_ix (see "Not yet built").
- **KICKOFF.md deliverable 3 ("embedding cache + model file as fossil
  unversioned files") is now met for both halves.** `viki cache
  push`/`pull` move `viki-cache.db` **and**
  `viki-model/{model.onnx,vocab.txt,viki-manifest.json}`. Proven by
  `M1`-`M9` in the green run above: the blobs are published under exact
  names, come back byte-identical, are sha256-verified against the epoch
  pin before install, and a **fresh clone that never indexed anything**
  runs *hybrid* retrieval on the pulled model while its only sync peer is
  a local `file://` hub (`M9`) -- with `M8` (empty model dir -> degraded,
  no results) as the control. `build/model-uv-e2e-probe.sh` covers the
  same loop in 43 checks including the failure paths (`0 failed`, last
  measured 2026-08-13 19:50; not re-run since).

- `build/build.sh` produces `build/dist/viki` cleanly (only benign
  unused-variable warnings from sqlite-ndvss's own NEON kernels, not viki
  code) -- including downloading, SHA256-verifying, and linking against
  ONNX Runtime, and downloading + verifying the pinned embedding model.
- `viki index` + `viki ask` on a scratch directory with planted content:
  correct chunk found, correctly ranked, snippet highlights the matched
  terms (BM25 path).
- `viki ndvss-selftest`: confirms real static linking (`ndvss instruction
  set: neon` on this Apple Silicon machine -- runtime CPU dispatch, not a
  stub).
- `viki embed-selftest`: confirms the ONNX pipeline produces semantically
  meaningful embeddings, not just "didn't crash" -- checks that a related
  sentence pair cosine-scores clearly higher than an unrelated pair
  (observed 0.64 vs. -0.05).
- **Semantic-only retrieval, no keyword overlap at all**: `viki ask
  "livestock standing by a drinking pool"` against a 3-document corpus
  (horses/water-trough, a grocery list, a tax note) correctly ranked the
  horses document first, despite the query sharing zero literal words
  with it -- the ranking can only have come from the vector leg, proving
  hybrid retrieval is real, not BM25 wearing a costume.
- **Wiki + ticket extraction, against a real repo**: created a wiki page
  and a ticket in a scratch `fossil-see` repo, ran `viki index`, and
  `viki ask` correctly retrieved each with the right virtual-path
  attribution (`wiki:PumpMaintenance`, `ticket:<uuid>`) and correctly
  ranked whichever source best matched a semantically-phrased query.
  Along the way, found and fixed a real ticket-content corruption bug
  (see FINDINGS.md: `strtok_r` collapses empty TSV fields).
- **Forum post extraction: round-tripped against live posts; the three
  bugs that found are fixed in `src/viki_index.c` AND in
  `build/dist/viki`.** The gap this bullet used to describe ("no live
  forum post has been round-tripped through `index_forum()`") is closed:
  real forum posts, created by Fossil's own `forum_post()` via a headless
  `fossil http` POST to `/forume1` (`/forume2` for replies/edits), were
  indexed and retrieved by `viki ask` under `forum:<uuid>` -- thread-start
  posts and replies both, on an encrypted repo, with the vector leg proved
  two-sidedly (a query with no keyword *stem* overlap returns nothing in
  degraded mode and forum hits in hybrid mode). The blocker the first
  attempt hit was **a missing `Referer:` header**, not an AJAX UI.
  `build/forum-e2e-probe.sh` is the runnable end-to-end proof: `PASS=26
  FAIL=0` against the current `build/dist/viki`, `PASS=17 FAIL=9` against
  a binary compiled from git `HEAD`'s `src/` -- say it that way, because
  the older figure `18/8` in this file was attached to the bare phrase "a
  pre-fix binary" and the two are not interchangeable (see the resolved
  caveat below).
  The three defects, all fixed in `src/viki_index.c`:
  1. **The `H` title card was indexed with Fossil's escaping intact**, so
     every title word after the first was unsearchable. Fixed by calling
     `unquote_fossil()` (byte-identical to Fossil's `defossilize()`).
     `index_wiki()` was never affected (`fossil wiki export` returns
     rendered text, not a manifest).
  2. **Edited posts were indexed twice, superseded text served as
     current.** Fixed by excluding `forumpost.fprev` in the selector.
  3. **`find_line_card()` scanned past the card region into the post
     body**, so a reply (which has no `H` card) adopted any body line
     starting `"H "` as its title. Fixed by bounding the scan at the W
     card's payload. This one was new -- see FINDINGS.md for why two live
     posts weren't enough to expose it and four were.
  **Caveat RESOLVED (2026-08-13):** `build/dist/viki` has now been
  rebuilt, so the shipped binary carries all three fixes. Confirmed the
  binary really changed rather than assuming it: `strings build/dist/viki
  | grep -c fprev` was `0` before and is `1` now, and
  `sh build/forum-e2e-probe.sh <empty-dir>` is `PASS=26 FAIL=0` against
  it.
  **Which "pre-fix binary" -- this file used the phrase for two possible
  builds and pinned a number to the one nobody can rebuild:**
  1. *forum-fix-only* (`src/viki_index.c` reverted, everything else as it
     stood when the forum round ran) scored `PASS=18 FAIL=8` against the
     probe **as the probe was then**. That is the origin of the 18/8
     figure. It is a working-tree state that no longer exists; nothing in
     the repo can rebuild it.
  2. *all of git `HEAD`'s `src/`* -- i.e. before all three of this
     round's fixes -- scores `PASS=17 FAIL=9`, re-measured 2026-08-13
     from a freshly compiled binary (`git show HEAD:src/*` into a scratch
     dir, compiled against read-only copies of `build/obj/sqlite3.o` and
     `sqlite-ndvss.o`, never touching `build/obj`; recipe in FINDINGS.md).
     Failures: `B4 B5 B6 B7 C1 C2 C3 C4 C6`.
  The probe still has 26 assertions either way (`18+8 = 17+9 = 26`) and
  the two failure sets differ by exactly `C6` -- the *attribution*
  assertion, rewritten this round for `viki ask`'s new
  `<content_hash>#<ix>  <source>` hit line, so a HEAD binary fails it for
  a reason unrelated to forum parsing. The delta is the probe moving, not
  the binary: eight of the nine are still the forum-fix-specific ones.
  **Still NOT fully verified, and this is the weakest of the four content
  types -- do not read the above as "forum indexing is done":**
  - **Four artifact shapes were exercised, not the type's full grammar** --
    thread-start, reply, card-shaped reply, edit. Post *deletion*
    (`nullout=1`), moderation-pending posts, and edit chains longer than
    one hop are all untested, and the reason bug 3 existed at all is that
    two shapes were not enough to expose it.
  - **`test/m1.sh` does not touch the forum leg at all**, by design -- it
    plants no forum post and asserts nothing about one either way. All 8
    occurrences of `forum` in it sit in one comment block (currently
    lines 415-427, headed "DELIBERATELY NOT PLANTED: a forum post")
    explaining that scope call; grep for that heading rather than
    trusting the line numbers, which move whenever the file grows. The definition-of-done test is green
    whether forum indexing works or not, so a forum regression can only be
    caught by running `build/forum-e2e-probe.sh` as well.
  - **The probe is not in CI**, unlike `test/m1.sh`. It is a local,
    manually-run proof.
- **`viki serve`**: started the server against the same scratch repo used
  for the wiki/ticket tests and hit every route with `curl`: `/api/health`
  reports mode+model_id correctly; `/api/ask?q=...&k=...` returns the
  same ranked hits `viki ask` does (now provably the same code path, not
  just "should be the same" -- both call `viki_ask_query()`), including
  `source` (`wiki:PumpMaintenance`, `./docs/note.md`); `/api/chunk?hash=&ix=`
  round-tripped a hash from an `/api/ask` response to its full chunk
  text; missing `q` on `/api/ask` -> 400 JSON, an unknown hash on
  `/api/chunk` -> 404 JSON, an unknown route -> 404 JSON, `POST` ->
  405 JSON; `/` serves the HTML search page. Not tested: the page's own
  JS search flow in an actual browser (only curl'd the routes it calls)
  -- worth a manual click-through before calling the UI itself done.
- Full hub/spoke/fresh-clone loop, using real `fossil-see`-built repos
  connected via `file://` sync: index + push from a spoke, then a
  **completely fresh clone that never ran `viki index`** pulls the cache
  and `viki ask` immediately returns the correct planted answer. This is
  the D-11 "compute once, share via `fossil uv`" claim, actually proven,
  not assumed.
  This bullet's original caveat -- "predates the embedding pipeline,
  worth re-running with a model present to confirm embeddings round-trip
  through `fossil uv`; not yet done" -- **is now done**, on an encrypted
  hub rather than the plain `.fossil` one used here. It was first done in
  `build/m1-e2e-probe.sh` (C7) and is now covered by `test/m1.sh`, three
  ways rather than one:
  - C10 -- the pulled cache db is **byte-identical** to the pushed one
    (`cmp -s`), which needs no `sqlite3` and is the strongest available
    statement that the uv round trip is lossless;
  - C11 -- states it in D-11's own terms: an `sqlite3` fingerprint over
    `(content_hash, hex(substr(embedding,1,8)))` for every non-NULL
    embedding, ordered, matches on both sides -- and, unlike the probe's
    C7, requires that fingerprint to be non-empty, so two failed reads
    cannot pass it by both being blank;
  - C16 -- the *pulled* vectors actually work: a semantic-only query
    with no keyword overlap ranks the witness document 1 in a clone that
    never indexed anything, while C15 confirms that same query finds
    nothing when no model is present.
  - Honest limit: **C11 skips** (and prints a SKIP line) when either the
    model or a stock `sqlite3` is missing; C10 and C16 do not.

## Not yet built (see KICKOFF.md's full Milestone 1 spec for what's still owed)

- **`release.yml` does not depend on the `m1` job.** A `v*` tag can publish
  release artifacts for a commit on which the Milestone 1 definition-of-done
  test never ran. The `m1` job exists and gates `main`, but nothing gates a
  release on it. Found 2026-08-13 by the coverage review; not yet fixed.
- **`viki index sub` and `viki index ./sub` are different keys.** The path
  spelling passed on the command line is stored verbatim in `viki_source`, so
  re-indexing the same directory under a different spelling re-keys every row,
  reports a misleading non-zero "stale source(s) retired" count, and changes
  the `source` string `viki ask` attributes results to. No data is lost (the
  content_hash is unchanged, so `gc_orphan_chunks()` removes nothing) and the
  retired counter is the only wrong output, but the source attribution wobbles
  for no real reason. Normalizing the walk root before keying would fix it.

- **`viki-manifest` epoch *migration* mechanism -- deliberately POST-M1,
  not an M1 hole.** Judgment call made 2026-08-13; recorded here so it is
  not re-relitigated as unfinished M1 work.
  **What KICKOFF.md item 1 actually asks for is done**: "the pinned model
  named in `viki-manifest` (create the manifest; pick a well-supported
  quantized MiniLM-class model; record checksum)". `build/build.sh` writes
  `build/dist/model/viki-manifest.json` with `model_id`, `dim`,
  `model_sha256` and `vocab_sha256`, and it is *load-bearing rather than
  decorative*: `read_manifest()` in `src/embed.c` parses it at startup, a
  missing/bad manifest is what puts viki in degraded BM25-only mode, and
  the `model_id` it yields is what stamps every `viki_chunk` row and what
  `viki_ask.c` filters the vector leg by.
  **The storage side of two-epoch coexistence is already there** --
  `PRIMARY KEY(content_hash, model_id, chunk_ix)` (`viki_db.c`) plus the
  `WHERE model_id=?1` filter (`viki_ask.c`) means vectors from two models
  can sit in one cache without colliding or being mixed at query time.
  The keyword leg deliberately does NOT filter by `model_id` (a chunk may
  exist only under `model_id='none'`, and an asker with no model must still
  search a peer-built cache); it collapses the duplicate per-epoch FTS rows
  instead, so a two-epoch cache scores exactly like a one-epoch cache. That
  was a real bug until 2026-08-13 -- see FINDINGS.md.
  **What is missing is only the migration driver**, and it is
  VIKI_DESIGN.md's "Epochs" section, not KICKOFF.md's M1 list: nothing
  detects that the pin moved, nothing re-embeds existing content under the
  new `model_id`, and nothing GCs the old epoch's vectors afterward. Today
  a pin bump means new content is embedded under the new id while old
  content stays queryable only under the old one.
  **Why post-M1/M2 is the right scope, not laziness:** D-11 pins *one*
  universal model precisely so an epoch bump is a rare, fleet-wide,
  deliberately-scheduled event -- it has never happened once (`VIKI_MODEL_ID`
  has exactly one `+` line in the entire history of `build/versions.env`
  and has never been changed) -- and D-10
  makes the whole layer disposable, so the working fallback is "delete
  `.viki/cache.db` and re-index", which costs minutes at personal scale.
  Building migration machinery before a second model exists means
  designing against an imaginary `model_id`. Two smaller gaps go with it,
  both cheap and both waiting on the same trigger: the manifest is
  narrower than VIKI_DESIGN.md's spec (no chunking params, no ndvss/schema
  version, no model uv-path) and it is generated into gitignored
  `build/dist/`, not committed as the "small versioned file" that doc
  describes. Revisit all of this together when a model change is actually
  proposed.
- ~~Cross-arch model verification~~ **Done, as of the CI round of
  2026-08-13.** The pinned model is the `arm64` quantization recipe,
  picked originally because it could only be verified on this arm64 dev
  machine. `build/build.sh` ends by running `ndvss-selftest` and
  `embed-selftest` (lines 283-284), so every CI `build` leg runs the same
  semantic-property check -- and it was **observed green on linux-x86_64
  and windows-x86_64 in the GitHub Actions runs of 2026-08-13** (the
  round that produced commit 478b8e3). That observation needs network and
  has **not** been re-confirmed since; nothing on this machine can
  re-check it, so treat it as dated evidence, not a standing property.
  `build/versions.env`'s caveat about this tension is resolved in
  practice, if not in the design (still one universal pinned model, not
  per-arch).
- ~~Windows binary depends on `msys-2.0.dll` at runtime, not bundled~~
  **Done** (commit 478b8e3, 2026-08-13 -- this bullet outlived its own fix
  by a day; see FINDINGS.md). `viki.exe` is still an MSYS2 plain-MSYS
  binary, not a native Windows one -- that is required for `fork()`/BSD
  sockets (see FINDINGS.md) and is not changing -- so it does link
  `msys-2.0.dll`. But that DLL now ships beside it. `build/build.sh`'s
  Windows branch copies it (`command -v msys-2.0.dll`, else
  `/usr/bin/msys-2.0.dll`, else a loud `WARN:` that the binary will only
  run where MSYS2 is installed), and both `.github/workflows/build.yml`
  and `release.yml` stage `build/dist/*.dll` wholesale rather than naming
  `onnxruntime.dll` alone.
  Verified from the *published artifact*, not from reading the scripts --
  **once, on 2026-08-13**, and not since (it needs network, and artifacts
  expire after 30 days): `gh run download 31723892765 -n
  viki-windows-x86_64` then yielded `viki.exe`, `msys-2.0.dll`
  (3,367,041 bytes), `onnxruntime.dll`, `onnxruntime_providers_shared.dll`,
  and `model/`. Re-download a recent green run before repeating the claim.
  Still unverified, and nobody here can verify it: that this artifact
  actually *starts* on a Windows machine with no MSYS2 installed. There is
  no Windows dev machine in this project -- CI is the only Windows.
- **Tech notes / other artifact types** aren't indexed (checkout files,
  wiki pages, tickets, and forum posts are, so far -- forum indexing is
  verified end to end against live posts and the three bugs found that
  way are fixed in both `src/` and `build/dist/viki`, but see the "still
  NOT fully verified" list above before trusting the forum leg).
- **`viki serve`'s HTML page still shows no `content_hash`, so the one
  HUMAN surface cannot cite a source.** The CLI hit line and `/api/ask`'s
  `"hash"` both carry it (KICKOFF deliverable 2), but `viki_serve.c`'s
  embedded page renders `'#' + hit.rank + '  rrf=' + ... + hit.source +
  ' (chunk ' + hit.chunk_ix + ')'` and nothing else -- the hash is in the
  JSON the page already fetches, simply not displayed. Nothing in
  `test/m1.sh` covers `viki serve` at all.
- **`VIKI_FTS_EPOCH_SLACK = 4` (`src/viki_ask.c:16`) is a bounded
  heuristic.** The BM25 leg over-fetches `poolSize * 4` rows and stops at
  `poolSize` *distinct* chunks, which is what makes a multi-epoch cache
  score like a single-epoch one. A cache holding more than 4 epochs of
  the same chunk can therefore hand the fusion fewer candidates than it
  asked for: scores stay correct, deep-tail recall shrinks. Untested --
  `test/m1.sh` never builds more than 2 epochs. The constant's own
  comment concedes this; it had appeared in no doc until now.
- **`viki serve`'s search page isn't linked back to Fossil's own web UI.**
  Results show `source` (e.g. `wiki:PumpMaintenance`, `./docs/note.md`)
  as plain text, not as a clickable link into `fossil-see`'s web UI for
  that wiki page/ticket/file/forum post -- would need either a configured
  base URL for the Fossil web server or `viki serve` proxying to it.
  Not started.
- **No browser-driven click-test of `viki serve`'s `/` page's JS.** The
  HTTP routes it calls were verified directly with `curl` (see "Verified
  working" above), but the page itself hasn't been exercised in an
  actual browser.
- **`viki serve` is single-threaded, blocking accept().** Fine for one
  local user; a second concurrent request queues behind whatever's being
  handled (each request is fast -- ms-scale DB queries -- so this is
  unlikely to matter for the personal-tool use case it's built for, but
  it's a real limit, not a hidden one).
- **Chunking is naive**: fixed 40-line splits, no overlap, no token
  awareness (and no truncation-awareness of the model's 512-token limit
  for very long lines). Fine for now; revisit before relying on it for
  real retrieval quality.
- **Tokenizer is ASCII-scoped**: no Unicode NFD accent stripping, no CJK
  per-character splitting (both part of the reference BERT basic
  tokenizer). Non-ASCII text degrades to more `[UNK]` tokens rather than
  being mis-tokenized silently wrong, but this is a real accuracy gap on
  non-English content. See `tokenizer.h`.
- ~~Garbage collection of orphaned chunks~~ **Done -- and this bullet's
  own claim was wrong.** It used to read: "When a file's content changes,
  the old content_hash's chunk rows are never deleted (harmless --
  content-addressed, rebuildable, D-10 -- but will accumulate)."
  "Harmless" is **disproven**. It was never merely an accumulation
  problem: nothing invalidated `viki_source` either, so withdrawn content
  stayed at **rank 1** in `viki ask` indefinitely. Measured on a two-file
  corpus (full repro in FINDINGS.md): after rewriting `docs/barn.md`, the
  replaced text still came back rank 1; after `rm docs/tax.md`, the
  deleted file came back rank 1 under its own full path. For a memory
  tool that is a retrieval-correctness bug, not a housekeeping debt.
  `viki index` now retires stale `viki_source` rows and then deletes
  chunks nothing references, from `viki_chunk` **and** `chunk_fts`.
  The **scoping rule is the hard part** and is documented at
  `sweep_sources()` in `src/viki_index.c`: a run invalidates only
  namespaces it can prove it observed -- filesystem paths beneath the
  directory it actually walked (so `viki index docs` cannot touch anything
  outside `docs/`), and `wiki:`/`ticket:`/`forum:` only when that
  extractor exited 0. That exit-status check is what stops a machine with
  no `fossil` binary from deleting every wiki page, ticket and forum post
  in the cache -- empty output otherwise looks identical to "this repo has
  none". Verified with over-deletion controls, not just the happy path:
  subdirectory indexing leaves outside rows and chunks intact; a shared
  `content_hash` survives while any path still references it; and with
  `VIKI_FOSSIL_BIN` pointed at nothing, all three virtual namespaces
  survive byte-for-byte (`0 stale source(s) retired`) while the run says
  so out loud on stderr. It also fixes a case the forum `fprev` filter
  could not reach: a post indexed while it *was* current, then superseded
  by an edit, used to stay retrievable forever.
  **Honest limits** (all "keeps too much", never "deletes too much"):
  a row recorded under an absolute path is not swept by a relative
  `viki index .`; a `..` anywhere in a path puts it out of scope; and a
  directory that fails to open suppresses the sweep for that whole
  subtree.
  **And one that is not about the sweep at all: staleness detection is
  `mtime`-only.** `viki index` skips a file whose `(path, mtime)` matches
  the `viki_source` row and never hashes it, so a document *rewritten
  within the same second as its last index* is invisible -- `0
  (re)chunked, 0 stale source(s) retired`, and the replaced text still
  answers at rank 1. This is a real limitation of `viki index`, not just
  a test artifact (FINDINGS.md, "`viki index` trusts mtime alone", with
  the repro); it is also
  why every mutation in `test/m1.sh` section 7 is followed by
  `touch -t 202001010000`. Sub-second edits, and edits that preserve
  mtime, are missed until something else touches the file.
  Also note a real consequence: a cache **pulled** from a peer
  (D-11/D-12) and then re-indexed in a tree that lacks those files will
  retire their rows -- correct for the local view, but it discards the
  peer's compute-once work, so do not `viki index` in a fresh clone you
  only meant to query.
- **In-process Fossil (FFI) is not started -- and KICKOFF.md's
  `experiments/harness.c` regression gate is CLOSED BY REFERENCE, not
  outstanding.** Judgment call made 2026-08-13; this is the resolution, so
  do not re-open it as a failing definition-of-done item.
  KICKOFF.md's definition of done says "Re-run `experiments/harness.c`
  against your fossil-see build -- still ALL PASS (regression gate)". It
  is satisfied as follows:
  1. **viki has no in-process Fossil to regress.** Every Fossil operation
     is a subprocess resolved by `viki_fossil_binary()`
     (`$VIKI_FOSSIL_BIN` -> `fossil-see` on PATH -> `fossil`), used in
     `viki_cache.c` and `viki_index.c`. Measured, not asserted from
     design -- and measured over the **full** symbol table, because
     `nm -u` lists only *undefined* symbols and statically linked code
     contributes *defined* ones, so an empty `nm -u` would prove nothing
     about static linking (see FINDINGS.md):
     `nm build/dist/viki | grep -i fossil` prints exactly three symbols,
     all viki's own -- `_unquote_fossil` (local), `_viki_fossil_binary`,
     `_viki_fossil_user`. No Fossil implementation is present in the
     binary in any linkage form, and it contains neither
     `fossil_cli_main` nor `fossil_embed_init`. Nothing that harness
     covers is on viki's critical path.
  2. **The live harness IS all-pass, in the repo that owns it.** It moved
     to the sibling `fossil-sqlcipher-libressl` repo's `embed/` directory;
     `embed/README.md` records `harness.c --net URL` (local phase, then a
     second networked in-process repo) as **ALL PASS, 0 failures**,
     verified from a clean-room rebuild rather than the working tree the
     fix was developed in.
  3. **The `experiments/` copy cannot be run here anyway**, so re-running
     *it* would prove nothing about anything current. Its own header
     recipe needs `objcopy --redefine-sym` and GNU-ld `-Wl,--wrap=exit`;
     on this machine `which objcopy`, `which llvm-objcopy` and
     `xcrun -f objcopy` all fail (re-verified 2026-08-13). And the
     `-Wl,--wrap=exit` requirement is itself obsolete -- see item 2 of the
     update below.
  What remains genuinely not-built is the FFI *work*, which KICKOFF.md
  puts out of scope for M1 anyway. **Before resuming it, read
  `../fossil-sqlcipher-libressl/embed/README.md`, NOT
  `./FFI_RISK.md`** -- the details below say why. (`FFI_RISK.md` lives at
  the **repo root**, not in `experiments/`; `experiments/` holds only
  `db-embed.patch` and `harness.c`. This file said `experiments/FFI_RISK.md`
  three times and was wrong three times.)
  **Update (2026-08-13):** the embedding foundation this depends on moved
  significantly since `FFI_RISK.md` was written -- that file
  and `experiments/harness.c`/`db-embed.patch` are now a frozen snapshot,
  **not current truth**. The live version lives in the sibling
  `fossil-sqlcipher-libressl` repo's `embed/` directory (promoted there
  specifically so any consumer, not just this one, builds on it), and has
  three real bugs found and fixed since this snapshot, verified via a
  clean-room rebuild each time:
  1. The delete-on-failure carryover bug (already known here too).
  2. `fossil_exit()` now traps via a registered handler
     (`fossil_embed_init()`) instead of GNU ld's `-Wl,--wrap=exit` --
     portable to Apple's linker, closing the "Exit trap without GNU ld"
     item this file used to list as remaining work.
  3. **The cross-repo bug this bullet used to describe as open
     ("`db_repository_filename`'s `zRepo`... cross-repo switching in-process
     misbehaves") is now fixed.** Root cause: that `zRepo` was a
     function-local `static`, memoizing the first repository ever opened
     in the process for the life of the process -- every later command
     against a *second* in-process repo silently operated on the first
     repo's data instead. Fixed by promoting it to file scope and adding
     `fossil_reset_repository_filename_cache()`, called by the embedding
     shim after every command (same pattern as the fatal-guard fix below).
     `harness.c --net URL` (local phase, then a second, networked,
     in-process repo: clone/open/add/commit/push/pull/sync) is now ALL
     PASS, 0 failures.
  A sibling bug of the same class (`create_admin_log_table()`'s `once`
  guard) was found but deliberately left unfixed -- narrower blast radius,
  not yet exercised by any test. **Before resuming FFI/embedding work on
  viki, read `../fossil-sqlcipher-libressl/embed/README.md` in full**
  rather than `./FFI_RISK.md` -- it has the current bug list,
  the required shim rules (which grew since this snapshot: two more
  `fossil_reset_*()` calls are now required per command, not just
  `db_clear_delete_on_failure()`), and what's still genuinely open
  (output capture design, iOS cross-compilation, `libfossilsee`
  packaging) before this is viable to wire into viki for real.
