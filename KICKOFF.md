# viki — agent kickoff brief

You are a coding agent starting the first implementation milestone of viki.
Read these before writing any code, in this order:

1. `USER_STORIES.md` — what viki is; design principle 0 (respect the user's
   focus); decisions D-1..D-12 are settled — do not relitigate.
2. `ARCHITECTURE.md` — hub-and-spoke; fossil protocol is the only transport.
3. `ENCRYPTION.md` — fossil-see build (Fossil 2.28 + SQLCipher/LibreSSL),
   threat model, key handling. Fossil is PINNED at 2.28.
4. `VIKI_DESIGN.md` — memory layer: FTS5 + sqlite-ndvss + pinned ONNX model,
   uv distribution, epochs.
5. `FFI_RISK.md` + `experiments/` — verified findings about in-process fossil;
   `experiments/harness.c` is the working proof and regression test.

## Milestone 1: `viki` core CLI (no Flutter yet, no VPS yet)

Build a repo layout with a `viki` command-line tool (C or C+small-script glue;
match the fossil-see aesthetic: static, few deps) that against any fossil-see
checkout provides:

1. `viki index` — walk checkout files + wiki artifacts; chunk; maintain the
   derived store from VIKI_DESIGN.md (`viki_chunk` + FTS5 mirror) in a local
   cache db (NOT inside the repo db). Incremental by
   (content_hash, model_id, chunk_ix). Embeddings via ONNX Runtime with the
   pinned model named in `viki-manifest` (create the manifest; pick a
   well-supported quantized MiniLM-class model; record checksum).
2. `viki ask "<query>"` — hybrid retrieval: FTS5 BM25 top-K ∪ ndvss cosine
   top-K → reciprocal-rank fusion → results with source content_hash,
   snippet, score. Works with no model present (BM25-only, degraded flag).
3. `viki cache push` / `viki cache pull` — embedding cache + model file as
   fossil unversioned files (`fossil uv add/sync`), per D-12.
4. Vendor sqlite-ndvss (static link, like SQLCipher). Do not fork it.

## Constraints and known traps (all empirically verified — believe them)

- Fossil 2.28 only (SQLCipher baseline is SQLite 3.53.1; 2.29 requires 3.54).
- **Vendor `fossil-see` directly** (git submodule at `vendor/fossil-see`,
  pointing at https://github.com/wmacevoy/fossil-sqlcipher-libressl) and build
  it with `vendor/fossil-see/build/build.sh` — do NOT hand-apply
  `fossil-db-key.patch`/`fossil-server-key-validator.patch` yourself; that
  build already applies both (server dies NOTADB without the validator patch,
  which is exactly why it's there). Repos are `*.efossil`; key via
  `FOSSIL_SEE_KEY` env in tests (renamed from `FOSSIL_PPV_KEY` when the patch
  moved into fossil-see — see ENCRYPTION.md).
- If you embed fossil in-process: apply `experiments/db-embed.patch`
  (a later fossil_fatal deletes earlier-registered files without it — we
  watched it delete a repository), call the clear function + sqlite3_shutdown
  between commands, set `backoffice-disable=1` on every repo, never rely on
  interactive prompts (`clone --save-http-password`, pass `--user`), one
  repository per process lifetime.
- Autosync commits can hit server check-in locks ("Might fork / parent
  locked") — handle by update-and-retry; it's a feature.
- Embeddings/model/caches are NEVER committed as versioned artifacts — uv
  files only (D-12). Vectors are never source of truth (D-10).

## Definition of done for milestone 1

- `make test` (or equivalent) proves end-to-end on a scratch encrypted repo:
  init → add sample docs + wiki → `viki index` → `viki ask` returns the
  planted answer both with and without the model present → `viki cache push`
  then fresh clone + `viki cache pull` → `viki ask` works WITHOUT re-indexing
  (the compute-once claim, D-11).
- Re-run `experiments/harness.c` against your fossil-see build — still ALL
  PASS (regression gate).
- A brand-new agent can orient from repo contents alone (US-2c): keep an
  AGENTS.md current with layout, commands, and any new findings.

## Process rules

- Work in this `viki` repo/folder. Vendor `fossil-see` as a git submodule
  (the shared build both viki and pizza-party-vote-fossil now depend on);
  treat it, and `pizza-party-vote-fossil` if referenced for anything else,
  as read-only upstream (vendor, don't edit).
- Small commits with reasons; anything surprising you discover goes in a
  `FINDINGS.md` with a repro, in the spirit of FFI_RISK.md.
- When a design doc conflicts with reality, update the doc in the same
  commit as the code — docs are source of truth here.
- Out of scope for M1: Flutter app, VPS deployment, calendar projection,
  voice, MCP server. Do not start them.
