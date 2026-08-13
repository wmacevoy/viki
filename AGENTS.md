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
forum extraction in particular is verified only against the manifest
*format* (via a real wiki artifact), not a live forum post; read that
entry before trusting forum indexing output.

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
  viki_index.c/.h `viki index <dir>`: walk+chunk+hash+embed checkout files, plus `fossil wiki`/`fossil ticket` subprocess extraction and `fossil sql` extraction (no CLI export exists for forum posts) for wiki pages, tickets, and forum posts (virtual paths `wiki:Name`/`ticket:UUID`/`forum:UUID`)
  viki_ask.c/.h   `viki ask "<query>"`: FTS5 BM25 + ndvss cosine, reciprocal rank fusion (OR-of-terms FTS query, see FINDINGS.md). Retrieval logic lives in public `viki_ask_query()` (viki_ask.h) so `viki serve` can share it; `viki_cmd_ask` is a thin CLI-printing wrapper around it.
  viki_serve.c/.h `viki serve`: single-threaded POSIX-sockets HTTP server (no external HTTP library). `/` = static HTML search page (renders results via textContent, never innerHTML -- indexed content isn't trusted markup). `/api/ask`, `/api/chunk`, `/api/health` = JSON, for agents. No static-file serving (every response is generated, not read off disk), so no path-traversal surface.
  viki_cache.c/.h `viki cache push|pull`: fossil uv wrappers (subprocess)
  sha256.c/.h     content_hash keying, via LibreSSL EVP (not hand-rolled)
  tokenizer.c/.h  BERT WordPiece tokenization against vocab.txt (ASCII-scoped, see FINDINGS.md/tokenizer.h)
  embed.c/.h      ONNX Runtime C API: session, tokenize -> Run -> mean-pool -> L2-normalize
build/
  build.sh        builds build/dist/viki + downloads/verifies ONNX Runtime + the pinned model; see its header comment
  versions.env    pins: onnxruntime release (3 platforms) + the embedding model/vocab, all SHA256-verified
vendor/
  fossil-see/     git submodule -- NOT a build dependency of viki itself (see FINDINGS.md); kept only so a
                  fossil-see binary is available at runtime if $VIKI_FOSSIL_BIN/PATH don't already have one
  sqlite-ndvss/   git submodule -- Warren's fork of JarkkoPar/sqlite-ndvss, statically linked
  download-cache/ gitignored -- cached onnxruntime tarball + model/vocab downloads (build/build.sh's fetch_verify)
experiments/      FFI_RISK.md's proof-of-concept (in-process fossil), inherited from fossil-app
server/           hub deployment scripts: setup-hub.sh (fossil server + Caddy TLS, D-8) and
                  setup-viki-serve.sh (adds viki serve behind the same Caddy, Basic Auth) -- see
                  SERVER_SETUP.md; inherited from fossil-app (pre-encryption; see ENCRYPTION.md TODOs)
```

## Build and run

```sh
# self-contained -- no other project needs to be built first (see
# FINDINGS.md). Downloads + SHA256-verifies the SQLite amalgamation,
# onnxruntime, and the model on first run, cached after that:
build/build.sh

export VIKI_MODEL_DIR=build/dist/model   # or wherever; unset/absent = BM25-only
build/dist/viki index <dir>          # walk dir, populate .viki/cache.db (relative to cwd)
build/dist/viki ask "<query>"        # hybrid top-5; --k N to change
build/dist/viki cache push           # fossil uv add + sync (run from an open fossil-see checkout)
build/dist/viki cache pull           # fossil uv sync + export
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

## Verified working end to end (not just "should work")

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
- **Forum post extraction: partially verified.** `viki index` against a
  repo with zero forum posts correctly reports `0 forum post(s), 0
  (re)chunked` without crashing. The manifest-card parsing it depends on
  (`W <n>\n`-counted body, optional `H` title card) was verified against
  a real wiki artifact's raw manifest, and the `event.type='f'` selector
  against Fossil's own `--type` docs -- but **no live forum post has been
  round-tripped through `index_forum()` end to end** (web-UI form
  submission resisted `curl` scripting; see FINDINGS.md). Treat forum
  search results with more skepticism than wiki/ticket/file results until
  someone verifies this against a real post.
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
  not assumed. (This test predates the embedding pipeline -- worth
  re-running with a model present to confirm embeddings round-trip
  through `fossil uv` correctly too; not yet done.)

## Not yet built (see KICKOFF.md's full Milestone 1 spec for what's still owed)

- **`viki-manifest` epoch mechanism.** `build/versions.env` pins one model
  and `build/build.sh` writes a `viki-manifest.json` next to it, but
  there's no code path yet for detecting/handling a *model change*
  (VIKI_DESIGN.md's "epoch bump": update the manifest, re-embed, let
  `model_id` let two epochs coexist during migration). Right now bumping
  the pin just means new content gets embedded under a new model_id;
  nothing re-embeds old content or cleans up the old epoch's vectors.
- **Cross-arch model verification.** The pinned model is the `arm64`
  quantization recipe (picked because it could be verified on this dev
  machine). Not yet verified on x86_64 -- see FINDINGS.md and
  `build/versions.env`'s caveat on this.
- **Tech notes / other artifact types** aren't indexed (checkout files,
  wiki pages, tickets, and forum posts are, so far -- forum indexing is
  implemented but not verified against a live post, see FINDINGS.md).
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
- **Garbage collection of orphaned chunks.** When a file's content
  changes, the old content_hash's chunk rows are never deleted (harmless
  -- content-addressed, rebuildable, D-10 -- but will accumulate).
- **`experiments/harness.c` (in-process embedding) was not re-run** against
  this fossil-see build as a regression gate -- viki's CLI shells out to
  the `fossil-see` binary rather than embedding it in-process, so that
  harness isn't on viki's critical path yet. Revisit when/if the Flutter
  FFI target starts (explicitly out of scope for M1 per KICKOFF.md).
