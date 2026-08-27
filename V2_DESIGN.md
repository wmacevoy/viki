# V2_DESIGN -- escalation by available resources, and range-addressed chunks

**Status: PROVISIONAL.** Written 2026-08-26 from a design conversation, before
any code. **Nothing here is implemented** -- but SS 8.1 and SS 8.2 have since
been MEASURED against viki's own SQLite (both answered, and the answer is a
trade rather than a yes), and SS 10's item 2 (overlap) shipped the same day. It is recorded so the reasoning is not
re-derived, not because it is settled.

Read `SCOPES.md` first for the L0-L3 split and the placement test; this file
assumes them and does not restate them. Read `CLAUDE.md` for what v1 actually
does. Where this file and reality disagree, reality wins and this file is stale.

**What v1 has shipped that this file assumed was still ahead** (checked
2026-08-26, and the reason to check again before building anything here):

- **Overlap** (SS 10 item 2) -- done and measured; see that item.
- **The `(model, chunking)` epoch key** -- done. SS 3c is written as though it
  is still load-bearing; it is, until ranges land, and SS 3c already says it
  survives as belt-and-braces rather than being reverted.
- **`--k` truncates rather than steering retrieval**, and the candidate pool is
  a tuned constant (80). Anything here that reasons about pool depth should use
  that number, not the old `topK * 4`.
- **The serve page cites its sources.** SS 9's citation break therefore touches
  FOUR surfaces, not three: CLI, `/api/ask`, the wasm edge, and now the HTML
  page.
- **`viki fts-query` / `--pool`** exist as hidden subcommands, because
  `test/retrieval-eval.py` may not mirror `src/`. Anything v2 adds that the
  harness needs to know should arrive the same way.

**And what this file resolves for v1**, recorded in `VIKIVERSE_V1.md` in place:
SS 6a/6b decide the shape of v1's SS 2.11 ("One API, several consumers"), and
SS 3/SS 5 decide how v1's remaining P0.4 work gets built -- which is why
structure-aware chunking should NOT be built under the current schema.

Every claim below is tagged:

    DECIDED     Warren settled it in conversation, quoted and dated.
    ANALYSIS    derived here, not measured, not confirmed by him.
    UNVERIFIED  a factual claim about SQLite/FTS5 nobody has tested.
    OPEN        a real question with no answer yet.

---

## 0. The two problems v2 exists to solve

1. **Resources are not uniform across peers.** A laptop can run a much better
   embedding model than a phone. D-11 pins ONE model universal across peers
   precisely so an embedding is a deterministic function of
   `(content_hash, model_id, chunk_params)` -- which is the property "escalate
   by available resources" appears to break.

2. **A resource-poor device must be able to accept content anyway.** Capturing
   on a phone today forces a chunking decision the phone is not equipped to
   make, and bakes it into rows that are expensive to revise.

Both turn out to be the same problem, and SS 1 is the answer to both.

---

## 0a. Decision register -- what was chosen, what was refused, and why

Compact index. Each line is expanded in the section named. **The refusals matter
as much as the choices**: most of them are ideas that look right until a viki-
specific constraint kills them, and without the reason recorded they come back.

**Escalation**

- **Escalate at query time by default; the epoch only by tribe decision** (SS 1).
  *Because* category B costs a differing peer nothing, while category A is the
  shared artifact. **Refused: per-device epochs chosen freely** -- vectors from
  different models are not comparable, so D-11's compute-once-share-many dies and
  each device silos.
- **A daemon-served local model is category B only** (SS 1a). *Because* the same
  model name across two daemon versions or quantizations produces different
  vectors. **Refused: Ollama/LM Studio for indexing** -- it reintroduces exactly
  the key-collision class `(model, chunking)` was just added to close.

**The ladder**

- **Rungs 0 and 1 compress into one base tier** (SS 2a). *Because* they already
  fuse into one RRF pool, so the merge is free -- and because it makes capability
  order agree with dependency order. **Refused: keeping them separate** -- rung 1
  needs *fewer* dependencies than rung 0 (literal needs no FTS5 module), so the
  two orderings disagreed and `--max-rung 1` would have been ambiguous about
  whether BM25 was included.
- **Tiers stay separate rather than fused** (SS 2g). *Because* RRF across legs of
  unequal quality dilutes the strong signal. **Refused: fusing a pinned and a
  larger epoch into one pool**, pending measurement.
- **The ladder lives in viki, not above it** (SS 2c). *Because* the caller
  supplies the bound and viki reports the fact -- neither half is an opinion.
  **Refused: leaving it to L3** -- a consumer cannot detect engine availability
  from outside the process, so it would have to guess.
- **Default stays a bare `ask`** (SS 2c). *Because* the agent wants "just ask",
  and `minCos` already sets the precedent that a knob defaults to off.
  **Refused: a required rung argument** on every call.
- **Engines are named, not numbered** (SS 2e). *Because* rung numbers become
  frozen API the moment a consumer depends on one, and inserting a tier later
  breaks them all. **Refused: `--max-rung N`.**
- **A ceiling and a floor are both needed** (SS 2d). *Because* a ceiling bounds
  cost and a floor bounds correctness, and the missing floor is why
  `PASS WITH SKIPS` can exit 0 with meaningless numbers. **Refused: one knob.**

**Chunks and storage**

- **Chunks become ranges over blobs** (SS 3). *Because* it lets a phone accept
  content without making a chunking decision, and because the extent moves into
  the key so cross-peer collisions become unrepresentable. **Refused: keeping
  `chunk_ix`** -- an ordinal whose meaning depends on the params that produced it,
  forcing text to be re-stored on every re-chunk.
- **A lazy, evictable shadow table holds materialized chunk text** (SS 4b).
  *Because* zlib has no random access and **viki links no zlib**, so decompressing
  per snippet is the expensive path. **Refused: `substr` on demand** (inflates a
  whole document per snippet). **Refused also: calling it a wasteful duplicate** --
  that framing measured the wrong thing; blob and shadow row have different roles.
- **`contentless_delete=1` for the index** (SS 4c). *Because* it makes eviction
  safe at any time and retires the delete-order trap entirely. **Refused: pointing
  `content=` at the evictable shadow table** -- evicting a row starves FTS5's
  delete and the withdrawn text stays searchable, which is H11's bug through a new
  door and far harder to reproduce.
- **The eviction bound is supplied by the caller** (SS 4d). *Because* a threshold
  is a judgment and judgments are L3 -- the same rule that keeps a staleness number
  out of `coverage`. **Refused: a hardcoded budget in `src/`.**
- **Overlap first, the tree later** (SS 5c). *Because* overlap is cheap and has no
  ranking consequence, while multi-scale matching carries the whole cross-scale
  problem. **Refused: shipping them together.**
- **Embed leaves only; expand ranges for context** (SS 5b). *Because* it costs zero
  extra vectors. **Refused: a full embedded tree** -- ~2n embeddings for a ranking
  that leaves win anyway (SS 5a).

**Interfaces**

- **MCP as a local stdio face** (SS 6a). *Because* one artifact covers Claude Code,
  Claude Desktop and Cowork, with no auth, no TLS and no hub exposure. **Refused:
  building the remote authenticated hub server first** -- only claude.ai in a
  browser needs it.
- **`viki sql` stays off the MCP face** (SS 6b). *Because* of aggregation: `ask --k 5`
  returns five chunks and `sql` returns the corpus in one call. **Refused:
  exposing it for face parity** -- that is the injection jackpot, and it resolves
  SCOPES SS 7.4 on better grounds than the loopback argument did.
  **CORRECTION 2026-08-26, measured on a prototype: excluding `sql` is NOT
  SUFFICIENT, and stating the rule as "no `sql`" invites exactly the hole that
  appeared.** A first MCP build honoured the exclusion and then exposed `grep`
  with an unbounded `k` -- `{"pattern":".","k":2000000000}` returned the entire
  index in one call. The property SS 6b is protecting is **bounded response
  size**, and `sql` is only the most obvious way to violate it. Any tool taking
  a caller-supplied count needs a server-side ceiling, and the face is where it
  belongs (SS 6c: an MCP client holds no key, so a server-side limit is a real
  capability bound for it). Restate the rule as the property, not the tool.
- **Scope is enforced in the face, not only in the data** (SS 6c). *Because* an MCP
  client holds no key and no SQL, so a server-side filter is a real capability
  limit for it -- unlike an in-cache label, which SYNC.md 0b makes advisory.
  **Refused: relying on a librarian alone** -- a detective control as the only
  control means finding out afterward.
- **Domain, identity and tribe are three axes** (SS 6c). *Because* one person has
  many domains and one domain can have many people. **Refused: domain-as-tribe** --
  every domain would need its own repo, key, sync and cache.

**Refused outright** (SS 7): a SQLCipher patch for access control; per-domain
encryption of a searchable corpus; ANN; an engine swap; GraphRAG; LLM-generated
contextual retrieval. Reasons in SS 7 -- each is viki-specific, not a general
verdict on the technique.

---

## 1. The organizing split: does it write to the cache?

**ANALYSIS.** Every escalation is exactly one of two kinds, and the difference
decides whether peers must agree.

| | **A. epoch-forming** | **B. query-time** |
|---|---|---|
| examples | a different embedding model; a different chunking | rerank; HyDE / multi-query; fusion weights; `minCos` |
| touches | `viki_chunk` -- the shared artifact | nothing, reads only |
| D-11 applies | **yes** | no |
| determinism required | **yes** | no |
| cost of peers differing | cache fragmentation | **nothing** |

That last row is the whole design.

**Query-time escalation is free to be non-uniform across peers.** A laptop that
reranks and a phone that does not still share one cache, one set of vectors,
one set of `content_hash`es. Results differ in *quality*, never in *identity*.
There is nothing to reconcile, no epoch, no re-index, no negotiation.

Epoch-forming escalation is the opposite: it is the shared artifact, so it is a
tribe-wide decision rather than a per-device one.

> **The rule: escalate at query time by default. Escalate the epoch only by
> tribe decision.** Everything else in this file is mechanism.

### 1a. The split predicts which tool is allowed where

**ANALYSIS, and this is the test that shows the split is real rather than tidy.**

A locally installed model served by a daemon (Ollama, LM Studio) is
**disqualified from category A and fine for category B**. D-11 needs an
embedding to be a deterministic function of its inputs, and a serving daemon
does not provide that: the same model *name* across two daemon versions -- or
two quantizations, q4 vs q8 -- produces **different vectors**. Peers would then
write rows agreeing on the epoch id and disagreeing on the contents, which is
the exact failure class the `(model, chunking)` epoch key was just introduced
to close (CLAUDE.md, `viki_cache_epoch_id()`), arriving again through another
door.

So:

- **category A wants a pinned file with a checksum** -- ONNX, pinned runtime,
  distributed as a uv blob, verified against the epoch pin, exactly as today.
- **category B may use the daemon freely.** Rerank, HyDE generation, query
  expansion: nothing is stored, so non-determinism is harmless.

---

## 2. The rung ladder

`viki` already says "rung" for its retrieval legs. v2 keeps the word and makes
it a first-class, reportable property.

### 2a. Rungs 0 and 1 compress into one base tier -- DECIDED

> *"compress 0 & 1 into the base level -- that is minor and keeps the
> ranking."* -- Warren, 2026-08-26

Free: BM25 and the literal leg already fuse into one RRF pool, so merging them
is a labelling change with **no scoring consequence and nothing to re-measure**.

**ANALYSIS of why it is worth doing at all, beyond tidiness:** before the merge
the ladder had *two orderings and they disagreed*.

    by capability:   0  <  1  <  2       bm25 < literal < vector
    by dependency:   1  <  0  <  2       literal needs LESS than bm25

The literal leg is a plain scan of stored text: no FTS5 module, no ndvss, no
model -- and it is **the only rung a stock `sqlite3` can reproduce**, since rung
2 needs `ndvss_cosine_similarity_f` (the reason `viki sql` exists) and rung 0
needs FTS5 compiled in, which CI's macOS runner does not have. So a numeric
`--max-rung 1` would have been ambiguous about whether BM25 was included, and an
embedded agent asking for it could still break on a missing module. Compressing
them removes the ambiguity by making the two orderings agree.

**Note the price of the merge, and do not pay it accidentally:** "zero cost" for
the literal leg means **zero dependency cost, not zero query cost**. It is a
linear scan with a second pass for `1/df`, so as a corpus grows it becomes the
*slowest* leg while BM25 over an index stays sublinear. The ladder is not
monotonically cheaper going down and should never be described as if it were.

    base    no vector space         lexical: bm25 + literal
    1       pinned model            epoch <pinned>/c<lines>
    2       larger model            epoch <larger>/c<lines>

### 2b. After compression, the rung IS the epoch -- ANALYSIS

"Which rung answered" == "which `model_id` the vector leg filtered on" -- a
value the cache already stores and the code already filters by. The rung stops
being a new concept and becomes a name for something that exists.

Two consequences:

- **Rerank falls off the ladder, correctly.** It is category B and applies on
  top of *any* tier -- base results can be reranked. So it was never a rung; it
  is an orthogonal modifier. The ladder is exactly the category-A axis, and
  everything category B sits beside it rather than on it. SS 1 made structural
  instead of remembered.
- **The only cost bound anyone needs is `base`**, i.e. "do not touch a model".
  Within-tier availability needs no knob: an embedded agent with no FTS5 module
  gets literal-only automatically, because degraded mode is already a required
  path (CLAUDE.md). **The report does the work a constraint was going to do.**

### 2c. It belongs in viki -- DECIDED

> *"it belongs in viki -- this gives a simple surface to work from. it could
> have optional constraints --rung<2 and report the engine for the consumer to
> know. but the agent wants a 'just ask'"* -- Warren, 2026-08-26

**ANALYSIS of why that passes the placement test:** the caller supplies the
bound, viki reports the fact. Neither half is an opinion. A rung ceiling is the
consumer's judgment about its own budget; "the literal engine answered this" is
a fact about the computation, exactly as `coverage` prints last-seen times with
no threshold. By contrast "this query is worth a remote round trip" *is* a
decision, and stays L3.

Precedent for the default polarity is already in the code: `minCos` defaults to
disabled because measurement killed a default-on threshold, and `viki_ask_opts`
exists so "adding a knob never breaks `viki serve`". A rung bound lands in that
same seam.

### 2d. A ceiling and a floor are different needs -- ANALYSIS

- **ceiling** = a *cost* bound. "Do not escalate past base."
- **floor** = a *correctness* bound. "Fail if you cannot reach rung 1."

The floor is not decoration. `test/m1.sh` prints `PASS WITH SKIPS` and exits 0,
so a benchmark that silently drops to base produces numbers **worse than no
numbers**. `{off, opportunistic, required}` collapses into exactly this pair:
`required` is a floor.

Debugging wants an exact tier, expressible as floor == ceiling.

### 2e. Name the engines, do not number them -- ANALYSIS

Once `--max-rung 2` is public, rung *numbers* are API, and inserting a tier
later breaks every consumer. `VIKI_LEG_*` constants already exist, so names are
nearly free and survive insertion.

Shape, with "just ask" preserved as one required argument:

    ask(query)                        best available; engines reported
    ask(query, engines: [literal])    cost bound
    ask(query, require: [vector])     correctness bound

### 2f. Report at two levels -- ANALYSIS

`viki_ask_result.legs` already carries per-hit leg attribution, commented
"internal bookkeeping". v2 is a **surfacing job, not a plumbing job**.

- **per result set** -- which engines were available, which ran. This is what
  stops a consumer silently trusting a base-only ranking as semantic.
- **per hit** -- which legs contributed. **A literal-leg hit is an exact
  substring match and is citable as such; a BM25 or cosine hit is not.** That
  is the one per-hit fact an agent can act on, and it is currently discarded.

**Compress the tier, keep the attribution.** The merge in SS 2a must not lose
the literal/BM25 distinction at hit level.

### 2g. Tiers stay separate rather than fused -- DECIDED

> *"i think leave them separated"* -- Warren, 2026-08-26, answering whether a
> pinned and a larger epoch should fuse into one candidate pool.

**Why:** RRF across legs of unequal quality dilutes the strong signal -- a weak
epoch contributes ranks that displace a strong epoch's, and the fused result is
worse than the better input alone. Separate tiers also keep SS 2f's report
meaningful: "rung 2 answered" is a fact only while rung 2 is a distinguishable
producer.

**What this refuses:** blending epochs to "get the best of both". If a query is
served by more than one epoch, the better one wins outright rather than voting.

**What it leaves open:** whether best-epoch-wins is measurably right. It is the
lean, not a result -- see SS 8.3.

**Practical scope, from the same exchange:** *"i will probably only divide at
rung 2-3"* -- the base/vector boundary is the one that matters in daily use; the
finer tiers exist for the platform, not for Warren's own retrieval.

---

## 3. Range-addressed chunks

### 3a. The shape -- DECIDED

> *"can chunks be (model_id,vector,start,end,blob_id) tuples. store the
> artifact once, recycle as needed?"* -- Warren, 2026-08-26

Today `viki_chunk` is `(content_hash, model_id, chunk_ix, chunk_text,
embedding)`, PK `(content_hash, model_id, chunk_ix)`. `chunk_ix` is an ordinal
whose meaning depends on the params that produced it, and the text is stored
per chunk.

v2: a chunk is a **range over a blob**, and the text is not stored in the chunk
row at all.

### 3b. Why: offline capture stops requiring a commitment -- ANALYSIS

This is the strongest argument and it answers SS 0.2 directly. Under ranges the
phone stores **the blob and nothing else** -- bytes and a hash. No chunking, no
vectors, no model, no judgment. A laptop later adds `(model_id, vector, start,
end)` rows over that same blob, and can add a *second* set with different
boundaries without disturbing the first.

That extends D-11 in the natural direction: today embedding is
compute-once-share-many; this makes **chunking** compute-once-share-many too.
The device with resources does the work; the device without one still
participates fully. The same ladder as SS 2, applied to ingest.

### 3c. Why: the key becomes collision-proof -- ANALYSIS

Under `(content_hash, model_id, chunk_ix)` two peers with different
`VIKI_CHUNK_LINES` wrote rows that agreed on the key and disagreed on the text,
and `INSERT OR IGNORE` silently double-indexed (FINDINGS.md). That is what
forced chunking into the epoch id.

Under `(blob_id, model_id, start, end)` **that collision is unrepresentable** --
different boundaries produce different keys by construction. The extent is *in*
the key instead of implied by it.

- The cache moves from SYNC.md's `derived`/latest-wins class into **`grow-only`
  on a content key** -- immutable rows, union IS merge, multi-writer safe. That
  is the strongest class in the sync policy and where a multi-peer cache wants
  to be.
- **Chunking can leave the epoch key.** It is in `model_id` today only because
  the boundary was implicit. Once explicit in the row, `model_id` can mean the
  model again, as D-11 originally said. Keep a `chunking_id` for provenance --
  *which policy drew these lines* -- as metadata, not identity. The existing fix
  becomes belt-and-braces rather than load-bearing, **without being reverted**.

### 3d. Overlap becomes free -- ANALYSIS

Today overlap duplicates the overlapped text into two rows. Under ranges,
overlapping ranges are just ranges: zero storage cost. This matters because
overlap is the cheapest known chunking improvement and needs no LLM.

### 3e. Honest deflation -- ANALYSIS

**It is not a storage win today.** Chunks do not currently overlap, so total
`chunk_text` is approximately document size, and blob + text-free chunks is
approximately the same. A wash.

The saving appears only once the flexibility is used -- overlap, or two
chunkings coexisting. So the win is real but **contingent on doing the thing
this enables**, not a free rewrite dividend.

---

## 4. Storage: blobs, the shadow table, eviction

### 4a. Blob storage is optional where a repo is present -- ANALYSIS

> *"even now, if viki holds a book, you can check out the source as a single
> fusion artifact, without having to re-represent the chunks."* -- Warren,
> 2026-08-26

With a repo, every blob is recoverable: file content directly, and composed
sources (`ckin:`, `wiki:`, forum, ...) recomputable because the composition
recipe is frozen. So a peer with a repo needs ranges and vectors only; a peer
without one (phone, wasm edge) needs materialized blobs. That is
`caching = {none, optional, required}` applied one level down, and it makes the
blob table itself a projection -- droppable where it can be re-derived.

**The asymmetry to keep in view:** file-backed content resolves from a single
artifact; composed content resolves from a *recipe*. That quietly promotes the
composition recipe from "sharing contract" (CLAUDE.md) to "part of the
resolution path" -- already frozen, but now load-bearing in a second place.

`blob_id` should be keyed by `content_hash` so the identity is the same one
Fossil uses and the relationship stays checkable.

### 4b. A lazy, evictable shadow table -- DECIDED

> *"the shadow table is lazy and model dependent, substr sounds very expensive
> for compressed blobs, i think the shadow table is the way to go, especially
> with some caching information to know when to delete it."* -- Warren,
> 2026-08-26

**This corrects an earlier framing in this same conversation.** The 34-36%
saving from external-content FTS5 was measured in a world where `chunk_text`
was the *only* representation and the FTS copy was pure duplication -- same
bytes, same role, twice. Under blob + ranges the two have **different roles**:
the blob is cold storage, the materialized text is a decompression cache. That
is a working set, not a duplicate, and "it reverses the 36%" was measuring the
wrong thing.

The compression argument is decisive: **zlib has no random access**, so
`substr(blob, start, len)` inflates from the beginning -- every snippet on a
book-sized artifact decompresses megabytes, and an FTS rebuild does it once per
chunk. Worse for viki specifically: **viki links no zlib today**, which is why
`index_unversioned()` forks `fossil unversioned cat` per file and accepts
O(blobs) subprocesses there (`src/viki_index.c`). Whole-blob compression would
either add that dependency or drag that pattern into the read path.

Lazy is right and has a property worth naming: **a device that never queries a
document never materializes it.** The phone's working set stays proportional to
what it reads, not to what it holds.

**Keying:** the shadow row is a function of `(blob_id, start, end)` -- the
extent, not the blob -- so two epochs with different boundaries materialize
different rows over the same blob and evict independently. Re-chunking then
invalidates materialized text without touching cold storage.

### 4c. THE TRAP: eviction resurrects the H11 bug -- ANALYSIS

**If FTS5's `content=` points at the shadow table AND the shadow table is
evictable, H11's defect returns through a new door.** Evict a row whose tokens
are still in the index, and FTS5's next DELETE for it reads nothing, removes
nothing, and **the withdrawn text stays searchable** -- the same failure
`gc_orphan_chunks()` was ordered fts-then-chunk to prevent, now triggered by
cache pressure instead of delete ordering, and far harder to reproduce.

Proposed resolution -- combine rather than choose:

> **`contentless_delete=1` for the index, shadow table for excerpts.**

- deletes never read back, so nothing structural depends on a shadow row
- eviction is safe by construction, at any time, for any row
- a snippet on an evicted row pays the decompression it would have paid anyway
- **the delete-order trap retires**: H11 becomes a test of something that can no
  longer fail

Cost: `snippet()`/`highlight()` are unavailable on a contentless table, so
excerpt selection moves into C. That lands in code that already exists --
display-side fragment marking -- and puts all three markers (` ... `,
`<<document continues ...>>`, `<<excerpt truncated>>`) in one place instead of
two, with viki owning the ` ... ` it currently inherits from FTS5.

`viki_db.c`'s current comment rejects `content=''` because "it breaks
`snippet()`, which every surface and both fragment markers depend on." **That
was correct then and stops being correct under blob + ranges**, because the
reason contentless was unattractive -- no other way to reach the text -- is
exactly what this design removes.

### 4d. The eviction bound is a policy number -- ANALYSIS

"Keep 50MB", "evict after 30 days" is a threshold, and by SCOPES SS 3 that is
L3 -- the same rule that keeps `coverage` from having a staleness number. viki
may evict **to a bound the caller supplies**; choosing the bound belongs above
it. The caching metadata itself (last_used, bytes) is opinion-free and belongs
in the row.

### 4e. Compression granularity is the real decision -- OPEN

Whether `substr` is expensive is a question about compression, separable from
the shadow table:

    whole-blob compressed   substr is O(document); shadow table is STRUCTURAL
    page-compressed         a chunk touches 1-2 segments; shadow table is SPEED
    uncompressed            substr is free; shadow table is SPEED

Worth deciding deliberately rather than inheriting, because it determines
whether the shadow table is an optimization or a requirement. And worth asking
whether blob compression is needed at all: the size pressure actually measured
was on the **distributed artifact**, which is a transport concern, not a storage
format. Note SQLCipher output does not compress meaningfully, so
compress-before-encrypt is the only ordering that helps there (SYNC.md).

---

## 5. Multiresolution -- the wavelet idea

> *"Also makes the wavelet idea fairly simple (chunks can overlap, or be a log n
> tree)."* -- Warren, 2026-08-26

**Representationally free** under SS 3: a tree is just a set of ranges, no schema
beyond the tuple. The problem is not representation.

### 5a. Cross-scale cosine ranking does not work -- ANALYSIS

Long-text embeddings drift toward the centroid of their content, so a broad node
matches everything weakly and a tight leaf matches its topic strongly. Pool all
levels into one candidate set and **leaves win essentially always** -- the tree
buys nothing, and you have paid ~2n embeddings for it (n + n/2 + n/4 + ... = 2n).

It is viki's existing density bias, sharpened: now a concentrated chunk is being
compared against a deliberately diluted parent, in the same ranking. Cross-scale
retrieval therefore needs per-level normalization (a node competes only within
its level) or a traversal (score the top level, descend into promising
subtrees) -- **not a flat top-k**.

### 5b. The cheap version gets most of the value -- ANALYSIS

Embed **leaves only**; use structure purely for *context expansion*. Match small,
return big -- the parent is a wider `(start, end)` over the same blob and never
had to be embedded. Standard "small-to-big", and under range-addressed chunks it
costs **zero extra vectors** and has no ranking problem at all.

### 5c. Overlap and the tree solve different problems -- ANALYSIS

Overlap fixes boundary-straddling answers, is cheap, and has no ranking
consequence. The full tree is a multi-scale *matching* change and carries all of
SS 5a. **Overlap is the safe win; the tree is the research project.** Do not
conflate them in one round.

### 5d. It enables a verb the fragment markers have been missing -- ANALYSIS

`<<document continues above/below>>` currently tells an agent there is more and
gives it no way to get it: the marker is a dead end. With ranges, expansion is a
real operation -- return `hash#start-end` widened by N, or by one level. It is
opinion-free (the caller says how much) and fits "just ask" as an optional
argument rather than a new verb.

---

## 6. Interfaces and scoping

Reasoned in the same conversation, before SS 0-5. Kept because it produced
decisions and two closed branches.

### 6a. MCP is a face, not a connector -- ANALYSIS

Consumers (openclaw, nanoclaw, Claude Cowork, Claude Code) are L3 **consumers**.
A consumer does not need a connector *into* viki; it needs a **face**, and
SCOPES SS 5 already calls MCP the third peer face rather than an integration.
Anything that can fork a process has full access today via the CLI.

**Local stdio MCP is the small build and covers Claude Code, Claude Desktop, and
Cowork in one artifact.** Cowork reaches local resources through the Claude
Desktop app: MCP servers configured in `claude_desktop_config.json` run **on the
host**, not in Cowork's VM, and Desktop bridges them in. So no auth, no TLS, no
hub exposure. Only claude.ai in a browser would need the remote authenticated
version -- a separate, later decision.

Note `viki serve` does **not** back-door this: it is loopback-only plus
`X-Viki-Local`, and a request from a VM is not loopback from the host.

### 6b. Auth is not the control that matters here -- ANALYSIS

The threat is the **confused deputy**: an agent reads an injected instruction and
a fully authenticated client asks viki for everything. Auth answers *who is
calling*, never *whether the caller is currently under someone else's control*.
Prompt injection uses the token. Container isolation (nanoclaw) limits blast
radius after compromise but does not stop it either.

What does help is **capability scoping**, and two forms are available:

1. **Per-tribe servers.** 2.10 is crypto-enforced, so an agent on one tribe
   **cannot** reach another. Stronger than auth or containers, and already built.
2. **Curated verbs only.** `viki sql` should NOT be on the MCP face -- not for
   transport reasons but for **aggregation**: `ask --k 5` returns five chunks,
   `sql` returns the corpus in one call. This resolves SCOPES SS 7.4 on better
   grounds than the loopback argument.

Also: an MCP face exposing no `capture` and no `push` cannot be used to write
poisoned content *into* the tribe, which is how injection gets persistence.

### 6c. Domain is not identity -- ANALYSIS

    identity   who may open the tribe            keys, viki-identity
    tribe      which corpus this is              SQLCipher, 2.10
    domain     what subject content belongs to   nothing yet

One person has many domains; one domain can have many people. Collapsing them
means every domain needs a repo, key, sync and cache.

viki has scope in two disconnected halves today: **`source`** on chunks
(computed, opinion-free, `grep --source` can filter it, but it is *provenance*
not subject) and **`place`** on notes (human-assigned, subject-shaped, notes
only). **`ask` can filter by neither** -- `viki_ask_opts` is one field,
`minCos`.

A **librarian** that notices a scope jump is L3: deciding a jump is alarming is a
judgment, the same mistake `viki brief` made. viki reports the domain of a
result; the librarian decides what a jump means.

**But detection should not be the only control.** A label in the cache is a
guardrail (SYNC.md 0b: a member holds the key and has SQL). That objection does
**not** apply to an MCP client, which holds neither -- its entire capability is
the tool surface. So:

- **scope in the data** -> advisory, detective
- **scope in the face** -> a real capability limit. A domain-bound MCP server
  whose every query is domain-filtered **cannot be talked out of it by prompt
  injection**, because the constraint lives in the server process.

Minimum enabling change: `--source` on `ask` (grep already has it; the opts
struct is the designed seam), plus a declared source->domain map -- a
`.vikidomains` sibling to `.vikiignore`, same one-glob-per-line convention, same
placement, proven mechanism, computed and rebuildable (L1).

Weakness to state: glob-by-path domains are only as good as the directory
layout, and `ckin:`/`wiki:`/`forum:` get whatever their whole namespace gets,
which is probably too coarse.

---

## 7. Closed branches -- do not re-derive these

### 7a. A SQLCipher patch cannot enforce access control -- ANALYSIS

Firebase's rules work because they are evaluated **server-side**, behind a
network boundary, against an identity the client cannot forge. viki is the
mirror image: the file is on the client and **the key is on the client**. A rule
engine compiled into SQLCipher runs in the same process as the key holder, who
can use stock `sqlcipher`, `libfossilsee`, their own SQLite build, or decrypt the
file directly. That is SYNC.md 0b, and moving policy from viki's C into
SQLCipher's C does not move it across any boundary.

Practically it is worse than it looks: SQLCipher lives at
`vendor/fossil-see/vendor/sqlcipher-libressl` -- two submodules deep -- and
`vendor/` is read-only upstream with exactly one deliberate exception
(`sqlite-ndvss`, already Warren's fork). This is not that.

**The right version of the idea is SS 6.2's MCP server**, which is the Firebase
shape on the boundary that actually exists. For defence in depth *inside* that
process, `sqlite3_set_authorizer()` is the stock SQLite mechanism and needs no
patch; viki uses it nowhere today (`SQLITE_OPEN_READONLY` is the current
structural control, and `viki_db.h:39` already prefers structural enforcement
over parsing).

### 7b. A searchable corpus cannot be per-domain encrypted -- ANALYSIS

Encrypt `chunk_text` under a per-domain key and two things stay in the clear:

- the **FTS5 index** -- external content removed the second *text* copy, but the
  inverted index still holds every token and its positions. The vocabulary leaks.
- the **embeddings** -- a vector is not a hash. Embedding-inversion work
  reconstructs substantial source text from vectors alone, and cosine leaks
  topical membership even without inversion.

**The index defeats the encryption.** Per-domain keys therefore mean per-domain
*indexes*, which means separate caches, which is the tribe cost arriving from
the other direction. This closes the branch.

### 7c. Also not worth doing

Each is refused for a **viki-specific** reason, not because the technique is bad.
Recorded so the argument is not had again from scratch.

- **ANN.** *Refused because the scale is not reached and the cost is recall.*
  Every HNSW/IVF index trades exact recall for speed, and the usual failure is
  shipping one without measuring what was given up. VIKIVERSE_V1 SS 3 already
  says out. **The positive reason to keep brute force:** it is *exact*, and it is
  why a domain or `--source` filter costs viki nothing -- pre-filtering breaks
  graph connectivity and post-filtering returns fewer than k, which is a standing
  pain point in every vector DB and simply does not arise here. SS 6c's scoping
  design depends on this.
- **Engine swaps (sqlite-vec).** *Refused because it was built, measured and
  reverted* -- portability, specifically wasm, and MSYS. FINDINGS.md has what it
  did and did not buy. Do not re-argue it; re-measure if you must.
- **GraphRAG.** *Refused because the wins concentrate on multi-hop synthesis,
  which is the caller's job.* viki returns passages and the agent reasons -- a
  design choice, not a resource constraint (VIKIVERSE_V1 SS 3). A large build to
  do badly what the consumer already does well.
- **Contextual retrieval** (LLM-generated per-chunk context before embedding).
  *Refused because it needs an LLM at index time and viki has none, by design.*
  This is the largest reported chunking gain in the field, so the refusal costs
  something real and is worth stating plainly rather than eliding. Its
  viki-shaped cousin -- extending the composed-header idea from check-ins to file
  chunks -- is possible without an LLM, but the composition recipe is part of the
  sharing contract, so it is **cache-fragmenting** and must be called out as such
  (CLAUDE.md).
- **Fusing epochs.** See SS 2g.
- **`viki muse` unmarked excerpts.** Not refused, just out of scope here: same
  defect and same patch shape as the fragment markers CLAUDE.md already
  documents. Noted so SS 5d's expansion work does not silently inherit it.

---

## 8. What to verify or decide first

### 8.1 MEASURED 2026-08-26 -- yes, but it does NOT retire the trap

**Answer: FTS5 accepts a VIEW for `content=`.** Tested against the SQLite viki
actually compiles (amalgamation 3.53.4, `-DSQLITE_ENABLE_FTS5`), not the system
`sqlite3`. Create, `'rebuild'`, `MATCH` and **`snippet()` all work** over
`substr(blob, s, e-s)`, including two overlapping ranges on one blob.

**But the question this was asked to settle is not the one it answers.** The
view route inherits SS 4c's delete-order trap *exactly*, which the "if yes /
if no" framing above does not admit:

```
  delete FTS row first, then the range row   -> 'epsilon' matches 0   safe
  delete the range row first                 -> 'epsilon' matches 1   H11
```

With the source row gone the view resolves to nothing, FTS5 has no tokens to
drop, the delete succeeds and changes nothing, and **the withdrawn text stays
searchable**. That is `gc_orphan_chunks()`'s existing invariant (CLAUDE.md,
m1's H11) surviving the rewrite unchanged -- so a view keeps `snippet()` and
keeps the trap, and under SS 4b's *evictable* shadow the trap gets worse,
because eviction deletes the source at a time nobody chose. SS 4c reasoned to
that conclusion already; this measures it.

The original test, for the record:

```sql
CREATE TABLE b(id INTEGER PRIMARY KEY, data TEXT);
CREATE TABLE c(id INTEGER PRIMARY KEY, blob_id INT, s INT, e INT);
CREATE VIEW v AS
  SELECT c.id AS rowid, substr(b.data, c.s, c.e - c.s) AS txt
    FROM c JOIN b ON b.id = c.blob_id;
CREATE VIRTUAL TABLE f USING fts5(txt, content='v', content_rowid='rowid');
-- then exercise all four: 'rebuild', MATCH, snippet(), and a delete.
```

### 8.2 MEASURED 2026-08-26 -- present, and it costs `snippet()`

`contentless_delete=1` **works** on viki's own SQLite (3.53.4, above the 3.43
floor). Confirmed against the actual build, as this section asked:

```
  DELETE with NO source table at all   -> 'epsilon' matches 0   (order stops mattering)
  survivor intact                      -> 'alpha'   matches 1
  snippet(g,0,...)                     -> NULL
```

**So the trade is now explicit and measured, and it is the real content of
SS 8.1 + SS 8.2 taken together:**

| route | `snippet()` | delete-order trap |
|---|---|---|
| `content=` a VIEW | **works** | **inherited, and eviction makes it worse** |
| `contentless_delete=1` | **NULL -- excerpting moves into C** | **retired entirely** |

Neither is free. SS 4c chose contentless on the reasoning that eviction must be
safe at any time; that reasoning is now backed by a measurement rather than an
argument, and the price it pays has a name and a number: `snippet()` returns
NULL and excerpt selection becomes C viki does not have yet.

Still to check before building: `contentless_delete=1` must be set at CREATE
time, so this is a migration -- `migrate_chunk_fts()` already detects on stored
SQL, which is the right pattern.

### 8.3 OPEN

- **Is best-epoch-wins measurably right?** SS 2g decided tiers stay separate and
  the better epoch wins outright. The *reasoning* is sound (unequal-quality
  fusion dilutes); the *result* is unmeasured. If a case appears where a weaker
  epoch surfaces something the stronger one misses, revisit -- but measure before
  reopening, because the alternative reintroduces exactly the dilution SS 2g
  refuses.
- **Rung selection rule** (proposed): use the best epoch for which vectors exist
  in the cache **and** this device can embed the query. Both conditions checked
  independently -- see SS 9.
- **Compression granularity** (SS 4e).
- **`place` vs `domain`** (SS 6c): "where this happened" and "what region of my
  life this is" overlap but are not the same. Merging them by default would make
  notes and chunks mean different things by one word -- the failure mode
  SCOPES.md exists to stop. Decide deliberately.
- **Does `chunking_id` belong in the row** as provenance (SS 3c), and does the
  epoch key then shrink back to the model alone?

---

## 9. What this breaks

- **Citations.** `content_hash#chunk_ix` becomes `content_hash#start-end`. Wire
  format across the CLI hit line, `/api/ask`, the wasm edge (which reproduces
  native output *exactly*, verified), and three test files that parse by
  position. **It is also an upgrade**: `#chunk_ix` is meaningless without the
  params that produced it, while `#start-end` is self-describing and **stable
  across re-chunkings**. Do it deliberately in one commit, not in pieces.
- **`snippet()`**, if SS 8.1 fails or SS 4c is adopted: excerpt selection moves
  into C.
- **The delete-order invariant**, in the good direction -- it retires (SS 4c).
- **Every cache**, on re-chunking. Same class as the current epoch note in
  CLAUDE.md: the cache is derived (D-10) and `viki index` is the whole fix.

### 9a. The asymmetry that is easy to miss

**Vectors are only half.** A phone that pulls a laptop's better-model vectors
still cannot use them, because it cannot project the *query* into that space.
`cache pull` makes the document side feel solved and it is not. Options: ship the
larger model too (D-12's uv mechanism already does this); **remote query
embedding** -- send one string, get one vector, no corpus movement, but it
**leaks the query** to whoever computes it, inside or outside the tribe boundary,
and viki must report which happened; or fall back to the pinned epoch.

**Therefore: do not delete the pinned small epoch when a larger one arrives.**
Multiple epochs already coexist -- the vector leg filters by `model_id`. Keeping
both is exactly what makes a constrained device's graceful degradation possible.
Storage cost is real and bounded; the alternative is a device that drops to base
the day you upgrade.

---

## 10. Suggested order

Nothing here is scheduled. If it were:

1. ~~SS 8.1 and SS 8.2 -- two prototypes~~ **-- DONE 2026-08-26.** Both
   measured against viki's own SQLite; see those sections. The result is a
   trade, not a green light: the view keeps `snippet()` and keeps the
   delete-order trap, contentless retires the trap and returns NULL from
   `snippet()`.
2. ~~**Overlap alone**, under the existing schema~~ **-- DONE 2026-08-26**,
   exactly as prescribed: same corpus (`fp 1b1e3c962c1e8cad`), varying only the
   binary, old binary re-measured first. 40-line windows with a 10-line overlap;
   recall@1 0.256 -> 0.349, MRR 0.381 -> 0.424, held-out recall@1 0.333 -> 0.417,
   at +26% chunks. A sweep picked 10 (0, 5, 10, 20 measured; 20 costs +77%
   chunks for nothing). **Two honest qualifications in FINDINGS.md**: the
   predicted mechanism is only weakly supported -- RIGHT DOCUMENT, WRONG CHUNK
   moved 21->20, while what actually improved was the vector leg -- and it COSTS
   the coverage-closed class (recall@1 0.333 -> 0.200). The prediction that the
   `(model, chunking)` epoch key "just made it safe to try" held: differently
   chunked peers now coexist as two epochs instead of colliding.
3. Rung reporting (SS 2f) -- surfacing `legs`, no schema change.
4. Range-addressed chunks (SS 3-4), one commit including the citation change.
5. Rerank (category B, no epoch, no re-index) or a larger epoch (category A,
   tribe decision) -- independent of each other and of the above.

`test/retrieval-eval.sh` is the arbiter for anything claiming quality. "A much
better embedding" is a hypothesis until it is measured against the same held-out
set, with the old binary re-measured first.

**And the arbiter has a rule this file must state, because SS 2g, SS 5 and SS 8.3
all propose choosing between alternatives by measurement.** TUNE ON DEV. The
held-out 31% is spent the moment it is read while deciding -- that is model
selection on the test set, which is what commit `4292a0a` reverted a round of
ranking work for, and it recurred on 2026-08-26 when overlap and pool depth were
both picked off the DEV+TEST aggregate (FINDINGS.md; both survived re-derivation
on DEV alone). Every sweep this document proposes -- overlap vs the tree,
best-epoch-wins vs fusion, one pool depth vs another -- is a selection, so each
must be decided on DEV and reported on TEST once, afterward.
