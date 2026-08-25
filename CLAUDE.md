# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Orientation

**Read `SCOPES.md` when you are about to add something and are not sure where
it goes.** It is the four-level split -- state, projections, interfaces,
connectors -- and the one-line test that decides: *can viki compute this without
an opinion?* If not, it is a connector and does not belong in `src/`. That
boundary got crossed twice in a single day before it had a name, and both times
the code was reasonable and the placement was not.

**Read `AGENTS.md` first.** It is the current-state snapshot — what exists, what
is verified end to end, and what is not yet built — and is kept current in the
same commit as any code change that makes it stale. `KICKOFF.md` has the full
Milestone 1 brief and the process rules; `FINDINGS.md` records every surprise
with a repro.

Docs are source of truth here. When a design doc conflicts with reality, update
the doc **in the same commit** as the code. Anything surprising you discover
goes into `FINDINGS.md` (newest entry at the top, `## <one-line claim>` heading,
with the repro and the wrong assumption it replaces).

Settled decisions live in `USER_STORIES.md` as D-1..D-12 — do not relitigate
them. The ones that constrain code most:

- **D-10** Vectors are projections. Embeddings/FTS tables are derived,
  rebuildable, disposable; never source of truth. Content lives as Fossil
  artifacts.
- **D-11** One pinned rung-2 model, universal across peers, which makes an
  embedding a deterministic function of `(content_hash, model_id, chunk_params)`
  — computed once by whoever sees the content first, then shared.
- **D-12** The cache db and the model file distribute as Fossil *unversioned*
  files (`fossil uv`), never as versioned artifacts.
- **D-9** Fossil is pinned at 2.28 (SQLCipher baseline is SQLite 3.53.1).

## Capturing and structuring notes

There is a browser UI for the whole loop at `http://127.0.0.1:8080/capture`
once `viki serve` is running, and a JSON API behind it (`/api/pending`,
`/api/notes`, `/api/capture`, `/api/structure`, `/api/reindex`). Mutating
routes are POST-only and need an `X-Viki-Local` header -- a cross-origin
guard, not authentication.

`viki capture "..."` records raw text with no model, network or fossil
needed. `viki structure --pending` is the work queue for adding judgement,
and `viki structure <id> --type ... --place ...` writes it back.

The `@type` vocabulary and the rules for applying it live in **AGENTS.md,
"Structuring captures"** -- primary copy there, deliberately not duplicated
here, because this repo has had the same claim rot in one of two files three
times. Read it before draining `--pending`; the short version is that only
`task` belongs in "what needs to be done", abbreviations get expanded into
`--place` at structure time, and `--closes` is how one note retires another.

## Build and run

There is no Makefile. `build/build.sh` is the whole build — self-contained, no
other project needs to be built first. It downloads and SHA256-verifies the
SQLite amalgamation, ONNX Runtime, and the pinned model (all pinned in
`build/versions.env`, cached in `vendor/download-cache/`), compiles, links, and
ends with a smoke test.

```sh
git submodule update --init vendor/sqlite-ndvss   # the only build-time submodule
build/build.sh                                    # -> build/dist/viki

export VIKI_MODEL_DIR=build/dist/model   # unset/absent => BM25-only, not an error
build/dist/viki index <dir>              # walk + chunk + hash + embed into .viki/cache.db
build/dist/viki ask "<query>" [--k N]    # hybrid top-5; each hit prints
                                         #   [<rank>] rrf=<score>  <content_hash>#<chunk_ix>  <source>
                                         # then the snippet indented 4 spaces, carrying
                                         #   <<document continues above/below>> / <<excerpt truncated>>
build/dist/viki grep "<regex>"           # exact POSIX-ERE regex over every indexed chunk,
                                         #   including artifacts `rg` cannot see (ckin:, wiki:,
                                         #   ticket:, forum:, note:, attach:, uv:). ERE not PCRE.
                                         #   Same hit-line shape as `ask` minus the score, same markers
build/dist/viki muse [--k N] [--seed N] [--from <hash>#<ix>]
                                         # undirected recall: NO query. Returns chunks from
                                         # the MIDDLE of a random seed chunk's cosine band.
                                         # Needs vectors in the cache but NOT model.onnx;
                                         # refuses a BM25-only cache rather than faking it.
                                         # --seed replays a run exactly.
build/dist/viki promises [--me NAME] [--horizon 7d] [--all]
                                         # THE LEDGER: what is owed, to whom, by when, what is
                                         # at risk. LIVE tasks only -- anything a later note
                                         # `--closes` is excluded, which is the property a
                                         # ledger cannot do without. Ordered by due, not by
                                         # write time. States its own coverage every run.
build/dist/viki sql "<SELECT ...>" [--json]
                                         # THE RAW SURFACE. Read-only SQL over the cache with
                                         # the vector function registered -- and this is the
                                         # ONLY place it exists: a stock `sqlite3` opening
                                         # cache.db gets `no such function:
                                         # ndvss_cosine_similarity_f`, so without this an agent
                                         # cannot do a vector query at all and `ask` is the
                                         # only door. READONLY is structural (SQLite enforces
                                         # it), not parsed; a refused write reports and exits 1
build/dist/viki coverage [--json]         # which sources fed the corpus and when each was
                                         # last seen. A QUERY WITH NO JUDGMENT IN IT: no
                                         # staleness threshold, no advice. The moment it
                                         # needs a number like "12 hours" it is policy, and
                                         # policy lives in assistant/ -- see below
build/dist/viki why <note-id>            # the supersession chain BOTH ways: what this replaced
                                         # and what replaced it. What an agent reads BEFORE
                                         # starting, so it does not redo a superseded attempt
build/dist/viki serve [--host H] [--port N]   # 127.0.0.1:8080; / = HTML page, /api/* = JSON
build/dist/viki cache push|pull [db-path] [--no-model]
                                         # fossil uv add/sync/export -- moves the embedding
                                         # cache AND the pinned model ($VIKI_MODEL_DIR), so a
                                         # fresh clone gets hybrid retrieval from the hub alone.
                                         # The model leg is ON by default; --no-model opts out
                                         # (viki_cache.h explains why that polarity)
```

`vendor/fossil-see` is **not** a build dependency — it is a submodule only so a
fossil-compatible binary is available at runtime. Leave it uninitialized unless
you need it (CI does).

`.gitmodules` gives `vendor/sqlite-ndvss` an SSH URL; CI rewrites it with
`git config --global url."https://github.com/".insteadOf "git@github.com:"`.

### Testing

`test/m1.sh` is KICKOFF.md's `make test` — the Milestone 1 definition of done,
90 assertions over a scratch **encrypted** repo, most positive claims paired
with a control that must come out the other way. Eighteen assertions are
explicitly labelled `CONTROL`; the rest rely on the surrounding section's
controls or on being independent properties, so read the pairing as a strong
habit rather than an invariant. It is the portable, hermetic, CI-invoked proof that the
M1 loop works on a `*.efossil` repo — no hard-coded absolute paths, its own
`FOSSIL_HOME`/`FOSSIL_SEE_KEY`/scratch tree, nothing outside them; whereas
`build/m1-e2e-probe.sh`, which got there first, hard-codes absolute paths,
writes into the developer's real `~/.config/fossil.db`, has a few weaker
assertions, and **does not currently come out clean or deterministically** —
it fails one assertion on a rank-1 tie of its own making, so its exit status
is not a usable gate. AGENTS.md's Layout entry for it has the count, the date
and the diagnosis; a git-`HEAD` binary fails it identically, so it is not a
regression to go hunting for. m1.sh sets its own `FOSSIL_SEE_KEY` and asserts the hub is really
ciphertext (E1), that a plaintext repo built by the same binary and key fails
that same check (E2), that a stock `sqlite3` **cannot** open the encrypted hub
(E3) but **can** open the plaintext control (E3b), and that a wrong key is
rejected non-interactively (E4). Then
degraded and hybrid retrieval of the same planted answer, the two-sided vector
proof, and the full D-11 push → fresh clone → pull compute-once loop with the
pulled cache byte-compared to the pushed one (C10) and the vectors
fingerprint-compared through it (C11). Since 2026-08-13 it also covers the
D-12 model round trip (M1–M9: the model travels as uv blobs, is checksummed
against the epoch pin on the way in, and a fresh clone runs *hybrid*
retrieval on it while its only sync peer is a local `file://` hub), the
citable `content_hash` on every hit line (G1–G6, cross-checked against an
independent `shasum` and against `.viki/cache.db`), stale-content withdrawal
after an edit and a deletion (H1–H11), and mixed-epoch scoring (J1–J4).
Those last two groups are what closes **KICKOFF.md deliverables 2 and 3** —
"results with source content_hash" (G1–G6, with controls G4b/G5b) and
"embedding cache **+ model file** as fossil unversioned files" (M1–M9, with
controls M1b/M8) — neither of which was true before 2026-08-13. Deliverable 2
is met on the CLI and `/api/ask` only; `viki serve`'s HTML page still shows no
hash.
Non-vacuity is measured, not asserted: a binary compiled from git `HEAD`'s
`src/` scores `54 passed, 36 failed, 0 skipped` on this same file. The two
36s are a coincidence, not an identity — of the 36 assertions added this
round, 26 fail against that binary and 10 pass (mostly controls), and the
other 10 failures are pre-existing assertions that go red only on the changed
`viki ask` output format. AGENTS.md lists both id sets.

```sh
bash test/m1.sh                             # "0 failed, 0 skipped" == M1 met (see below)
sh build/forum-e2e-probe.sh <empty-dir>     # the forum leg, which m1.sh deliberately omits
sh build/grep-probe.sh <empty-dir>          # `viki grep`: ERE really ERE, -i, --k, --source
sh build/muse-probe.sh <empty-dir>          # `viki muse`: undirected recall, no query
sh edge/build-wasm.sh                       # viki edge: the READ-ONLY tier compiled to
                                            #   WebAssembly, via a CONTAINER (emscripten/emsdk)
                                            #   so nothing lands on the host. -> edge/dist/.
                                            #   Builds ONLY the files with no fork/exec/socket/
                                            #   dlopen: viki_ask/db/grep/note + SQLite + ndvss.
                                            #   The three ONNX symbols viki_ask.c references are
                                            #   aborting stubs (edge/edge_noembed.c) -- that
                                            #   three-symbol coupling is the whole distance
                                            #   between viki's read path and a phone.
                                            #   HYBRID: the query is tokenized by viki's OWN
                                            #   tokenizer.c inside wasm, onnxruntime-web runs the
                                            #   graph in JS, and pooling/L2 happen back in C
                                            #   mirroring embed.c. Verified to reproduce the
                                            #   NATIVE binary's ranking AND rrf scores exactly.
                                            #   Falls back to BM25+literal with no model.
                                            #   IT NEEDS NO FOSSIL: the hub already serves
                                            #   /uv/viki-cache.db and /uv/viki-model/* over plain
                                            #   HTTP, so `cache pull` on the edge is a fetch()
sh build/vikiverse-up.sh <dir> [tribe] [--lan]  # stands up a WORKING vikiverse: one
                                            #   encrypted hub served over real HTTP, a
                                            #   `laptop` peer that has the model and does
                                            #   the embedding, and a `phone` peer with NO
                                            #   model that pulls cache+model as uv blobs
                                            #   and still answers semantically. Writes a
                                            #   README.txt and a vikiverse-down.sh.
                                            #   DO NOT PIPE IT -- see its header
sh edge/tools/build-tools.sh                 # the native key-custody tools -> build/dist/:
                                            #   viki-key-wrap    age v1 wrap/unwrap to X25519
                                            #                    recipients. Wire-compatible:
                                            #                    `age -d` reads what it writes
                                            #                    and vice versa (proven, I1-I3)
                                            #   viki-identity    identity.db -- a SQLCipher
                                            #                    container under the KNOWN key
                                            #                    "1" (kept for its per-page HMAC
                                            #                    = tamper detection), holding
                                            #                    private keys each wrapped with
                                            #                    its own passphrase
                                            #   viki-cache-encrypt   the pushing peer's cache
                                            #                    converter, via sqlcipher_export
                                            #   LibreSSL + SQLCipher come from fossil-see's
                                            #   vendor tree; nothing new is downloaded
sh build/reader-probe.sh                    # the Chrome reader is OBSERVE ONLY, 9 assertions
                                            #   and they are safety properties, not style: no
                                            #   .click/.submit/dispatchEvent/innerHTML anywhere,
                                            #   loopback as the ONLY destination, and every
                                            #   extractor able to report `blind` so a broken
                                            #   selector cannot read as a quiet day
sh assistant/brief.sh [--me NAME]           # THE MORNING BRIEF -- and it is NOT VIKI. It
                                            #   composes promises + coverage + pending into a
                                            #   decision: what is at risk, what I can and
                                            #   cannot see, which channels need signing in,
                                            #   what I am unsure about. Every judgment (what
                                            #   counts as stale, what to ask) lives there and
                                            #   none of it in src/. See assistant/README.md
                                            #   for the test that tells which side a thing is
                                            #   on -- the sharpest being that viki CANNOT ask
                                            #   questions, so a brief that asks one was never
                                            #   a viki feature
sh build/promise-probe.sh <empty-dir>       # THE PROMISE LEDGER, 24 assertions. P4 is the
                                            #   one that matters: a superseded promise must
                                            #   LEAVE the ledger. A promise retired last month
                                            #   still reading as owed makes the brief wrong in
                                            #   the direction of anxiety, which is what the
                                            #   product exists to remove. NOTE: no `set -e` --
                                            #   every CONTROL is a `grep -c == 0` and grep exits
                                            #   1 on zero, so set -e would abort the suite
                                            #   exactly when the code was correct
sh build/keywrap-probe.sh <empty-dir>       # key custody, 9 assertions. The INTEROP three
                                            #   (I1-I3) are the ones that matter and they SKIP
                                            #   without `age` on PATH rather than passing:
                                            #   an early build passed its own round-trip while
                                            #   emitting keys real age rejected
sh build/literal-probe.sh <empty-dir>       # `viki ask`'s LITERAL leg. Tests under CONTEST:
                                            #   a long same-topic document must bury the one
                                            #   chunk naming the identifier, or every
                                            #   assertion passes vacuously (an earlier draft
                                            #   scored 7/7 against a binary with no leg).
                                            #   REFUSES to run without a model, for that reason
sh build/fragment-probe.sh <empty-dir>      # fragment marking on ask / serve / grep
sh build/cache-probe.sh <empty-dir>         # the DISTRIBUTION path: push/pull. The only
                                            #   test that runs over real HTTP with a
                                            #   capability-limited user -- which is how
                                            #   `viki cache push` silently published
                                            #   nothing for so long (needs the fossil `y`
                                            #   capability; see QUEUE 35)
sh build/fossilsee-probe.sh <empty-dir>     # in-process fossil SQL == subprocess.
                                            #   Needs a built libfossilsee; REFUSES the
                                            #   equivalence half without one rather than
                                            #   passing vacuously
build/dist/viki ndvss-selftest              # proves sqlite-ndvss is really statically linked
build/dist/viki embed-selftest [model-dir]  # semantic property check, not just "didn't crash"
bash test/retrieval-eval.sh                 # retrieval QUALITY + index COVERAGE (not pass/fail)
```

**None of the tests above says anything about retrieval quality.** They
prove an answer *comes back*; `test/retrieval-eval.sh` measures whether it
comes back **first**. It builds an encrypted corpus from this
repo's own docs plus check-in comments, wiki, tickets, forum posts, a tech
note, an attachment, tags and an unversioned file
(`test/retrieval-corpus.sh`), runs 59 questions (`test/retrieval-queries.tsv`,
six classes, 31% held out), and prints recall@1/@5/@k, MRR, a per-class and
per-artifact breakdown, a **BM25-only control**, and a per-query failure
taxonomy. It prints numbers and gates nothing; exit 0 means it produced
them.

**Do not transcribe its numbers into this file.** Corpus size, recall and the
per-artifact coverage figures all move whenever these docs are edited — which
is what the harness's `corpus fp` exists to pin — so a figure copied here goes
stale silently and then gets quoted as current. Two paragraphs of exactly that
were removed on 2026-08-23 (QUEUE 44): a frozen "114-chunk" that AGENTS.md
already contradicted with 138, and "0 of 16 questions whose answer lives in a
check-in comment or other un-indexed artifact are answerable at all" — which
stopped being true once `ckin:`/`note:`/`tchg:`/`attach:`/`uv:` were wired in,
leaving only historical file versions and tags unindexed. **Run the harness and
read report 2** rather than believing any number in prose here. Re-measure the
old binary before claiming a new one improved anything, and quote the `corpus
fp` with any number you do report.

The two selftests are hidden subcommands (not in `usage()`) and both run
automatically at the end of `build/build.sh`.

`test/m1.sh` will not run against a stock `fossil` — it creates `*.efossil`
repos and asserts they are really ciphertext, so it needs
`vendor/fossil-see/build/build.sh` to have been run (or `VIKI_FOSSIL_BIN`
pointed at some other SQLCipher build).

**A green run says nothing about `src/`.** `test/m1.sh` rebuilds nothing; it
tests whatever `build/dist/viki` happens to be, and that file is gitignored and
can lag `src/` arbitrarily — it has already scored a full green (54/54, in the
54-assertion era of this file) against a binary
missing three fixes that were sitting in `src/viki_index.c` at the time, and
its output was *byte-identical* before and after the rebuild that added them.
Before believing a green run, check the binary is current (`ls -la
build/dist/viki src/*.c`, or grep the binary for a string unique to the newest
fix). No single suite covers all nine indexed content types: `test/m1.sh`
(files/wiki/ticket, the DoD gate, the only one in CI),
`build/forum-e2e-probe.sh` (forum, against live posts),
`build/model-uv-e2e-probe.sh` (D-12 model distribution),
`build/muse-probe.sh` (`viki muse`), `build/grep-probe.sh` (`viki grep`),
`build/fragment-probe.sh` (how every surface MARKS what it returns) and
`bash test/retrieval-eval.sh`
(ranking quality + coverage) are seven different questions. Run the ones your
change can break.

**A skipping run still exits 0.** With no model, or no `sqlite3` on PATH,
m1.sh prints `RESULT: PASS WITH SKIPS` — by its own words "NOT a full
Milestone 1 pass" — and exits 0 anyway. There is no single skip figure;
each missing dependency has its own, all measured 2026-08-13 against the
same binary: no model → `64 passed, 0 failed, 26 skipped`; an `sqlite3`
with no **fts5 module** → `87 passed, 0 failed, 3 skipped` (H11/H11b/J1
query `chunk_fts`; this is what the CI macOS runner ships, and it read as
three hard FAILures until 2026-08-21); no `sqlite3` →
`80 passed, 0 failed, 10 skipped`; neither → `57 passed, 0 failed, 33
skipped`; all present → `90 passed, 0 failed, 0 skipped`. The skip sets
overlap, so they do not add. A missing model does **not** skip the whole
vector proof either: `A12`, the half that asserts a semantic query finds
nothing *without* a model, runs and passes there — only `B5` is skipped.
Never read exit status alone; read the `N passed, N failed, N skipped`
line. CI does exactly that and fails the job on any skip.

CI (`.github/workflows/build.yml`) builds on linux-x86_64, linux-arm64
(experimental), macos-arm64 and windows-x86_64, and runs `test/m1.sh` **and
`build/fossilsee-probe.sh`** on the first and third of those — the other two carry an announce-only job leg named
`-- NOT RUN` explaining why. Adding a platform to the m1 matrix is what
removes its announcement; there is no second list to keep in sync.

Everything beyond that is manually verified against a scratch fossil-see repo
and written up in AGENTS.md's "Verified working end to end" section. **Match
that standard**: prove a semantic property or a real round-trip, not that a
command exited 0 — and record honestly in AGENTS.md what you did *not* verify.
Forum-post extraction is the cautionary example: it sat "implemented, never
round-tripped against a live post" for a while, and the first real round-trip
immediately found two bugs (escaped `H` thread titles; superseded post versions
indexed as current) -- then a *second*, wider round-trip found a third that the
first had missed (card lookup running past the card region into the post body,
so a reply adopts a body line as its title). Structural similarity to a verified
path is not evidence, and neither is one successful round-trip: it took four
artifact shapes (thread-start, reply, card-shaped reply, edit) to exercise the
path. `build/forum-e2e-probe.sh` is the standing proof.

## Architecture

`viki` is a C CLI over an unencrypted, local, **derived** SQLite cache at
`.viki/cache.db`, relative to cwd — deliberately *not* inside the Fossil repo
db. The repo (fossil-see) holds truth; the cache holds projections.

**The retrieval core** is `viki_ask_query()` in `viki_ask.c`. It runs **three**
legs into one candidate pool and fuses them by reciprocal rank (RRF, k=60):

- **Rung 0** — FTS5 BM25 over `chunk_fts`. Always available; free (FTS5 is
  compiled in).
- **Rung 1** — the **literal leg**: exact substring match over `chunk_text`,
  scored by the *rarity-weighted* sum of the query terms a chunk contains
  (`1/df`, from one extra scan). Always available; needs no model. It exists
  because the other two are **density-biased** — bm25() rewards term frequency
  and cosine rewards topical concentration — so a document treating a subject
  at length takes every slot and a document stating the same fact once, in
  passing, is buried. Passing mentions are where a partially-applied update
  hides. It also makes `ask` subsume a literal lookup, so the ask-or-grep
  decision mostly disappears (`grep` remains for real regex and exhaustive
  enumeration). Measured, corpus fp `c7e52620ae430794`, n=43: recall@1
  0.302 → 0.372, MRR 0.418 → 0.465, and on the `identifier` class MRR
  0.667 → 0.700 with held-out recall@1 0.500 → 1.000. recall@5 is unchanged:
  it **reorders** the pool rather than finding new documents.
  **The rarity weighting is load-bearing** — a plain count of matched terms
  ties a unique identifier with the common words around it, and volume then
  wins; `build/literal-probe.sh`'s L2 is what catches that.
- **Rung 2** — `ndvss_cosine_similarity_f()` over `viki_chunk.embedding`,
  filtered by `model_id`. Present only when a model loads.

`viki_cmd_ask` (CLI printing) and `viki_serve.c`'s `/api/ask` are both thin
wrappers around that same function — so the CLI and the HTTP API provably cannot
diverge. Keep it that way when adding surfaces.

**Degraded mode is a required path, not a failure.** No model at
`$VIKI_MODEL_DIR` (falling back to `build/dist/model`) means BM25-only, with an
honest notice on stderr. `open_embedder_if_available()` in `viki.c` returning
NULL is expected; never propagate it as fatal.

**Indexing** (`viki_index.c`) is content-addressed and incremental. Files are
skipped by `(path, mtime)` via the `viki_source` side table, then chunked only
if `(content_hash, model_id)` is absent. **Nine** content types share one
`index_text_blob()` sink, with virtual paths distinguishing the non-file ones:

| Source | How it is extracted | Virtual path |
|---|---|---|
| checkout files | directory walk | `./relative/path` |
| wiki pages | `fossil wiki` subprocess | `wiki:Name` |
| tickets | `fossil ticket` subprocess | `ticket:UUID` |
| forum posts | `fossil sql` against `event`/`blob` (no export subcommand exists) | `forum:UUID` |
| check-in comments | `fossil sql`, `coalesce(ecomment, comment)` — Fossil's own timeline rule | `ckin:UUID` |
| tech notes | `fossil sql`, `event.type='e'` (Fossil DELETEs superseded rows) | `note:ID` |
| ticket changes | `fossil sql` + the artifact's `J` cards (`ticketchng.icomment` is NULL in practice) | `tchg:UUID` |
| attachments | `fossil sql`, `isLatest AND src <> ''` | `attach:UUID` |
| unversioned files | `fossil sql`; content is zlib-compressed when `encoding=1` | `uv:NAME` |

Each non-file class costs **one** `fossil sql` subprocess, not one per
artifact, using a counted framing parsed by `framed_next()` — **with one
unavoidable exception, `uv:`**. `index_unversioned()` uses framed SQL for the
NAMES and then forks `fossil unversioned cat` per file
(`src/viki_index.c:1724`), because `unversioned.content` is zlib-compressed
behind a 4-byte big-endian length prefix when `encoding=1`, and `unversioned
cat` is the only extraction path that does not require linking zlib. So a hub
with many uv blobs really does pay O(blobs) subprocesses. Both this file and
AGENTS.md stated the rule without that exception until 2026-08-23 (QUEUE 44).
That is not
tidiness: every `fossil` invocation costs a process spawn plus repo open, so
per-artifact extraction is O(artifacts) subprocesses where this is O(1).
**The magnitude depends on the key form and the old figure here was the
passphrase one.** Measured 2026-08-21, 40 reps: plaintext repo 5.71 ms/open,
encrypted with a raw `x'<64 hex>'` key **5.99 ms**, encrypted with a
passphrase **345.93 ms** — SQLCipher uses a raw 256-bit key directly and
skips PBKDF2 entirely (FINDINGS.md). So the KDF is only a real cost for
human-typed keys; the counted framing is still right, but do not justify it
with a ~0.5 s per-open number.
`viki_source.mtime` stays **0** for every virtual source and must — `fossil
amend` changes `ecomment` without touching the check-in's own time, so a real
mtime would make the fast-skip serve pre-amend text forever. Time belongs in
the composed header, where it is both FTS-indexed and embedded.

**The composition recipe is part of the sharing contract even though
`viki-manifest` says nothing about it.** `content_hash = sha256(the composed
extracted text)`, so two peers that compose a check-in header differently
produce different hashes for the same check-in and both end up in `viki ask`.
That is cache *fragmentation*, not corruption, and not an epoch bump — but the
header formats are frozen, and changing one must be called out as
cache-fragmenting.

**Fossil is a subprocess by default**, resolved by `viki_fossil_binary()`
(`$VIKI_FOSSIL_BIN`, else `fossil-see` on PATH, else `fossil`). **Nothing is
linked at build time and that is a hard constraint** — viki builds on four
platforms with no fossil-see prerequisite.

**The one exception is `viki_fossilsee.c`, and it is `dlopen`, not a link.**
When `libfossilsee` is loadable, `fossil_sql_framed()` runs its SQL
in-process; when it is absent, damaged or ABI-mismatched, it falls back to
the subprocess silently. Degraded mode is a required path here exactly as it
is for the model. The reason to prefer the in-process path is **not speed**
(`viki index` issues ~7 queries per run, so it saves ~45ms): `fossil sql`
exits 0 whether a query returned no rows *or failed to prepare*, which is the
ambiguity that once let `sweep_sources()` delete every `forum:` row. In
process, a failed prepare is a real error. `viki fossilsee-status` (hidden)
says which path is live, and `build/fossilsee-probe.sh` is the standing proof
the two agree. Two traps if you extend it: the ABI declarations in
`viki_fossilsee.c` are a hand-copy of `embed/fossilsee.h` guarded only by
`fossilsee_abi()`, and Fossil registers per-command SQL functions that
`db_open_repository()` does not — `content()` was missing this way and three
extractors silently produced nothing (FINDINGS.md).

Everything *else* Fossil-related is still a subprocess, and the wider
in-process track remains unfinished — before touching it, read
`../fossil-sqlcipher-libressl/embed/README.md`, **not**
`FFI_RISK.md`/`experiments/`, which are a frozen and now-outdated snapshot.

**sqlite-ndvss is statically linked**, compiled with `-DSQLITE_CORE` and
registered via `sqlite3_auto_extension(sqlite3_ndvss_init)` in
`viki_db.c` — never loaded as a runtime `.so`. It ships no public header, so the
entry point is `extern`-declared by hand.

**`viki serve`** is a single-threaded POSIX-sockets HTTP server with no external
HTTP library and no static-file serving (every response is generated, so there
is no path-traversal surface). The search page renders results via `textContent`
— indexed content is untrusted markup, never `innerHTML`. It is loopback-only
with no auth *by design*; internet exposure goes behind the Caddy instance
`server/setup-viki-serve.sh` configures, not hand-rolled TLS/auth in C.

## Conventions and traps

- **C style follows SQLite/Fossil**, since that is what this ecosystem is:
  `zPath`/`nItems`/`pDb` naming, `/* ... */` comments with `**` continuation,
  no C99 declarations-after-statements in most files. Comments explain *why*,
  and cite `FINDINGS.md` when the reason was discovered empirically.
- **`vendor/` is read-only upstream**, with one deliberate exception:
  `vendor/sqlite-ndvss` is **already Warren's fork**, so fixing a real
  portability bug there is maintaining what this project owns, not forking
  something it doesn't. KICKOFF's "do not fork it" was about not diverging
  from upstream's *design*, and it has been read too literally at least
  once — it is what pushed an engine swap (sqlite-vec) that was later
  reverted. Send the fix upstream and pin past it; do not accumulate local
  patches.

  **Why ndvss and not sqlite-vec, decided 2026-08-21 after actually trying
  the swap: PORTABILITY, and specifically wasm.** ndvss ships per-ISA
  kernels including `similarity_functions_wasmsimd.h`, which matters for
  the eventual wasm build (here and for other consumers of this engine).
  It also builds under MSYS2's plain MSYS environment, which viki requires
  for `fork()`/BSD sockets — sqlite-vec does not, without a patch:
  `sqlite-vec.c` assumes `#ifndef _WIN32` implies the BSD `u_int8_t`
  spellings exist, and plain MSYS is neither case. The swap was built,
  measured and reverted rather than argued about; see FINDINGS.md for what
  it did and did not buy. Its aarch64-Linux build bug
  is why CI's linux-arm64 job is `experimental: true`; that is a bug in that
  project, not something to work around here.
- **Never `strtok_r` Fossil's TSV/line output** — it collapses empty fields and
  silently corrupts records (it truncated ticket content once). Use
  `split_preserve_empty()` in `viki_index.c`.
- **FTS5 MATCH is implicit-AND**, which is wrong for natural-language queries.
  `build_or_query()` builds an explicit OR-of-quoted-terms; don't "simplify" it
  back to passing the raw query through.
- **When you *use* viki, don't embed your question — embed the answer you
  expect to find.** viki has no LLM and never will, but every caller is one,
  so the highest-leverage retrieval fix available is a calling convention
  (HyDE) rather than a feature: it needs no schema change, no epoch bump and
  no re-index. **AGENTS.md's "Querying viki" section is the one authoritative
  copy** — the rule, a re-runnable worked example, the measurement, its
  caveats, and when *not* to do it (a literal string wants `viki grep`; an
  already-precise keyword query is only diluted by expansion). Do not restate
  any of that here; this repo has been bitten repeatedly by one claim living
  in two files and rotting in one. Two operational facts you need before you
  type the command: `--k N` goes **after** the query string, and no harness
  in this repo measures this technique, so its numbers and their caveats
  live in that section rather than in a script you can re-run.
- **Excerpts are marked as fragments at display time, and there are two
  different "something is missing" notations.** ` ... ` is FTS5's `snippet()`
  eliding text from *inside* a chunk; `<<document continues above/below>>`
  says the *chunk* is a slice of a longer document; `<<excerpt truncated>>`
  says the excerpt shown is a cut-short prefix of its chunk. They are kept in
  disjoint alphabets (dots vs angle brackets) so a reader cannot merge them,
  and the markers avoid `[`/`]`, which the FTS snippet call spends on match
  highlighting. Marking is display-side only — `chunk_text` is stored raw and
  no re-index is involved — and it never touches the
  `[N] rrf=…  <hash>#<ix>  <source>` header, which three test files parse by
  position. `/api/ask` exposes the facts as
  `fragment_head`/`fragment_tail`/`snippet_truncated`/`chunk_count` booleans
  and leaves `snippet` undecorated on purpose: a client already parsing
  `snippet` must not start receiving marker words it cannot tell from indexed
  content. The literal strings live once, as `VIKI_MARK_*` in `viki_ask.h`;
  `viki_grep.c` and `viki_serve.c`'s embedded page both take them from there
  by include and C string concatenation rather than retyping them, so the
  surfaces cannot drift. `build/fragment-probe.sh` is the proof (R2/R6 are
  the assertions that fail if `viki grep` ever stops marking).
  **`viki muse` still prints unmarked excerpts** — same defect, same patch
  shape, deliberately out of scope for the round that fixed the other three.
- **`fossil ticket` refuses to run without a resolvable user**, even read-only
  (`viki_fossil_user()`: `$VIKI_FOSSIL_USER`, else `$USER`, else `"viki"`).
  `fossil wiki` has no such requirement.
- **Windows builds under MSYS2's plain MSYS environment**, not MINGW64 (needed
  for `fork()`/BSD sockets), so `viki.exe` links `msys-2.0.dll` — bundled by
  `build.sh`. ONNX Runtime is native and needs UTF-16 model paths regardless
  (`-DVIKI_WIN_ORT_PATH`) and `cygpath -m` paths, since it has no MSYS awareness.
- **`.vikiignore` decides what is in the corpus, and the corpus is the
  product.** One glob per line, `#` comments, at the indexed root; a pattern
  with no `/` matches any path COMPONENT, one with `/` matches the whole
  relative path and everything under it. `SKIP_DIRS` in `viki_index.c` still
  matches by BASENAME, which is why it was not enough: `vendor-wasm` is not
  `vendor` and `dist` is not `build`. Measured 2026-08-24 on this repo before
  the file existed — **83.4% of all chunks were vendored SQLCipher/LibreSSL
  source**, 8.6% build output, 6.9% actually ours, and a question about why
  ndvss was chosen over sqlite-vec returned a vendored `sqlite3.c` at rank 1.
  Adding the file took the corpus from 9,578 chunks to 779 and moved that
  question's rank-1 hit to `FINDINGS.md`, where the answer is.
- **Known-naive by choice, not by oversight** (see AGENTS.md before "fixing"):
  fixed 40-line chunks with no overlap or token awareness, ASCII-scoped
  tokenizer, no epoch-migration path for a model change, and
  `VIKI_FTS_EPOCH_SLACK = 4` (`viki_ask.c`) — the BM25 leg over-fetches 4×
  and stops at `poolSize` distinct chunks, so a cache holding more than 4
  epochs of one chunk returns fewer candidates than asked for. Scores stay
  correct; deep-tail recall shrinks. Untested (nothing builds >2 epochs).
- **Staleness detection is `mtime`-only.** `viki index` skips a file whose
  `(path, mtime)` matches `viki_source` and never hashes it, so a document
  rewritten *within the same second* as its last index is invisible —
  `0 (re)chunked`, and the replaced text still answers at rank 1. Real
  limitation, not just a test artifact; it is why `test/m1.sh` section 7
  follows every mutation with `touch -t 202001010000`. See FINDINGS.md.
- **`viki serve`'s HTML page renders no `content_hash`.** The CLI hit line
  and `/api/ask`'s `"hash"` both carry it, but the page shows only
  rank/rrf/source/`chunk_ix of chunk_count` plus the fragment markers — so
  the one *human* surface cannot cite a source, and nothing in `test/m1.sh`
  covers `serve` at all (`build/fragment-probe.sh`'s S-series is its only
  automated coverage, and only of the fragment fields).
- **`viki index` invalidates, and its scoping rule is load-bearing.** Superseded
  and deleted sources are retired from `viki_source`, then chunks no longer
  referenced by any live source are deleted from **both** `viki_chunk` and
  `chunk_fts` — nothing cascades. **`chunk_fts` is an EXTERNAL-CONTENT FTS5
  table** (`content='viki_chunk'`) since 2026-08-21, which removed a full second
  copy of every chunk's text and took **36.4%** off the artifact D-12 ships
  (4,882,432 → 3,104,768 bytes, `VACUUM INTO`, same 245 matches). That makes
  the **delete order load-bearing**: FTS5 keeps no text of its own, so it
  re-reads `viki_chunk` to know which tokens to drop — delete the chunk row
  first and the FTS delete succeeds, changes nothing, and *the withdrawn text
  stays searchable*. `gc_orphan_chunks()` deletes **fts first, then
  `viki_chunk`**, and m1's H11 is what catches a regression. Old caches
  migrate on open (`migrate_chunk_fts()` in `viki_db.c`); `cache pull`
  **rebuilds** the index rather than copying it, because external-content
  entries are bound to rowids and rowids are assigned locally. A run only invalidates
  namespaces it can prove it observed: filesystem paths under the directory it
  actually walked, and each virtual namespace only when that extractor proved
  it ran (a `#viki-eof` sentinel row, **not** the exit status — `fossil sql`
  exits 0 when the query fails). A namespace missing from `VIRTUAL_NS[]` is
  treated as a relative path and swept, which is how an older binary used to
  delete every `ckin:` row. Never widen that scope casually — "delete everything not seen this
  run" wipes the cache on any subdirectory index or on any machine without a
  `fossil` binary. See `sweep_sources()` in `src/viki_index.c` and FINDINGS.md.
- **What may sync is a policy, and `SYNC.md` is it.** Four classes: `derived`
  (rebuildable — latest-wins is safe), `grow-only` (immutable rows on a content
  key — union IS merge, so multi-writer is safe), `owned` (one declared writer),
  `private` (**never syncs**). An undeclared blob must be refused, not guessed
  at, because the guess is latest-wins and that loses data silently. Two
  measured facts drive it: `uv` is latest-wins by mtime across peers whose
  clocks disagree, and **encrypting the same database twice with the same key
  produces different bytes** (SQLCipher salts per database) — so encrypted blobs
  cannot be diffed, deduped or delta-synced, and every sync is a full transfer.
  That is the strongest argument for keeping truth in Fossil artifacts, which
  merge and carry history, rather than in blobs. `identity.db` is `private` and
  `viki_cache_refuse_private()` enforces it in code rather than in prose.
  **All of that is a GUARDRAIL, not a security boundary** (SYNC.md 0b): a tribe
  member holds the key and has SQL — via SQLCipher directly, `libfossilsee`, or
  plain `fossil` — so viki-layer policy stops accidents and stops nothing else.
  The layer that *is* defensible is the ARTIFACT layer: artifacts are
  content-addressed and Merkle-linked, so tampering is detectable by any peer,
  while `uv` blobs are name-addressed with no hash in the protocol at all. Live
  consequence: `viki cache pull` reports the model "verified against the epoch
  pin", but the pin (`viki-manifest.json`) is **itself a uv blob** — so the
  check catches corruption, not an adversary.
- **Out of scope for Milestone 1**: Flutter app, VPS deployment, calendar
  projection, voice, MCP server. Do not start them.
