# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Orientation

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
                                         # then the snippet indented 4 spaces
build/dist/viki muse [--k N] [--seed N] [--from <hash>#<ix>]
                                         # undirected recall: NO query. Returns chunks from
                                         # the MIDDLE of a random seed chunk's cosine band.
                                         # Needs vectors in the cache but NOT model.onnx;
                                         # refuses a BM25-only cache rather than faking it.
                                         # --seed replays a run exactly.
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
writes into the developer's real `~/.config/fossil.db`, and has a few weaker
assertions. m1.sh sets its own `FOSSIL_SEE_KEY` and asserts the hub is really
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
build/dist/viki ndvss-selftest              # proves sqlite-ndvss is really statically linked
build/dist/viki embed-selftest [model-dir]  # semantic property check, not just "didn't crash"
bash test/retrieval-eval.sh                 # retrieval QUALITY + index COVERAGE (not pass/fail)
```

**None of the tests above says anything about retrieval quality.** They
prove an answer *comes back*; `test/retrieval-eval.sh` measures whether it
comes back **first**. It builds a 114-chunk encrypted corpus from this
repo's own docs plus check-in comments, wiki, tickets, forum posts, a tech
note, an attachment, tags and an unversioned file
(`test/retrieval-corpus.sh`), runs 59 questions (`test/retrieval-queries.tsv`,
six classes, 31% held out), and prints recall@1/@5/@k, MRR, a per-class and
per-artifact breakdown, a **BM25-only control**, and a per-query failure
taxonomy. It prints numbers and gates nothing; exit 0 means it produced
them. Two baseline facts worth knowing before you change retrieval code:
hybrid is **worse than BM25-only at rank 1** (0.256 vs 0.302) while better
at recall@5, and 0 of 16 questions whose answer lives in a check-in comment
or other un-indexed artifact are answerable at all. Both in FINDINGS.md,
with repros. Re-measure the old binary before claiming a new one improved
anything, and quote the harness's `corpus fp` with any number -- the corpus
is built from these docs, so editing them moves the baseline.

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
`build/muse-probe.sh` (`viki muse`) and `bash test/retrieval-eval.sh`
(ranking quality + coverage) are five different questions. Run the ones your
change can break.

**A skipping run still exits 0.** With no model, or no `sqlite3` on PATH,
m1.sh prints `RESULT: PASS WITH SKIPS` — by its own words "NOT a full
Milestone 1 pass" — and exits 0 anyway. There is no single skip figure;
each missing dependency has its own, all measured 2026-08-13 against the
same binary: no model → `64 passed, 0 failed, 26 skipped`; no `sqlite3` →
`80 passed, 0 failed, 10 skipped`; neither → `57 passed, 0 failed, 33
skipped`; all present → `90 passed, 0 failed, 0 skipped`. The skip sets
overlap, so they do not add. A missing model does **not** skip the whole
vector proof either: `A12`, the half that asserts a semantic query finds
nothing *without* a model, runs and passes there — only `B5` is skipped.
Never read exit status alone; read the `N passed, N failed, N skipped`
line. CI does exactly that and fails the job on any skip.

CI (`.github/workflows/build.yml`) builds on linux-x86_64, linux-arm64
(experimental), macos-arm64 and windows-x86_64, and runs `test/m1.sh` on the
first and third of those — the other two carry an announce-only job leg named
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

**The retrieval core** is `viki_ask_query()` in `viki_ask.c`. It runs two legs
into one candidate pool and fuses them by reciprocal rank (RRF, k=60):

- **Rung 0** — FTS5 BM25 over `chunk_fts`. Always available; free (FTS5 is
  compiled in).
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
artifact, using a counted framing parsed by `framed_next()`. That is not
tidiness: on an encrypted repo every `fossil` invocation pays a SQLCipher KDF
(~0.5 s here), so per-artifact extraction is minutes where this is seconds.
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

**Everything Fossil is a subprocess**, resolved by `viki_fossil_binary()`
(`$VIKI_FOSSIL_BIN`, else `fossil-see` on PATH, else `fossil`). Nothing is
linked in-process. In-process Fossil is a separate, unfinished track — before
touching it, read `../fossil-sqlcipher-libressl/embed/README.md`, **not**
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
- **`vendor/` is read-only upstream.** Vendor, don't edit — including
  `vendor/sqlite-ndvss` (KICKOFF: do not fork it). Its aarch64-Linux build bug
  is why CI's linux-arm64 job is `experimental: true`; that is a bug in that
  project, not something to work around here.
- **Never `strtok_r` Fossil's TSV/line output** — it collapses empty fields and
  silently corrupts records (it truncated ticket content once). Use
  `split_preserve_empty()` in `viki_index.c`.
- **FTS5 MATCH is implicit-AND**, which is wrong for natural-language queries.
  `build_or_query()` builds an explicit OR-of-quoted-terms; don't "simplify" it
  back to passing the raw query through.
- **`fossil ticket` refuses to run without a resolvable user**, even read-only
  (`viki_fossil_user()`: `$VIKI_FOSSIL_USER`, else `$USER`, else `"viki"`).
  `fossil wiki` has no such requirement.
- **Windows builds under MSYS2's plain MSYS environment**, not MINGW64 (needed
  for `fork()`/BSD sockets), so `viki.exe` links `msys-2.0.dll` — bundled by
  `build.sh`. ONNX Runtime is native and needs UTF-16 model paths regardless
  (`-DVIKI_WIN_ORT_PATH`) and `cygpath -m` paths, since it has no MSYS awareness.
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
  rank/rrf/source/chunk_ix — so the one *human* surface cannot cite a
  source, and nothing in `test/m1.sh` covers `serve` at all.
- **`viki index` invalidates, and its scoping rule is load-bearing.** Superseded
  and deleted sources are retired from `viki_source`, then chunks no longer
  referenced by any live source are deleted from **both** `viki_chunk` and
  `chunk_fts` (a plain FTS5 table — nothing cascades). A run only invalidates
  namespaces it can prove it observed: filesystem paths under the directory it
  actually walked, and each virtual namespace only when that extractor proved
  it ran (a `#viki-eof` sentinel row, **not** the exit status — `fossil sql`
  exits 0 when the query fails). A namespace missing from `VIRTUAL_NS[]` is
  treated as a relative path and swept, which is how an older binary used to
  delete every `ckin:` row. Never widen that scope casually — "delete everything not seen this
  run" wipes the cache on any subdirectory index or on any machine without a
  `fossil` binary. See `sweep_sources()` in `src/viki_index.c` and FINDINGS.md.
- **Out of scope for Milestone 1**: Flutter app, VPS deployment, calendar
  projection, voice, MCP server. Do not start them.
