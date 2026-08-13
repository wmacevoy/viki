# AGENTS.md -- orient here first

A brand-new agent session should be able to pick up work from this file
alone (US-2c). Read `KICKOFF.md` for the full Milestone 1 brief; this file
is the current-state snapshot, kept up to date in the same commit as any
code change that makes it stale (KICKOFF.md's process rule).

## What exists right now

Milestone 1 skeleton: a real, working `viki` CLI, BM25-only (rung 0).
**No ONNX embedding pipeline (rung 1/2) exists yet** -- `viki ask` is
correctly, honestly degraded-mode, not a stub pretending to do hybrid
retrieval. See `FINDINGS.md` for what was actually verified and how.

## Layout

```
src/            viki CLI source (C)
  viki.c          subcommand dispatch (index / ask / cache push|pull / version / ndvss-selftest)
  viki_db.c/.h    local cache db: schema, ndvss static registration
  viki_index.c/.h `viki index <dir>`: walk, chunk, hash, incremental insert
  viki_ask.c/.h   `viki ask "<query>"`: FTS5 BM25 retrieval (OR-of-terms, see FINDINGS.md)
  viki_cache.c/.h `viki cache push|pull`: fossil uv wrappers (subprocess)
  sha256.c/.h     content_hash keying, via LibreSSL EVP (not hand-rolled)
build/
  build.sh        builds build/dist/viki; see its header comment for the exact recipe
vendor/
  fossil-see/     git submodule -- the shared encrypted-Fossil build (must be built first: vendor/fossil-see/build/build.sh)
  sqlite-ndvss/   git submodule -- Warren's fork of JarkkoPar/sqlite-ndvss, statically linked
experiments/      FFI_RISK.md's proof-of-concept (in-process fossil), inherited from fossil-app
server/           hub deployment scripts, inherited from fossil-app (pre-encryption; see ENCRYPTION.md TODOs)
```

## Build and run

```sh
# one-time, several minutes (builds LibreSSL from source):
vendor/fossil-see/build/build.sh

# then, every time after a source change:
build/build.sh

build/dist/viki index <dir>          # walk dir, populate .viki/cache.db (relative to cwd)
build/dist/viki ask "<query>"        # BM25 top-5; --k N to change
build/dist/viki cache push           # fossil uv add + sync (run from an open fossil-see checkout)
build/dist/viki cache pull           # fossil uv sync + export
build/dist/viki ndvss-selftest       # proves sqlite-ndvss is really statically linked
```

`viki cache push`/`pull` need a `fossil`-compatible binary on PATH, or set
`VIKI_FOSSIL_BIN` explicitly (e.g. to `vendor/fossil-see/build/dist/fossil-see`).

## Verified working end to end (not just "should work")

- `build/build.sh` produces `build/dist/viki` cleanly (only benign
  unused-variable warnings from sqlite-ndvss's own NEON kernels, not viki
  code).
- `viki index` + `viki ask` on a scratch directory with planted content:
  correct chunk found, correctly ranked, snippet highlights the matched
  terms.
- `viki ndvss-selftest`: confirms real static linking (`ndvss instruction
  set: neon` on this Apple Silicon machine -- runtime CPU dispatch, not a
  stub).
- Full hub/spoke/fresh-clone loop, using real `fossil-see`-built repos
  connected via `file://` sync: index + push from a spoke, then a
  **completely fresh clone that never ran `viki index`** pulls the cache
  and `viki ask` immediately returns the correct planted answer. This is
  the D-11 "compute once, share via `fossil uv`" claim, actually proven,
  not assumed.

## Not yet built (see KICKOFF.md's full Milestone 1 spec for what's still owed)

- **ONNX embedding pipeline (rung 1/2).** No model is vendored or pinned;
  no `viki-manifest`; `viki_chunk.embedding` is always NULL; `viki ask`
  never does vector search, only BM25. This is the largest remaining
  piece of Milestone 1.
- **Wiki artifacts.** `viki index` walks plain checkout files on disk. It
  does not read Fossil *wiki* artifacts (which aren't files in the
  checkout -- see `ARCHITECTURE.md`'s note on this). Needs a separate
  extraction path (shell out to `fossil wiki export` or similar, or read
  the artifacts directly).
- **Chunking is naive**: fixed 40-line splits, no overlap, no token
  awareness. Fine for a skeleton; revisit before relying on it for real
  retrieval quality.
- **Garbage collection of orphaned chunks.** When a file's content
  changes, the old content_hash's chunk rows are never deleted (harmless
  -- content-addressed, rebuildable, D-10 -- but will accumulate).
- **`experiments/harness.c` (in-process embedding) was not re-run** against
  this fossil-see build as a regression gate -- viki's CLI shells out to
  the `fossil-see` binary rather than embedding it in-process, so that
  harness isn't on viki's critical path yet. Revisit when/if the Flutter
  FFI target starts (explicitly out of scope for M1 per KICKOFF.md).
