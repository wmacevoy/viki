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
build/dist/viki ask "<query>" [--k N]    # hybrid top-5
build/dist/viki serve [--host H] [--port N]   # 127.0.0.1:8080; / = HTML page, /api/* = JSON
build/dist/viki cache push|pull          # fossil uv add/sync/export
```

`vendor/fossil-see` is **not** a build dependency — it is a submodule only so a
fossil-compatible binary is available at runtime. Leave it uninitialized unless
you need it (CI does).

`.gitmodules` gives `vendor/sqlite-ndvss` an SSH URL; CI rewrites it with
`git config --global url."https://github.com/".insteadOf "git@github.com:"`.

### Testing

There is no automated test suite yet — KICKOFF.md's `make test` definition of
done is still owed. What exists:

```sh
build/dist/viki ndvss-selftest              # proves sqlite-ndvss is really statically linked
build/dist/viki embed-selftest [model-dir]  # semantic property check, not just "didn't crash"
```

Both are hidden subcommands (not in `usage()`) and both run automatically at the
end of `build/build.sh`. CI (`.github/workflows/build.yml`) runs the same build
+ smoke test on linux-x86_64, linux-arm64 (experimental), macos-arm64, and
windows-x86_64.

Everything beyond that is manually verified against a scratch fossil-see repo
and written up in AGENTS.md's "Verified working end to end" section. **Match
that standard**: prove a semantic property or a real round-trip, not that a
command exited 0 — and record honestly in AGENTS.md what you did *not* verify
(forum-post extraction is the current example: implemented, never round-tripped
against a live post).

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
if `(content_hash, model_id)` is absent. Four content types share one
`index_text_blob()` sink, with virtual paths distinguishing the non-file ones:

| Source | How it is extracted | Virtual path |
|---|---|---|
| checkout files | directory walk | `./relative/path` |
| wiki pages | `fossil wiki` subprocess | `wiki:Name` |
| tickets | `fossil ticket` subprocess | `ticket:UUID` |
| forum posts | `fossil sql` against `event`/`blob` (no export subcommand exists) | `forum:UUID` |

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
  tokenizer, no GC of orphaned chunk rows, no epoch-migration path for a model
  change.
- **Out of scope for Milestone 1**: Flutter app, VPS deployment, calendar
  projection, voice, MCP server. Do not start them.
