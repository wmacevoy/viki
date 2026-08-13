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
standalone path). See `FINDINGS.md` for what was actually verified and how.

## Layout

```
src/            viki CLI source (C)
  viki.c          subcommand dispatch (index / ask / cache push|pull / version / ndvss-selftest / embed-selftest)
  viki_db.c/.h    local cache db: schema, ndvss static registration
  viki_index.c/.h `viki index <dir>`: walk, chunk, hash, incremental insert, embed if a model is available
  viki_ask.c/.h   `viki ask "<query>"`: FTS5 BM25 + ndvss cosine, reciprocal rank fusion (OR-of-terms FTS query, see FINDINGS.md)
  viki_cache.c/.h `viki cache push|pull`: fossil uv wrappers (subprocess)
  sha256.c/.h     content_hash keying, via LibreSSL EVP (not hand-rolled)
  tokenizer.c/.h  BERT WordPiece tokenization against vocab.txt (ASCII-scoped, see FINDINGS.md/tokenizer.h)
  embed.c/.h      ONNX Runtime C API: session, tokenize -> Run -> mean-pool -> L2-normalize
build/
  build.sh        builds build/dist/viki + downloads/verifies ONNX Runtime + the pinned model; see its header comment
  versions.env    pins: onnxruntime release (3 platforms) + the embedding model/vocab, all SHA256-verified
vendor/
  fossil-see/     git submodule -- the shared encrypted-Fossil build (must be built first: vendor/fossil-see/build/build.sh)
  sqlite-ndvss/   git submodule -- Warren's fork of JarkkoPar/sqlite-ndvss, statically linked
  download-cache/ gitignored -- cached onnxruntime tarball + model/vocab downloads (build/build.sh's fetch_verify)
experiments/      FFI_RISK.md's proof-of-concept (in-process fossil), inherited from fossil-app
server/           hub deployment scripts, inherited from fossil-app (pre-encryption; see ENCRYPTION.md TODOs)
```

## Build and run

```sh
# one-time, several minutes (builds LibreSSL from source):
vendor/fossil-see/build/build.sh

# then, every time after a source change (also downloads onnxruntime +
# the model on first run, cached after that):
build/build.sh

export VIKI_MODEL_DIR=build/dist/model   # or wherever; unset/absent = BM25-only
build/dist/viki index <dir>          # walk dir, populate .viki/cache.db (relative to cwd)
build/dist/viki ask "<query>"        # hybrid top-5; --k N to change
build/dist/viki cache push           # fossil uv add + sync (run from an open fossil-see checkout)
build/dist/viki cache pull           # fossil uv sync + export
build/dist/viki ndvss-selftest       # proves sqlite-ndvss is really statically linked
build/dist/viki embed-selftest [model-dir]  # proves the ONNX pipeline produces real embeddings (semantic property check)
```

`viki cache push`/`pull` need a `fossil`-compatible binary on PATH, or set
`VIKI_FOSSIL_BIN` explicitly (e.g. to `vendor/fossil-see/build/dist/fossil-see`).

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
- **Wiki artifacts.** `viki index` walks plain checkout files on disk. It
  does not read Fossil *wiki* artifacts (which aren't files in the
  checkout -- see `ARCHITECTURE.md`'s note on this). Needs a separate
  extraction path (shell out to `fossil wiki export` or similar, or read
  the artifacts directly).
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
