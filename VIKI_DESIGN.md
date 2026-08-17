# viki — the memory layer

**viki is the project name and the verb.** Humans, apps, and agents all "ask
viki." Underneath: fossil-see (sync, identity, encryption at rest, wiki,
tickets, unversioned-file distribution) + SQLite FTS5 (keywords, already in
the binary) + sqlite-ndvss (vector similarity scan) + one pinned ONNX
embedding model (meaning).

## The core rule (D-10)

**Vectors are projections; viki is a protocol, not an index.** No embedding is
ever source of truth. Content (wiki artifacts, docs, tickets, events, field
notes) syncs as Fossil artifacts exactly as before. Anything queryable —
FTS5 tables, embedding caches — is derived, rebuildable, and disposable.

## The rung table

| Rung | Cost | What it gives | Who runs it |
|------|------|---------------|-------------|
| 0 | 0 MB — FTS5 is already compiled into fossil-see | BM25 keyword search over the whole corpus, offline | everyone, always (floor + exact-match layer) |
| 1 | single-digit MB (static/Model2Vec-style lookup embeddings) | cheap fuzzy matching | optional; superseded by rung 2 in practice |
| **2** | **~25 MB quantized MiniLM-class ONNX + runtime (~40–50 MB app-bundle bump total)** | **real sentence semantics, offline** | **the universal standard (D-11): every device and agent** |
| 3 | unbounded | best-available embeddings/reasoning | cloud agents, per epoch experiments |

Decision: **rung 2 is universal.** Rung 0 remains built-in and free; hybrid
queries (FTS5 candidates ∪ vector neighbors, rank-fused) are the default
query path, degrading to pure BM25 only if the model is somehow absent.

## Pinned model = shared memory (D-11)

Because every peer runs the *same* model, an embedding is a deterministic
function of `(content_hash, model_id, chunk_params)` — so it is computed
**once, by whoever sees the content first** (usually the hub or the next
agent to sync), and shared. No per-device re-indexing. An agent waking cold
does: pull → load cache → instant semantic recall over every project. A
"large vague memory," identical on every peer.

Float caveat: ONNX inference is near- but not bit-reproducible across
backends. Rows are keyed by content hash + model id, never by hashing the
vector; cosine similarity is indifferent to last-decimal wobble. Duplicate
computation by two offline peers converges harmlessly (same key, equivalent
value, last-write-wins).

## Distribution: unversioned files (D-12)

Embedding caches — and **the pinned model file itself** — travel as Fossil
*unversioned* files (`fossil uv`): synced through the same hub, same
accounts, same TLS, but latest-wins with no history. Versioned artifacts
would entomb every superseded vector in immutable history; uv files cost
nothing when replaced. Consequence: viki is self-contained — a fresh clone
pulls corpus + model + embeddings from one endpoint, no third-party
downloads at runtime.

`viki-manifest` (a small versioned file) pins the current epoch:
`model_id`, model uv-path + checksum, chunking params, ndvss/schema version.

As built (`src/viki_cache.c`), the uv names are `viki-cache.db` for the
embedding cache and `viki-model/{model.onnx,vocab.txt,viki-manifest.json}`
for the model. `viki cache push` publishes both by default — model
distribution is opt-*out*, because an opt-in flag makes the ordinary push
leave a hub on which the self-containment claim above is false, and says
nothing about it. The published `viki-manifest.json` doubles as the
skip-check: identical manifest ⇒ identical epoch ⇒ the ~23 MB model is not
re-pushed (fossil's own `uv add` has no such check — see FINDINGS.md).
`viki cache pull` verifies each blob against the manifest's recorded sha256
before installing the manifest, so a corrupt or truncated model is caught
before ONNX Runtime is asked to load it, and a hub with no model published
degrades to BM25-only rather than failing.

## Epochs

A better model someday = **epoch bump**: update viki-manifest, one agent
re-embeds the corpus overnight, uv cache and model file swap, done. The
`model_id` column lets two epochs coexist during migration. Never a
flag-day for content — only for the disposable layer.

## Local schema (per peer, derived)

```sql
CREATE TABLE viki_chunk(
  content_hash TEXT,     -- fossil artifact hash (or uv name) of the source
  model_id     TEXT,     -- epoch pin
  chunk_ix     INT,      -- position within source
  chunk_text   TEXT,     -- for display/snippets (also FTS5-indexed)
  embedding    BLOB,     -- ndvss vector
  PRIMARY KEY(content_hash, model_id, chunk_ix)
);
-- FTS5 table over chunk_text mirrors rung 0.
-- Query: FTS5 top-K ∪ ndvss cosine top-K → reciprocal-rank fusion.
```

ndvss is brute-force by design — at personal scale (≤~100k chunks) a linear
scan is milliseconds. If viki outgrows that, swap sqlite-vec under the same
schema; the protocol doesn't change.

## Component naming

- **viki** — the whole system (formerly "fossil-app") and the query verb.
- **fossil-see** — the refactored encrypted Fossil build (SEE scaffolding +
  SQLCipher/LibreSSL). README should carry one disclaimer line: *uses
  Fossil's SEE build scaffolding with SQLCipher as the codec; contains no
  SEE code and requires no SEE license* — avoids confusion with Hipp's
  proprietary SEE product.
- **sqlite-ndvss** — vendored vector extension (MIT, single C file,
  statically linked like SQLCipher).

## Agent contract addition (extends US-2)

A viki query is an ordinary request: locally answered in the app when the
model is present; posted as a request artifact when the asker wants a
better-equipped peer to answer. Agents answering viki queries cite
content_hashes so every answer links back to source artifacts — vague memory,
precise receipts.
