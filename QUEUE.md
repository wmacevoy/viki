# QUEUE.md -- measured work not yet done

Findings and decisions accumulated while working on retrieval, capture and
memory, kept out of `AGENTS.md` because that file is the current-state
snapshot and this is a forward list. Every entry here is MEASURED unless it
says otherwise, and the measurements are stated so a later agent can decide
whether to trust, re-run, or discard them rather than re-deriving them.

Numbering is CHRONOLOGICAL, not priority. Corrections are appended as new
entries (§19c corrects §19, §23 retracts part of §14/§21) rather than by
rewriting the original, so a claim and its refutation stay together -- the
same discipline `FINDINGS.md` uses.

Read `§21` first: it records that stored data is disposable pre-alpha, which
un-gates roughly half of what follows.

Priority as of 2026-08-20, per Warren's framing (primary consumer is an agent
working online; his own access is secondary and may be offline):
  1. §24 capture/structure loop -- the PDA spec, and the best-specified need here
  2. §26 (merged with §8b) alternative surface forms -- largest measured gain
  3. §23 temporal queries ("last") -- what he actually asked for, not a ranking problem
  4. manifest gap (§19) -- pins the model but not chunking; cheap, and unblocks experiments
  5. §17 multi-resolution indexing -- what §19c's negative result argues for
  6. §1 external-corpus eval -- most of this queue rests on a degenerate corpus

## 0. EVAL DISCIPLINE -- binding on every entry below (Warren, 2026-08-20)

**Every retrieval measurement runs against BOTH corpora, always:**

1. `../life-by-the-numbers` -- 36 .tex files, ~41k words of real authored prose.
   Queries come from the author's own chapter phrasings, so the query set carries
   no contamination from whoever is running the experiment.
2. **Fabricated field notes** -- short operational records with the properties
   viki's actual use has and project docs do not: proper nouns, domain vocabulary
   whose spelling differs from how anyone later asks (`off fore` vs `right front`),
   dates, people, places, and open/closed state. Live at `test/field-notes/`.

Neither alone is enough, and they fail in OPPOSITE directions -- which is the
point. The book is long-form prose queried topically; the notes are one-line
records queried for specific facts. Section 19c is the proof: token-budgeted
chunking won decisively on project docs, then LOST on the book, because chunk
size trades against query specificity. A single corpus cannot show that.

**viki's own docs are NOT an acceptable corpus.** They are degenerate: median
chunk-to-chunk cosine 0.397 against the book's 0.296, because most of the text
is agents writing about one project. Entries measured that way carry a
DEPRECATED banner and are hypotheses, not findings.


## 1. External-corpus eval mode  (user approved 2026-08-16)
Extend the round's eval harness so a corpus path comes from env (life-by-the-numbers at
`../life-by-the-numbers`, 36 .tex files, ~41k words). Queries + expected-answer keys live
in viki's repo; the book itself is NEVER copied in. Skip LOUDLY when absent — same
discipline as CI's "-- NOT RUN" legs. Rationale: viki's own docs are a degenerate
calibration corpus (see §2).

## 2. Muse band edges must come from quantiles, and be NARROWER than 0.25-0.55
Measured chunk-to-chunk cosine:
| corpus | q25 | median | q75 | inside 0.25-0.55 |
|---|---|---|---|---|
| life-by-the-numbers | 0.167 | 0.296 | 0.428 | 50% |
| viki's own docs | 0.289 | 0.397 | 0.493 | 69% |
A fixed band swallows half of even a diverse corpus. Derive edges from the observed
distribution per corpus.

## 3. Muse must SAMPLE within the band, not take its top
Taking band-top returns scores just under the upper edge (measured 0.547/0.527/0.527
against a 0.55 ceiling) = "results 6,7,8", not surprises. Random sampling in-band is the
difference between serendipity and a bigger --k.

## 4. Markup-aware indexing — new, nobody had identified this
viki chunks raw bytes with no content-type awareness. On real LaTeX: 6% of chunks are
>50% markup tokens, 15% >25%; worst offenders are 100% markup (`\def\LBNlayout{a5}\input{main}`)
and are embedded and retrievable as if they were prose. Same class of problem will hit
HTML and source files. Belongs with the tokenizer work.

## 5. FINDINGS.md write-ups owed (agents held the file; do not duplicate their entries)
- Embedding space is a NARROW POSITIVE CONE: min cosine -0.046 over 5995 pairs, so
  `|cos|` ranking is a provable no-op. Kills the "absolute cosine" idea on evidence.
- Opposition is NOT anti-alignment: "suite has 54 assertions" vs "...90 assertions"
  scores 0.9578, the highest non-identity pair. Contradictions look like near-duplicates.
- Muse strategy top-5 overlap vs plain cosine: sum|xi*yi| 0.70, random 64-dim subspace
  0.43, anti-search 0.00, mid-band 0.02.
- Query->chunk and chunk->chunk are DIFFERENT distributions: best query match ~0.51, i.e.
  inside the chunk-chunk mid-band. A band calibrated on one is meaningless on the other.
  This is why query-anchored mid-band augmentation of `ask` does not work.
- Contradiction detection at >0.95 FAILS at 40-line chunk granularity: 0 of 5995 pairs,
  corpus max 0.854, though the same claim pair scores 0.958 as isolated sentences.
  Supersession detection needs claim-level granularity. Input for MEMORY_DESIGN.md.

## 6. Design notes (not fixes)
- Chunk-level content addressing: embeddings are keyed by DOCUMENT hash, so text shared
  across files is embedded and stored twice and can take two result slots. Verified:
  `wonder/wonder.tex` and `wonder/sunny.tex` differ overall but have byte-identical first
  40 lines -> chunk 0 of each embeds to cosine 1.0000. D-11 territory.
- MMR diversification in `viki_ask_query()` is the principled fix for top-5 redundancy
  (measured self-similarity 0.543 vs corpus median 0.397, 2.8 distinct docs of 5 results).
  Measure before building; the redundancy is real but mild.

## 7. Already recorded in AGENTS.md "Not yet built", do not re-derive
- release.yml does not gate on the m1 job.
- `viki index sub` vs `viki index ./sub` re-keys viki_source and inflates the retired count.

## 8. Fixing the negative-trap class -- the worst-scoring and most valuable class

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Baseline: negative-trap scores recall@5 0.333 dev, **0.000 held-out**. These are queries
phrased as a SYMPTOM ("I rewrote a file and re-ran the indexer but the old wording keeps
coming back") whose answer chunk is written in HINDSIGHT vocabulary (mtime, sweep_sources).
Two complementary fixes, both measured on 6 negative-trap queries vs the repo-docs corpus:

**8a. HyDE (query-side, free today, no code change).** The caller embeds a hypothesised
ANSWER instead of the question. Gold-chunk rank, raw -> HyDE: 21->5, 2->1, 6->2, 83->15,
18->1, 81->3. 6 of 6 improved; recall@5 1/6 -> 5/6. viki has no LLM but every caller is
one, so this is a documented CALLING CONVENTION for AGENTS.md, not a code change.
CAVEAT: I wrote the hypotheticals already knowing this codebase (I avoided repo-specific
vocabulary but knew the mechanisms). Upper bound, not expected value.

**8b. doc2query / HyQE (index-side, the durable fix) -- DOMINATES.** Generate at index
time the questions each chunk answers, index them as retrievable proxies for their chunk.
Same 6 queries, rank raw / HyDE / doc2query: 22/7/5, 4/3/1, 9/5/1, 91/19/1, 20/2/1,
86/5/1. **recall@1 0/6 -> 5/6; median rank 21 -> 1.**
Why it fits viki better than HyDE: one LLM call per chunk ONCE at index time, and the
generated questions are derived rebuildable projections (D-10 clean) that ride the uv
cache to every peer under D-11 compute-once-share-everywhere. The querying agent needs no
cleverness. Should also help the keyword leg, which is where the worst failures were
(gold at BM25 rank 49-105, past the 40-chunk pool) -- doc2query was invented for BM25.
Cost: ~2 x 15-word questions per ~400-word chunk = +7% text.
CAVEATS: contamination risk is HIGHER here than for HyDE (I wrote the questions knowing
the queries; short text means phrasing dominates). A BLIND generator test is required
before believing the magnitude. Also the HyDE column above is measured against a corpus
polluted with 12 extra generated docs, so it is not directly comparable to 8a's table.
Open question this lands on MEMORY_DESIGN.md: viki has no LLM, so what is the write path?

**Generalisation worth writing down:** when the embedding space lacks a structure you
cannot recover it with a scoring function -- you must add it to the data. |cos| was the
scoring-function attempt at negation-invariance and failed (no negation structure exists
in a narrow positive cone). doc2query is the data attempt at question/statement
invariance and works (that asymmetry is real: query->chunk tops out ~0.51 while
chunk->chunk runs 0.30-0.40 median).

## 9. Typo tolerance -- a REQUIREMENT, not a nicety (phone, in the field, blurry text)

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Measured on the repo-docs corpus, 3 typos per query (adjacent-key, transposition, drop):
- MATCHING is unaffected: implicit-AND returns 0 rows / OR-of-terms returns 158 rows, and
  158 is IDENTICAL for clean, one-typo and three-typo queries. A typo removes one disjunct
  from a dozen. (Implicit-AND returns 0 even on the CLEAN query -- it was never viable.)
- RANKING degrades, since a misspelled term contributes no BM25 mass: 5->13, 29->33,
  19->10, 47->37 (noisy, no collapse).
- The VECTOR leg is the fragile one: 83->67, 18->**89**, 2->2, 81->66. WordPiece splits a
  misspelling into different subwords, so the embedding moves.
- Small n (4 queries) and poor baseline ranks: directional only.
**ACTION: document OR-of-terms as LOAD-BEARING for typo tolerance.** It is currently
recorded only as the fix for FTS5 implicit-AND. Someone optimising for precision later
(phrase matching, AND) would silently destroy field usability. This is a property, not a
bug fix.
Note D-7 already settles on-device dictation for mobile, which sidesteps typing but trades
keyboard typos for homophones and mangled proper nouns -- the same fuzzy machinery pays
off either way.

## 10. sqlean + fts5vocab for typo correction  (user asked 2026-08-16; DO THIS FIRST)
NOT ripgrep: it is Rust, so linking it adds a Rust toolchain to build.sh on four platforms
including MSYS and breaks the self-contained property won by decoupling from fossil-see.
Its strength (fast recursive scan) is orthogonal -- viki queries an index, it does not scan.
sqlean (git@github.com:nalgeon/sqlean.git) is the right shape: single-file C SQLite
extensions, statically linked exactly like sqlite-ndvss already is (-DSQLITE_CORE +
sqlite3_auto_extension in viki_db.c). Ships `fuzzy` (Levenshtein/Damerau/soundex/metaphone),
`regexp`, `unicode`, `text` -- CONFIRM the module list against the repo, this is from memory.
VENDOR the specific modules needed; do not fork (same discipline KICKOFF sets for ndvss).
**The design (this is the actionable part): use `fts5vocab`**, an FTS5 virtual table
already compiled in, to expose the index's own term vocabulary. For each query term
matching ZERO rows, substitute the nearest vocabulary term by edit distance. Corrects
against terms that actually exist in THIS corpus, costs one lookup per failed term, leaves
the hot path untouched. ~100 lines, no new process.

## 11. TRIED AND REVERTED 2026-08-21 -- keep sqlite-ndvss. sqlite-vec, as a LOCAL DERIVED INDEX only
Do NOT move the shared cache to vec0. Keep `viki_chunk` exactly as it is -- that is the
artifact D-12 ships between peers and D-11 says is computed once -- and build the vec0
table as a local index rebuilt from it. D-10 compliant by construction, NO epoch bump, no
peer coordination, revertible.
Wins: better-maintained engine; retires the aarch64 SVE2 bug that has linux-arm64 CI
marked experimental; unlocks quantization later to shrink what travels over sync.
VIKI_DESIGN.md already names sqlite-vec as the intended swap, so this is executing a
settled decision, not a new one.

OUTCOME: built to exactly that shape, CI-green on all eight jobs, then REVERTED on
Warren's call. The decision reason is PORTABILITY, and specifically WASM -- ndvss ships
similarity_functions_wasmsimd.h and builds under plain MSYS; sqlite-vec needed a Windows
patch and would need wasm redone. Quality was measured IDENTICAL and neither engine has
an ANN index, so nothing was given up. The arm64 bug that motivated the swap turned out
to be a five-line upstream fix (wmacevoy/sqlite-ndvss#1) in a repo we already own.
Full accounting in FINDINGS.md, including the reasoning error: "do not fork ndvss" was
read as "cannot fix ndvss", when vendor/sqlite-ndvss IS our fork.
REVISIT ONLY IF quantization-to-shrink-sync becomes a priority -- the one surviving
benefit, still unbuilt -- and make wasm coverage an explicit requirement of that
evaluation.

## 12. Meilisearch: take the IDEA, never the dependency  (user asked 2026-08-16)
Stretch is large and cuts against viki's constraints: Rust server (or `milli`, a heavy
embed); its index is its own format, not a rebuildable projection shippable as a uv blob;
adds a second data store to a system whose trust story is "one encrypted Fossil repo,
everything else derived".
WORTH STEALING: the typo model -- an edit-distance budget scaled to word length (roughly
1 edit at >=5 chars, 2 at >=9), with exact matches always ranked above corrected ones so
precision does not degrade for people who typed it correctly. Implement inside §10.

## 14. THE VECTOR LEG HURTS RANK-1 PRECISION -- root cause is chunk truncation

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Measured 2026-08-17 by me, three configurations, SAME 138-chunk corpus, 43 queries:
| config | hybrid r@1 | its own BM25-only control |
|---|---|---|
| baseline ask (reverted, = bd04683) | 0.209 | 0.256 |
| shipped ranking work (identifier gate) | 0.233 | ~0.349 |
| IDF weighting + vector leg always on | 0.140 | 0.302 |
**Hybrid loses to BM25-only at rank 1 in EVERY configuration, and the more the vector leg
participates the worse r@1 gets.** `query_is_identifier()` was not a clever heuristic; it
was crudely SUPPRESSING a leg that hurts (it disabled the whole vector leg on any `_`).
ROOT CAUSE, already named in the reverted code's own comment: embeddings are computed over
the first 254 WordPiece tokens of a 40-line chunk, so the vector leg sees ~41% of each
chunk and a definition is usually not in that 41%. RRF then lets two mediocre ranks outvote
one excellent rank. **The fix is token-aware chunking (<=512 tokens, with overlap), not
fusion tweaks or gates.** That is a chunk_params change = EPOCH BUMP; viki-manifest exists
for exactly this. Do this before any further ranking work -- everything else is downstream.
Also correct the record: the "0.256 -> 0.233 regression" the verifiers reported was mostly
the corpus growing 114 -> 138 chunks, NOT the ranking code. On a fixed corpus the ranking
package helped this harness and hurt the independent query set -- i.e. within noise. The
revert was justified on METHOD (burned split, eval-fitted gate), not on the metric.
AGENTS.md's long-standing evidence for rung 2 is the zero-keyword-overlap horses query --
the one case where fusion cannot hurt by construction. Needs a FINDINGS entry.

## 15. DOC DEBT left uncommitted-clean on 2026-08-17 (small, known, not yet done)
- CLAUDE.md ~line 128 transcribes `0.256 vs 0.302` in prose. Directionally CONFIRMED by §14,
  but corpus-stale (114-chunk era). Replace transcribed numbers with a pointer to
  test/retrieval-eval.sh -- this is the third round in a row that prose numbers rotted.
- RETRIEVAL_PLAN.md says "All gates green"; bar B4 (hybrid r@1 no-regression) FAILED and B7
  (latency) was never measured. Now moot for B4 since the ranking work is reverted, but the
  sentence is still wrong and should say so.
- MEMORY_DESIGN.md: the second memory container (a fenced ```viki-memory block inside an
  ordinary indexed file) breaks three of the document's own guarantees and reintroduces the
  forum-fprev bug shape it opens by warning about. Unaddressed.
- AGENTS.md tech-notes contradiction: FIXED 2026-08-17.

## 16. Old-binary wipe hazard: class fixed, specific case unfixable in code
VERIFIED 2026-08-17: the CURRENT binary does NOT sweep unrecognised namespaces --
`looks_like_namespace()` in sweep_sources() returns drop=0, and I confirmed empirically that
an injected `futurens:` row survives (`0 stale source(s) retired`). So forward skew is safe.
The remaining hazard is a bd04683-era binary run against a nine-class cache: it deletes all
five new namespaces and `viki cache push` publishes that fleet-wide. That CANNOT be fixed by
changing new code -- the old binary is already built. Mitigation is procedural: upgrade every
peer before sharing a nine-class cache, and say so loudly in AGENTS.md. A `cache push` that
prints its namespace inventory before publishing would make the loss visible; not implemented.

## 13. Benchmark discipline now available
59 scored queries with a held-out split exist. None of §8-12 should be landed on taste --
each is measurable. Pointing txtai or Meilisearch at the same query set would be a genuinely
informative comparison and is now cheap.

## 17. Multi-resolution ("Haar") indexing + DC removal  (measured 2026-08-20)
Warren's framing: chunk pyramid = scaling functions, residual-vs-parent = detail coeffs.
**A. Coarse levels CANNOT be derived by averaging.** cos(parent, mean-of-4-children) = 0.828
(n=24, min 0.72, max 0.91) vs 0.302 unrelated baseline. Each level must be embedded. Cost is
bounded at ~2x the finest level (dyadic sum 1+1/2+1/4+... = 2).
**THE INVERSION, and the real argument for the pyramid:** the 40-line parent is truncated at
254 WordPiece tokens so it sees ~41% of its own text; its four children collectively see 100%.
The mean-of-children may therefore be MORE faithful than the parent embedding -- i.e. the
disagreement is the PARENT being wrong. Testable against test/retrieval-eval.sh. This makes
the pyramid a correctness fix for §14, not a granularity nicety.
**B. Removing the corpus centroid (DC term) sharpens separation a lot.**
| | q25 | med | q75 | min | spread |
| raw | 0.130 | 0.239 | 0.341 | -0.034 | 1.034 |
| centroid removed | -0.157 | -0.058 | 0.071 | -0.385 | 1.385 |
Prior art: Mu & Viswanath "all-but-the-top". Unrelated pairs move to ~0 where they belong.
**This REVIVES |cos| (§8/queue history).** |cos| was killed because the cone had no negative
structure; after DC removal min cosine is -0.385, so opposition becomes representable. The
earlier null result was "opposition is buried under the background", not "not representable".
CAUTIONS: (1) better pairwise separation != better ranking -- each vector renormalises by a
different factor so ordering changes; must be measured on the harness. (2) THE CENTROID IS
CORPUS-LOCAL, which collides with D-11 shared embeddings: baking it into stored vectors means
two peers with different corpora no longer share. Must be a QUERY-TIME local transform, or the
centroid joins the epoch pin in viki-manifest.
Analogy's limit: Haar is orthogonal and invertible; this is neither.

## 18. SLIDING WINDOW (stride < width) -- measured 2026-08-20, the biggest single win yet

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Warren's proposal: slide 20 with width 40. Framing: non-overlapping chunks are SHIFT-VARIANT
(where the boundary falls changes the representation arbitrarily); stride<width is the
undecimated / a-trous transform, which trades redundancy for shift-invariance.
Vector-only, 38 eval queries with resolvable anchors, repo *.md corpus, same model:
| chunking | windows | r@1 | r@5 | r@10 | med rank |
| w40 s40 (CURRENT) | 163 | 0.184 | 0.289 | 0.474 | 11.0 |
| w40 s20 (Warren)  | 321 | 0.289 | 0.553 | 0.658 | 4.5 |
| w20 s20           | 321 | 0.263 | 0.553 | 0.658 | 5.0 |
| w20 s10           | 636 | 0.263 | 0.632 | 0.763 | 3.5 |
**CORRECTS §14's diagnosis.** I attributed the vector leg's weakness mainly to TRUNCATION
(254-token window = ~41% of a 40-line chunk). Compare w40_s20 vs w20_s20: SAME window count,
one truncated-but-overlapping, one narrow-but-whole. The truncated overlapping one WINS on
r@1 and ties elsewhere. So BOUNDARY PLACEMENT costs more than truncation. Shift-variance was
the bigger defect.
Knee is at stride 1/2: w20_s10 doubles the index again for deeper recall but does NOT beat
w40_s20 at rank 1. Recommend w40/s20 = 2x index for the rank-1 win.
**THE REAL PRIZE, still untested:** §14 found hybrid LOSING to BM25-only at rank 1 because
the vector leg is weak. A vector leg going 0.184 -> 0.289 may stop hurting and start helping,
which would flip the rung-2 story. Test fusion, not just the vector leg.
CAVEATS: vector-only measurement; one corpus, and it is viki's own self-similar docs (median
cosine 0.397) -- re-run against life-by-the-numbers per §1. Simulated by writing each window
as its own file (so content_hash is per-window, not per-document); faithful for retrieval
quality but not identical to a real chunker change. chunk_params change = EPOCH BUMP (D-11):
every peer re-embeds and must agree, which is what viki-manifest exists to coordinate.
Implementation note: VIKI_CHUNK_LINES is a compile-time #define in src/viki_index.c; a real
change needs a stride parameter, and the manifest must record BOTH width and stride.

## 19. TOKEN-BUDGETED CHUNKING -- supersedes §18's recommendation. DO THIS.

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Warren: "match viki chunking to 254 -- this is an impedance mismatch." Correct, and severe.
THE MISMATCH, measured with the real vocab.txt (WordPiece reimplemented in Python):
  width 40 lines: median 636 tok, 96.9% of windows exceed 254 -> **60.9% of ALL corpus
                  tokens are discarded before the model sees them**
  width 20 lines: median 316 tok, 82.6% exceed -> 24.0% discarded
  width 10 lines: median 158 tok,  2.2% exceed ->  3.5% discarded
Budget is exactly 254: src/embed.c `#define VIKI_MAX_SEQ_LEN 256`, and viki_tokenize counts
[CLS]/[SEP] inside maxLen. NOTE the project has quoted 512 elsewhere -- that is the model's
positional max, NOT what this build uses. Fix that wherever it appears.
RETRIEVAL, vector-only, 38 anchored eval queries, repo *.md:
| chunking | windows | r@1 | r@5 | r@10 | med |
| w40_s40 (CURRENT) | 163 | 0.184 | 0.289 | 0.474 | 11.0 |
| w40_s20           | 321 | 0.289 | 0.553 | 0.658 | 4.5 |
| w20_s10           | 636 | 0.263 | 0.632 | 0.763 | 3.5 |
| **tok254_s127**   | 768 | **0.342** | 0.605 | 0.658 | **3.0** |
| w10_s5            |1268 | 0.342 | 0.500 | 0.632 | 5.5 |
**RECOMMENDATION: token-budgeted windows, <=254 tokens, ~50% token stride.** r@1 0.184 ->
0.342 (+86% relative), median rank 11 -> 3.
Effects now SEPARATE cleanly (§18 could not do this -- both its arms were truncated):
  overlap alone +0.105 r@1; then fitting the budget a further +0.053. Both real.
NEGATIVE RESULT: w10_s5 goes NARROWER than the budget and does not help -- ties r@1 but worse
at @5/@10/median for 1.65x the index. Fragmenting below the model's window scatters answers.
w20_s10 still leads deep recall (@10 0.763): smaller windows help the TAIL while hurting
precision. If tail recall matters more than rank-1, that is the other knee.
COST: 768 vs 163 chunks = 4.7x, i.e. ~1.2 MB of vectors vs ~250 KB. Trivial at personal scale
but it does travel over sync (D-12).
**MANIFEST CONSEQUENCE (do not skip):** viki-manifest pins the MODEL but NOT the chunking, so
two peers can disagree about boundaries while believing they share an epoch -- same class as
the §16 mixed-fleet hazard. Token-budgeted chunking must record width + stride + token budget
in the manifest, and any change to them is an EPOCH BUMP.
STILL UNTESTED: fusion (this is vector-only), and a non-degenerate corpus (§1, the book).

## 19b. Refinement: tokens are the UNIT, lines are still the CUT POINT (2026-08-20)

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
Warren asked why lines at all -- why not slide continuously through a token stream. Tested.
(whitespace-insensitive anchor matching, so tok254 reads slightly higher than in §19)
| chunking | windows | r@1 | r@5 | r@10 | med |
| w40_s40 (CURRENT)              | 163 | 0.184 | 0.289 | 0.474 | 11.0 |
| tok254_s127 (token-budgeted, LINE-ALIGNED) | 768 | 0.368 | 0.632 | 0.684 | 2.5 |
| stream254_s127 (pure token stream)         | 807 | 0.316 | 0.474 | 0.579 | 7.5 |
The continuous stream has BETTER token utilisation (99% vs 95% of the 254 budget) and loses
on every metric. Utilisation is not what matters; boundary quality is.
CONCLUSION: measure the window in TOKENS (what the model consumes), but cut on LINE
boundaries (where meaning changes). Slide continuously through the BUDGET, snapping to
structure -- do not slide continuously through the prose.
CONFOUND, stated because it weakens the mechanism claim (not the recommendation): the stream
variant joined words with spaces and so destroyed markdown layout entirely -- headings, list
items and code fences collapsed to running text. It therefore conflates "not line-aligned"
with "no formatting". A cleaner test keeps newlines and cuts at arbitrary token offsets.
Answer to "why lines?": no reason. AGENTS.md has flagged "fixed 40-line splits, no overlap,
no token awareness" as naive since M1. It was a placeholder, never a design.

## 20. Fragment marking ("...") at chunk boundaries  (Warren, 2026-08-20)

> **DEPRECATED MEASUREMENT (2026-08-20).** The numbers in this entry were taken
> against viki's OWN docs -- a degenerate corpus (median chunk-chunk cosine 0.397;
> fourteen agents writing about one project). Section 19c shows a result measured
> that way can REVERSE on real prose. Treat these as hypotheses, not findings, until
> re-run against BOTH `../life-by-the-numbers` and the fabricated field notes.
viki does NO boundary marking today: a window starting mid-sentence is stored and embedded
as if it were a complete text ("and twenty years ago" reads as an assertion, not an excerpt).
Tested on the tok254_s127 winner: prepend "... " unless the window starts the document,
append " ..." unless it ends it. "..." is ONE token in vocab.txt (so is the single-char "…").
| variant | r@1 | r@5 | r@10 | med |
| tok254_s127 unmarked | 0.368 | 0.632 | 0.684 | 2.5 |
| tok254_s127 "..."-marked | 0.395 | 0.684 | 0.737 | 3.0 |
cos(same window, marked vs unmarked) mean 0.9856, min 0.9266 -- 2 tokens of 254 move the
vector ~1.4%, enough to reorder several results.
**DO NOT OVERREAD THIS.** n=38, so +0.027 r@1 is ONE query. Consistent across all three
recall depths, but not significant. Carry it into a larger test; do not bank it.
**THE STRONGER ARGUMENT IS PROVENANCE, NOT RANKING.** `viki ask` now prints content_hash so
an agent can cite precisely. An unmarked mid-document window invites that agent to quote a
dangling fragment as a complete statement -- a provenance defect for a system whose promise
is "vague memory, precise receipts", and it holds whatever the retrieval numbers do.
The two are SEPARATE SWITCHES: mark the STORED text (retrieval, unproven) vs mark only the
DISPLAYED snippet (honesty, certain). Recommend the display-side marking regardless.

## 19c. CORRECTION to §19 -- the chunking win does NOT generalize (2026-08-20)
Ran §19's comparison on life-by-the-numbers (the non-degenerate corpus, §1). Queries are the
AUTHOR's own chapter phrasings from filenames ("memory is not recording", "attention"), so no
contamination from me. RESULT REVERSES:
| chunking | windows | r@1 | r@5 | r@10 | med |
| w40_s40 (CURRENT) | 107 | 0.118 | 0.471 | 0.647 | 7.0 |
| tok254_s127       | 333 | 0.059 | 0.353 | 0.412 | 22.0 |
| tok254_ell        | 333 | 0.118 | 0.353 | 0.412 | 24.0 |
Re-scored as DOCUMENT ATTRIBUTION (did the top window come from the right chapter), the gap
nearly vanishes: top1-doc 0.765 / 0.647 / 0.706, top5-doc 0.941 for ALL THREE, median rank 1.0
for all three.
**MECHANISM (coherent, not a contradiction): CHUNK SIZE INTERACTS WITH QUERY SPECIFICITY.**
viki-docs test = specific factual questions, precise gold passage -> SMALL windows win big.
book test = broad topic words, narrative gold -> LARGE windows carry more topical mass and
win or tie. Precise queries want small chunks; diffuse queries want large ones.
**THEREFORE: §19 is DOWNGRADED from "DO THIS" to "corpus- and query-type dependent".** Do not
ship a global chunk-size change on the strength of the viki-docs measurement alone.
**AND THIS STRENGTHENS §17 (multi-resolution pyramid):** the defect is not that 40 lines is
the wrong size, it is that viki has ONE size for every query type. A pyramid lets retrieval
pick the scale. Arrived at from the opposite direction than expected.
CAVEATS: n=17, so every difference above is 1-2 queries. My anchor selection was mechanical
(a narrative sentence from mid-chapter), which is NOT what a reader would search for -- the
first table may be measuring my protocol rather than the chunking. A fair book test needs
reader-plausible questions with anchors on the passages that answer them.

## 21. CONSTRAINT RELAXED: stored data is disposable (Warren, 2026-08-20)
"You can rebuild stored data - this is pre-alpha - there is nothing to harm here."
Several entries above are gated on epoch-bump / peer-coordination cost. THAT GATE IS OFF.
There is no fleet, no shared cache in the wild, and re-indexing is cheap. Re-read the queue
with that in mind -- I was applying production D-11/D-12 discipline to a pre-alpha tree.
WHAT THIS RE-PRIORITISES:
- §19/19c chunking: the epoch-bump objection is void. The REAL blocker stands (it did not
  generalize to the book), but EXPERIMENTING is now free -- just re-chunk and re-measure.
- §20 stored-text fragment marking: was deferred partly on re-index cost. Now cheap to test
  properly at a sample size where +0.027 r@1 means more than one query.
- §17 multi-resolution pyramid: needs a schema carrying several scales. Now just a schema
  change, not a migration. This is the most promising unblocked item -- and §19c's finding
  (chunk size interacts with query specificity) is a direct argument FOR it.
- §11 sqlite-vec: I recommended "local derived index only, keep viki_chunk untouched" purely
  to protect the shared artifact. That constraint is gone -- swap the schema directly, which
  is simpler than the two-table design I proposed.
- §6 chunk-level content addressing: was "D-11 territory". Now just a schema change.
- §16 mixed-fleet wipe hazard: severity drops sharply. No fleet exists. Keep the
  looks_like_namespace() guard (it is correct), but the procedural "upgrade every peer
  before sharing" warning is premature.
WHAT THIS DOES NOT CHANGE:
- The manifest gap is still real: viki-manifest pins the model but not width/stride/token
  budget. Cheap to fix now, and fixing it BEFORE any chunking experiment is what makes those
  experiments reproducible.
- §19c's negative result stands on its own evidence, not on migration cost.
**BIGGEST UNBLOCKED QUESTION: does rung 2 earn its keep?** Hybrid loses to BM25-only at
rank 1 in every configuration measured, but always on a 61%-truncated index. With free
re-indexing, chunk correctly and re-run fusion. That answer decides whether ONNX + a 23MB
pinned model + epoch machinery + sqlite-ndvss (and its aarch64 CI bug) stay in the project
at all. It is now a cheap experiment rather than an expensive one.

## 22. Opened by the 88f9cb6 close-out (2026-08-20) -- small, known, not fixed
- **build/m1-e2e-probe.sh C11 is NON-DETERMINISTIC.** It now turns on an exact RRF tie whose
  resolution depends on qsort instability, so the probe can report FAIL=1 on an unchanged
  tree. Do not use it as a pass/fail gate until the tie is broken (add a deterministic
  tiebreak in the sort, e.g. content_hash then chunk_ix).
- **`viki muse` has the same unmarked-fragment defect `ask`/`grep` just fixed.**
  src/viki_muse.c selects substr(chunk_text,1,?) and prints it with no <<...>> markers.
  Deliberately left out of 88f9cb6 to keep that change small. The macros and the rule are
  already in src/viki_ask.h; this is a ~10-line change.
- §20's STORED-text half is still open; only the DISPLAY half shipped in 88f9cb6.

## 23. RUNG 2 STAYS -- and "last" is a different problem (Warren, 2026-08-20)
Warren: "some version of language similarity matching needs to survive for me (not you, you
could do multiple queries)". THE ASYMMETRY IS THE POINT: an agent compensates for weak
semantic search with multiple queries, HyDE and regex. A human on a phone gets ONE sentence.
Every rung-2 measurement so far used AGENT-shaped queries against PROJECT DOCS -- the wrong
user and the wrong corpus. Tested on a synthetic field-note corpus instead (horses, markings,
site baiting; note vocabulary deliberately != question vocabulary):
**Q1 "what horses have right front sock on monument rocks?" -- RUNG 2 EARNS ITS KEEP.**
  hybrid rank 1 = "Copper carries a sock on the off front only" -- the vector leg bridged
  "right front" -> "off front" (horse convention: off=right, near=left), zero shared words.
  BM25-ONLY rank 1 = "No horses moved. Nothing to report" (junk); correct answer at rank 2.
  => RETRACT the "does rung 2 earn its keep / consider dropping ONNX" framing in §21/§14.
  For Warren's query shape it demonstrably does. Keep it. Re-ask the question only for the
  AGENT path, where the answer may still be no.
**Q2 "who baited rimi site last?" -- NOT A RETRIEVAL PROBLEM.** "last" is a temporal
  superlative: filter entity+action, order by time, take max. First run APPEARED correct
  (newest note ranked 1) -- COINCIDENCE. Proved it: added an OLDER note (Jan 6) with richer
  bait vocabulary and it took rank 1 over the actually-most-recent (Jun 2). viki has no
  recency notion whatsoever.
  DATA EXISTS, QUERY SURFACE DOES NOT: viki_source stores mtime, and ckin:/note:/forum:
  artifacts carry real Fossil timestamps. viki_chunk has no time column and `ask` has no
  ordering. This is MEMORY_DESIGN.md territory (time/actor/episode) and it is what Warren
  actually asked for, so it should outrank most of the ranking work in this queue.
**DOMAIN VOCABULARY GAP, measured:** n01 "Rimi ... off fore white sock" never reached top-3
  while n03 "off front" did -- "off front" bridges to "right front", "off fore" does not.
  This is the strongest argument yet for §8b doc2query: the note-taker writes "off fore",
  the generated question says "right front", the human's phrasing matches the QUESTION.
  doc2query is now a HUMAN-facing fix, not just an agent one.
**EVAL GAP:** none of the 59 eval queries look like Warren's. A field-note query set (entity
+ attribute + place + time) is a different distribution from project-doc lookups and nothing
in this queue has been validated against it.

## 24. THE PDA SPEC -- 16 real notes from Warren, 2026-08-20. Best spec in this queue.
Warren dumped 16 real operational notes and said: "offline a lot of these turn into queue
follow-ups that would build into information a personal digital assistant with access to a
shared viki database would update and act on." Corpus preserved at scratchpad/pda/docs/.
MEASURED against them with the current binary:
- "what needs to be done in monument rocks?" returned a PLAUSIBLE-LOOKING WRONG LIST. It
  matched the WORDS "needs / remember to / warn to", not the CONCEPT of an open task.
  Returned "new foal in rimi's band" (an observation) and "renee will be baiting thursdays
  and sundays" (a schedule). Nothing filtered by place. And the one note that genuinely IS
  about Monument Rocks -- "rain washout makes MR impasible" -- matched for the wrong reason,
  because nothing resolves the abbreviation mr -> monument rocks.
  "tire for utv fixed" was correctly absent, but by luck of wording; nothing knows it closed.
- "what hotel are we staying at tonight?" returned 3 confident irrelevant notes. The answer
  is not in the corpus. **viki has NO notion of "no good answer"** -- RRF returns top-k
  regardless of absolute score.
**CHEAPEST HIGH-VALUE FIX IN THE WHOLE QUEUE: a confidence floor on `ask`.** For an assistant,
confabulating a basis for action is worse than ranking badly. Threshold on absolute score so
"I don't know" is a possible output. Small, no schema change, no epoch bump.
SEVEN THINGS THE NOTES DEMAND THAT VIKI LACKS:
 1 TYPE: task | observation | rule | schedule | alert. "new foal" is not a to-do.
 2 STATE: open/closed. "tire for utv fixed" should CLOSE a prior item (supersession again --
   same spine as the fprev bug and the 54-vs-90 doc contradiction).
 3 PLACE as a filter, WITH abbreviation resolution (mr = monument rocks, low gap, bait camp).
 4 TIME in three flavours: deadline ("by next friday"), recurrence ("thursdays and sundays,
   except labor day" -- note the exception), immediacy ("tonight").
 5 PERSON-CONSTRAINTS AS RULES, not facts: "renee has an injury so cannot lift heavy items"
   and "sue only has a mobile phone when hiking, always follow up with gmail" must MODIFY
   future action, not merely be retrievable.
 6 AGGREGATION: "what needs to be done in X" wants a LIST of open items, not top-5 chunks.
 7 CONFIDENCE FLOOR (above).
ARCHITECTURAL RECOMMENDATION -- do NOT turn viki into a task database. That competes with a
hundred tools and discards what is differentiated (offline-first, encrypted, shared,
semantically recallable). Warren's own split is right and matches ARCHITECTURE.md:
  OFFLINE: the C binary appends raw notes. No model, no network, no LLM. Works on a phone.
  ONLINE: an agent parses each note into type/entity/place/time/state and writes it back as
  structured artifacts, then acts.
Same shape as doc2query (§8b) and HyDE (§8a): viki stays dumb, the agent adds structure.
This is ARCHITECTURE.md's request-artifact pattern and MEMORY_DESIGN.md's open write-path
question -- these 16 notes are the concrete spec both were missing. USE THEM.

## 25. MUSE AS HONESTY AUDIT (Warren, 2026-08-20)
"part of muse could be tracking honesty of results on previous work."
WHY THIS IS THE RIGHT VERB: you cannot QUERY for a claim that has gone stale -- you do not
know which one. But you can be SHOWN one. Muse is the only verb that needs no question.
THIS PROJECT IS THE PROOF CASE. Same defect shipped FOUR times, always "true when written,
silently false later", always caught by a human or an adversarial reviewer READING the file,
never by the system:
  1. forum posts: superseded revisions served as current (fprev)
  2. AGENTS.md certifying "54 passed" and "90 passed" twelve lines apart
  3. the msys-2.0.dll "not bundled" bullet, months after it was bundled
  4. the "tech notes aren't indexed" bullet, a full round after they were
IMPLEMENTABLE FROM PIECES THAT NOW EXIST:
- `viki grep` finds CLAIM-SHAPED text (digits + assertion verbs: "PASS=", "verified",
  "N passed", "confirmed", dates). Regex is the right tool for shape, not meaning.
- viki_source.mtime gives AGE -- bias seeds toward OLD claims (muse already has --bias old).
- muse's mid-band surfaces the seed's NEIGHBOURHOOD, so contradicting evidence arrives with
  the claim rather than having to be searched for separately.
- `ask` now reports best cosine (35054cd), so a claim whose supporting evidence has WEAKENED
  is itself a signal.
- The agent then re-verifies and either confirms or SUPERSEDES -- which is MEMORY_DESIGN.md's
  spine, and closes the loop rather than just reporting.
MEASURED CAVEAT, do not skip: contradiction detection FAILS at 40-line chunk granularity --
0 of 5995 pairs exceeded cos 0.95, corpus max 0.854 -- while the same claim pair ("54
assertions" vs "90 assertions") scores 0.9578 as ISOLATED SENTENCES. An honesty mode needs
CLAIM-LEVEL granularity, so it is downstream of the chunking work (§19/§19c/§17), not a
standalone build.
SHAPE: `viki muse --audit` = seed from claim-shaped + old, return neighbourhood, let the
agent adjudicate. Do NOT try to adjudicate in C.

## 26. SIMPLIFIED-RESTATEMENT VECTORS -- merge with §8b into "alternative surface forms"
Warren: "384 dimensions is roughly toki pona - elastomer could easily just be 0's - would it
help to have a simplified language translation vector as well? this could only be generated
online, but, oh well."
TOKENISER EVIDENCE (real vocab.txt, 30522 tokens): rare/domain words SHRED --
  elastomer -> el ##ast ##ome ##r (4)   subzero -> sub ##zer ##o (3)
  fetlock   -> fe ##tl ##ock (3)        propane -> prop ##ane (2)
while rubber / cold / material / horse / sock are clean single tokens.
**BUT THE "ZEROS" HYPOTHESIS IS WRONG, MEASURED.** A technical query matches a technical doc
at 0.6452 -- because both shred into the SAME fragments and consistent noise correlates with
itself. Rare words are not unrepresented; they are ONLY representable AGAINST THEMSELVES.
The penalty is not blindness, it is that they match nothing but their own spelling.
2x2, same facts stated two ways:
| query | vs ORIGINAL | vs SIMPLIFIED |
| technical "synthetic elastomer ... subzero"      | 0.6452 | 0.3042 |
| plain "did the rubber seal stay soft in cold"    | 0.4961 | 0.8191 |
| Warren's "which horse has a right front sock"    | 0.5093 | 0.3115 |
(noise control 0.0752)
=> SIMPLIFICATION MUST NOT REPLACE THE ORIGINAL (-0.34 and -0.20 on domain-worded queries).
=> AS A SECOND VECTOR pointing at the same chunk, taking max(), it never loses and sometimes
   wins hugely: the plain-word query goes 0.4961 -> 0.8191.
**MERGE WITH §8b.** doc2query (generated questions) and simplified restatement are the SAME
MOVE: alternative surface forms of one chunk, each embedded, all citing the same content_hash.
Three doors into one room: domain text + plain restatement + generated questions. Build one
mechanism, not three.
ARCHITECTURE unchanged and already established (HyDE §8a, doc2query §8b): viki has no LLM,
an ONLINE agent generates the forms, and under D-11 they are computed once by whoever sees
the content first and shared via the uv cache. Warren anticipated the constraint himself.
SCHEMA NOTE (cheap now, per §21): needs a form/variant dimension on viki_chunk, or a sibling
table keyed by (content_hash, chunk_ix, form). Retrieval takes max() over forms and reports
which form matched -- that last part matters for provenance: cite the ORIGINAL text, never
the machine-generated restatement.


## 27. `viki muse`'s band floor is conditional on the DRAW, not the corpus (2026-08-20)
`build/muse-probe.sh` B7 exists because "the floor is a property of the CORPUS, not of the
draw, so it must not move between seeds. A floor that wobbled per run would mean the
estimator was sampling non-deterministically and no two muses would be comparable."
IT IS NOW FAILING, and the failure is real:
  seed=1 -> "band: skip the seed's nearest 15, window 22, floor cos>=0.3094 (corpus median
             pairwise cosine)"   -> 14 chunks in band, 8 documents
  seed=2 -> "band: skip the seed's nearest 15, window 22, NO floor in force"
                                 -> 22 chunks in band, 12 documents
Same corpus (425 chunks), same binary, different seed -> different band SEMANTICS.
**PRE-EXISTING, NOT a regression from the timestamp work.** Verified by compiling all of
git HEAD's src/ into a scratch binary and running the probe against it: identical failure.
It passed 40/0 in earlier runs, so the two seeds previously happened to agree -- i.e. B7 is
a test that passes flakily and has now correctly caught something.
**FIXED 2026-08-21, and it was a REPORTING bug, not a logic one.** The floor drop is a
documented degradation rung that fires when a seed's band is too thin -- working as designed.
But `floorCos` is OVERWRITTEN by the sentinel when that happens, and stderr printed only
`floorCos`, so a run that dropped the floor said nothing about the corpus at all. Two facts
were sharing one field: what the corpus floor IS (`floorSampled`, invariant across seeds)
and whether it was IN FORCE this draw (`floorCos`). muse now reports both --
`floor cos>=0.3093 (corpus median pairwise cosine) -- NOT IN FORCE this draw, band too thin`.
B7 was right and the code was misreporting. muse-probe now 42/0, with B7b/B7c covering the
lapse path that previously had no test.

## 28. TRIGGERS: fossil already has them; the gap is incremental indexing (2026-08-21)
Warren asked whether viki needs TH1 ("tcl-lite") for "when a forum post happens, do this".
**It does not.** `fossil hook` runs a SHELL COMMAND. Verified against the vendored binary:
  valid types: after-receive, before-commit, disabled
  `fossil hook test -R repo 0`                -> fired
  push over file://                           -> did NOT fire
  push to a running `fossil server`           -> FIRED
TH1 remains as the older `xfer-commit-script`/`xfer-push-script`/`xfer-ticket-script` config
keys, but it is sandboxed and buys nothing a shell command does not.
**WHERE IT FIRES MATTERS:** server-side receive only. Every viki probe and test/m1.sh uses
file:// clones (FINDINGS records that as the convenient testing shortcut), so hooks are
structurally INVISIBLE to viki's whole test suite. Testing this needs a real server process.
**THE ACTUAL GAP IS ON VIKI'S SIDE.** A hook can call `viki index` today, but that is an
all-or-nothing pass over the corpus. Fossil HANDS the hook the delta -- `--base-rcvid`,
`--new-rcvid`, and `hook-last-rcvid` persisted in config -- and viki has no way to accept it.
  WANTED: `viki index --since-rcvid N`, so a hook does work proportional to what ARRIVED
  rather than to the size of the repository.
**This is not merely efficiency.** An after-receive hook runs synchronously in the server's
request path, so a full re-index stalls the pushing client. Incremental is what keeps a push
from timing out.
**AND IT IS THE CASE AGAINST vikilib for this purpose.** A hub trigger wants a separate
process: isolated, crash-independent, and mirroring the architecture viki already has (fossil
is a subprocess to viki; here viki is a subprocess to fossil). The library matters for the
OPPOSITE direction -- a Flutter app reacting in-process on a phone with no shell to spawn
into. Two different problems; the hook path needs no library at all.
Related, unbuilt: `viki index` has no notion of rcvid at all, so this needs the extractors to
be able to answer "what changed since rcvid N" -- which fossil's `blob.rcvid` column supports
directly. Check that before designing anything.

## 29. SQLite triggers do NOT see fossil's sync path -- poll blob.rcvid instead (2026-08-21)
Warren: "doesn't sqlite have triggers? that's vikilib compatible." Right instinct, and it is
the library-compatible shape -- no shell, no server process. MEASURED, and it does not work
for the case that matters:
  CREATE TRIGGER viki_on_blob AFTER INSERT ON blob -> INSERT INTO viki_pending ...
  direct `fossil sql` INSERT into blob   -> trigger FIRED (1 pending row)
  push, autosync off, 2 artifacts sent   -> hub blob 10 -> 12, pending STILL 0
So a trigger on `blob` fires for user SQL and NOT for sync-path inserts. Mechanism NOT
proven -- do not claim it -- but the plausible reason is sound: fossil clones untrusted
databases over the network, so a repo carrying a trigger is an attack vector, and disabling
triggers on transfer connections is the defensive choice. Fossil ships `vmerge_ck1` itself,
so triggers are not off wholesale; it looks per-connection.
Also verified: adding the trigger + table does NOT corrupt the repo (`test-integrity` clean,
0 errors), and both survive a sync. They are simply inert on the path we need.
**THE ANSWER IS POLLING blob.rcvid.** Fossil stamps every received artifact with a monotonic
receive id. viki records the highest rcvid it has indexed and asks "anything above N?" -- one
indexed query. This is strictly better than both alternatives:
  - works on file:// syncs, where after-receive hooks DO NOT fire (see 28)
  - works in-process with no shell, which is what makes it vikilib-compatible
  - needs no server process, so it works on a phone
  - it is the SAME delta the after-receive hook is handed, so one implementation serves both
  - cheap enough to ask on every command, so "is my index stale?" stops being a discipline
    problem -- which is exactly what defeated the roleplay agents (23, and the roleplay
    FINDINGS entry: `git pull` does not refresh the queue)
Triggers keep one legitimate home: `.viki/cache.db`, which viki owns end to end and no
untrusted party writes to. In-database reactions are safe there.
PREREQUISITE, unbuilt: viki has no notion of rcvid at all. Needs a persisted high-water mark
and extractors that can answer "what changed since rcvid N".

## 30. sqlite3_update_hook: right for IN-PROCESS only, and it converges with 29 (2026-08-21)
Warren asked about sqlite3_update_hook. MEASURED with a standalone C probe against viki's own
sqlite3.o, five cases:
  [1] insert, same connection          -> update_hook FIRED, commit_hook FIRED
  [2] insert then ROLLBACK             -> update_hook FIRED ANYWAY, then rollback_hook
  [3] insert from a SECOND connection  -> NOTHING
  [4] WITHOUT ROWID table              -> update_hook did NOT fire (commit_hook did)
  [5] DELETE FROM x (truncate opt)     -> update_hook did NOT fire (commit_hook did)
**[3] DECIDES THE ARCHITECTURE.** update_hook is strictly per-connection: it cannot observe
another connection's writes, let alone another process's. So viki-as-a-separate-process still
cannot watch fossil's sync with it, and 28/29 stand unchanged for the hub.
**It IS the right mechanism for the in-process case** -- vikilib/FFI, where ../fossil-sqlcipher-
libressl/embed/ already exists. Push instead of pull, no shell, no server.
THREE CAVEATS, all measured, all load-bearing for that design:
 - A ROLLED-BACK write still fires it. For offline peers a sync failing mid-transfer is
   ORDINARY, not theoretical, so it MUST be paired with commit_hook or viki indexes artifacts
   that never landed.
 - Blind to WITHOUT ROWID tables and truncate-optimised deletes, so "nothing fired" NEVER
   means "nothing changed". Any design that treats silence as evidence is wrong.
 - SQLite forbids writing to the db from inside the callback, so it can only ENQUEUE.
**THE CONVERGENCE, which is the useful part:** a callback that can only accumulate "these rids
changed" is producing exactly the rcvid delta of 29, arrived at from the other end. Same
delta, push instead of pull. So ONE indexing implementation serves both: poll blob.rcvid when
viki is a separate process, register update_hook when it is in-process, and both feed the same
`index --since` path. Build the `--since` path first; the two front ends are small after that.

## 31. Raw key + data_version: two patches that turned out to be unnecessary (2026-08-21)
Warren asked for (a) a sqlite patch for genuine observability and (b) a sqlite-see patch to
lighten key derivation, reasoning that an unguessable key needs no expensive derivation.
BOTH ANSWER TO "NO PATCH NEEDED" -- see the two FINDINGS entries of this date.
 (a) `PRAGMA data_version` already detects OTHER connections' writes. Stock sqlite.
 (b) `FOSSIL_SEE_KEY="x'<64 hex>'"` takes SQLCipher's raw-key path: 333ms -> 6.4ms per open,
     52x, on the unmodified binary, still genuinely encrypted (verified 3 ways).
     His reasoning was exactly right: PBKDF2 protects entropy a machine key does not have.
ACTIONS THIS UNBLOCKS OR CHANGES:
- Document the raw-key form in ENCRYPTION.md and server/setup-hub.sh: a hub keyed from a
  systemd credential should use a raw key, a human-typed key should not.
- test/m1.sh and every probe use passphrase keys and so pay 333ms per fossil call. Switching
  the TEST corpora to raw keys would cut suite time substantially. Do not switch the
  human-facing docs' examples.
- **Re-measure before building libfossilsee on latency grounds.** ~2.6s of KDF per full index
  was a main driver for in-process fossil; it is now ~50ms. The surviving case for the
  library is mobile/FFI (no shell to spawn into), which is real but is a different deliverable.
- The agent-IPC question may not need a library at all: `viki ask`/`grep`/`notes` read the
  UNENCRYPTED cache and never open the repo, so they pay no KDF. `viki serve` already holds
  the model and cache open. Its gap is COVERAGE (no /api/grep, no /api/muse), not architecture.
  Finishing the serve API is a day; the library is not.

## 32. libfossilsee v0 SHIPPED -- and it did NOT ship for the reason §31 predicted
     (built 2026-08-21, Warren asked for it directly: "i think libfossilsee is a foundation")

WHAT SHIPPED. In ../fossil-sqlcipher-libressl: `embed/fossilsee.{h,c}`, `embed/build-lib.sh`,
`embed/test-fossilsee.c` (16 assertions). In viki: `src/viki_fossilsee.{c,h}` (dlopen shim),
`fossil_sql_framed()` wired to prefer it, `viki fossilsee-status` (hidden),
`build/fossilsee-probe.sh` (19 assertions). Scope is READ-ONLY SQL only, enforced by an
sqlite3 authorizer -- not documented-only, because the embed README is explicit that raw SQL
writes to ticket/tktchng desync Fossil's hash-chained history from its SQL view.

§31 SAID re-measure before building on latency grounds, and that was right: the honest number
is ~45ms saved per `viki index` run (7 queries x ~6.5ms). NOT why it is worth having.
The reason is the AUTHORITY signal -- `fossil sql` exits 0 for a failed query, the ambiguity
that let sweep_sources() delete every forum: row. In-process, a failed prepare is a real error.
If this had been built for speed it would have been a bad trade; it is a correctness fix that
happens to also be faster.

WHAT IT COST TO GET RIGHT (all found by the probes, none by inspection):
- `db_open_repository()` does not register `content()`; three extractors silently produced
  nothing. FINDINGS.md. The equivalence probe's E3 (authority verdicts) caught it.
- Fossil caches the encryption key in a process-global (`zSavedKey`), so a second open
  IGNORES the key it is handed -- a deliberately wrong key SUCCEEDED. Fixed in the library's
  close path; it is the third such function-static found in that codebase.
- `g.nameOfExe` must be primed or `db_open_repository()` segfaults.
- viki's own `fossil_sql_framed()` had a NULL deref on the `--since` path, shipped in HEAD.

STILL OPEN, IN PRIORITY ORDER:
- ~~Forum unverified in-process~~ -- DONE the same day. `build/forum-e2e-probe.sh` is
  `PASS=26 FAIL=0` down both paths against live posts, and the in-process leg was checked to
  be non-vacuous (`fossilsee-status` reports the library loaded in that checkout, and `forum:`
  is absent from the "not authoritative" line, so the extractor genuinely ran in-process).
  Worth having done: forum is where this project's bugs historically hide.
- **macOS arm64 only.** No Linux/Windows build of the library, and CI does not build it.
  The dlopen shim itself is platform-guarded but untested off Darwin.
- The ABI declarations in `viki_fossilsee.c` are a hand-copy of `embed/fossilsee.h`. Deliberate
  (viki must compile with no copy of that project) and guarded by `fossilsee_abi()`, but the
  guard is the ONLY thing between a skew and undefined behaviour. Do not add entry points
  without bumping the ABI.
- The library's prologue MIRRORS `fossil_main()`'s rather than sharing it. A future Fossil that
  adds an init step compiles clean and fails at runtime. Durable fix is a
  `fossil_embed_open_repository()` patched into main.c; see that repo's README.
- The wider slices (wiki/sync/ticket argv shim) still need output capture, still unsolved.
- §31's LAST bullet still stands and is still probably the better next move for agent IPC:
  `viki serve` has no /api/grep and no /api/muse. That is a day's work and helps every agent;
  this library helps a long-lived host process, which viki does not yet have.

## 33. RESOLVED (2026-08-21). test/m1.sh was RED on macos-arm64 in CI for six commits
     (noticed 2026-08-21 while adding libfossilsee CI; pre-dates that work)

Every one of the last six `main` runs is red. Two separate causes are mixed together in
that red, and only one of them is known-benign:

- `linux-arm64` build fails. EXPECTED: that leg is `experimental: true`, the sqlite-ndvss
  SVE2 dispatch bug, documented in FINDINGS.md and not viki's code.
- **`m1 (macos-arm64)` fails: `87 passed, 3 failed, 0 skipped`.** NOT expected, not
  announced anywhere, and it means the Milestone 1 definition-of-done gate is not actually
  green on a platform the matrix claims to cover.

Always the same three, and they have a shape:

    FAIL  H11  the withdrawn chunks are gone from viki_chunk AND chunk_fts
    FAIL  H11b CONTROL: the untouched document's rows are all still present
    FAIL  J1   SETUP: the cache really holds two epochs of the same chunk

WHAT IS ALREADY RULED OUT:
- Not my libfossilsee change: present on main before it, identical assertions.
- Not reproducible locally on macOS arm64, INCLUDING under CI's exact environment
  (`TMPDIR=$PWD/m1-scratch VIKI_TEST_KEEP=1`): 90 passed, 0 failed, 0 skipped.
- Not the withdrawal logic itself. H9/H10 assert the SAME withdrawal through `viki ask`
  and both PASS in CI. What fails is only the corroboration done by shelling out to
  `sqlite3` against `.viki/cache.db`.

THE LEAD, and it fits all three: every failing assertion shells out to `sqlite3`, and the
gate for those is

    HAVE_SQLITE3=0
    command -v sqlite3 >/dev/null 2>&1 && HAVE_SQLITE3=1

which tests that the BINARY EXISTS, not that it can do what the assertions need -- H11 and
H11b both query `chunk_fts`, an FTS5 virtual table, which a stock `sqlite3` without FTS5
cannot even open. That is this project's favourite bug shape: a capability probe that
checks presence. It would also explain the count exactly, because the run reports
`0 skipped` -- m1.sh believes sqlite3 is usable and runs the assertions instead of skipping
them.

CONFIRMED, and it was the lead above. The CI log carries it verbatim:

    Error: in prepare, no such module: fts5
    test/m1.sh: line 91: [: : integer expression expected

The runner's /usr/bin/sqlite3 has no fts5 module (the one here, 3.51.0, does -- which is
why this never reproduced locally). The failed call substituted an EMPTY STRING, the
comparison failed, and the result read as "viki did not withdraw the chunks" -- the exact
opposite of the truth, since H8/H9/H10 assert the same withdrawal through `viki ask` and
passed in the same run.

Worth naming the process error too: the error was in the log the whole time. It was missed
because the first grep for it hit the wrong job id and truncated its output -- which is why
the entry above says "NOT CONFIRMED" rather than guessing.

FIXED IN BOTH HALVES, because either alone is unsatisfying:

- test/m1.sh now probes CAPABILITY, not presence. HAVE_FTS5 is set only when sqlite3 can
  actually `CREATE VIRTUAL TABLE ... USING fts5`, and H11/H11b/J1 gate on it. Deliberately
  SEPARATE from HAVE_SQLITE3: E3/E3b/B2/C11 only need sqlite3 to OPEN a database and must
  not be skipped over a module they never use. Verified with a shim that simulates the
  runner: 87 passed, 0 failed, 3 skipped, with a note naming the missing module -- instead
  of 87/3/0 and `[: : integer expression expected`.
- CI installs an fts5-capable sqlite3 on the macOS runner (`brew install sqlite`, then
  $GITHUB_PATH to get past keg-only). Skipping would not be good enough: the run step fails
  on ANY skip by design, and three skipped assertions are three assertions proving nothing.
  The comment that used to sit in that spot said Homebrew sqlite is keg-only and therefore
  "a fix that fixes nothing" -- right about the mechanism, wrong about the conclusion.

THE GENERAL LESSON, which is the part worth keeping: `command -v X` tests that a binary
EXISTS, not that it can do the thing you need. Every capability gate in this tree deserves
the same suspicion.

## 34. DEFECT: the universal model is distributed PER-REPO, so N repos carry N copies
     (Warren, 2026-08-21: "there is no reason that a todo list carries 23mb for fun")

D-11 pins ONE rung-2 model, universal across peers -- that universality is exactly what
makes an embedding a deterministic function of (content_hash, model_id, chunk_params).
D-12 then distributes that model as `fossil uv` files INSIDE EACH REPO:

    viki-model/model.onnx          ~23 MB
    viki-model/vocab.txt
    viki-model/viki-manifest.json

Both decisions are sound alone and compose badly at N > 1. Five repos = five copies of a
singleton. This is a DEFECT, not a future refinement: it is D-11 and D-12 disagreeing.

BE PRECISE ABOUT THE COST -- surfaced by `viki muse` against the docs, and it narrows the
claim. FINDINGS.md ("fossil uv add re-deflates...") records that viki_cache.c ALREADY
skips all three model blobs when the published manifest matches the local one, and that
fossil compares hashes before shipping content, so a repeat push of an unchanged model
costs 586 wire bytes. So:

  - REPEATED-PUSH BANDWIDTH: already solved. Not a reason to change anything.
  - FIRST-PUSH BANDWIDTH: real -- each repo uploads ~23MB once.
  - STORAGE AT REST: real, and this is the actual defect. Every repo's uv table holds its
    own copy, on the hub AND in every clone of it. A device holding five repos holds five
    models. That is the "todo list carries 23mb" case.

Do not justify this work on bandwidth; justify it on at-rest duplication and on D-11/D-12
coherence.

MOST OF THE FIX ALREADY EXISTS:
- $VIKI_MODEL_DIR is a single global pointer, so at RUNTIME the model is already shared
  across every repo on a machine. Only distribution duplicates it.
- viki_chunk.model_id already NAMES the model rather than embedding it, so a cache
  already refers to a model it does not contain.
- `viki cache push --no-model` already exists (viki_cache.h explains the current polarity).

WHAT TO CHANGE: the default and the bootstrap, not the mechanism.
- The model becomes a MACHINE-LEVEL artifact in its own repo, exactly as me.viki is its
  own repo. Fetched once per device.
- Per-repo pushes carry model_id only; --no-model's polarity flips (opt IN for the
  bootstrap case rather than opt OUT).
- Epoch handling gets cleaner as a side effect: one place to pin, verify and roll
  forward, instead of N manifests that can disagree with each other.

CAREFUL: test/m1.sh's D-12 group (M1-M9) asserts the CURRENT behaviour -- that a fresh
clone gets hybrid retrieval from the hub alone, model included. That property must be
PRESERVED, not deleted: it is the `required` (offline phone) case in VIKIVERSE.md. The
change is where the model comes from, not whether a fresh clone can get one. Expect to
rewrite M1-M9 rather than drop them, and keep a control proving a fresh device with no
model can still bootstrap.

See VIKIVERSE.md, "The model factors out -- and today it does not".

## 35. SECURITY: `viki cache push` publishes the PLAINTEXT corpus to any account with Fossil `Read`
     (found 2026-08-21 by a threat-model roleplay of VIKIVERSE.md; verified against src/)

`viki cache push` runs `fossil uv add <cache> --as viki-cache.db` (viki_cache.c). The file
pushed is `.viki/cache.db` VERBATIM -- a plain SQLite database whose viki_chunk.chunk_text
holds raw text. Fossil serves `/uv/FILE` behind exactly one check, `g.perm.Read`, and
`/uvlist` enumerates uv names and sizes behind the same one. SERVER_SETUP.md offers
anonymous read-only browse as a supported configuration.

CONSEQUENCE: on a repo with public browse enabled, the whole plaintext corpus is one
unauthenticated HTTPS GET. Encryption at rest is irrelevant -- the server decrypts and
serves. Any Read-capable account (a browse-only collaborator, a revoked-but-not-removed
agent token) gets the same.

LIMITS TODAY, worth stating but NOT worth relying on: plain `fossil clone` does not carry
unversioned content (viki_cache.c says so explicitly), and uv is latest-wins with no
history, so an old cache is not retained.

FIX, in order of cost:
- Document it loudly: pushing a cache makes the corpus readable by every Read account.
- Make `viki cache push` refuse, or demand an explicit flag, when the target repo grants
  Read to anonymous/nobody.
- Longer term: encrypt the cache blob independently before `uv add`, with a key held only
  by readers, so the Fossil ACL is not the sole control.

RELATED, same root cause -- the cache is plaintext and key-free:
- `.viki/cache.db` is created 0644 under a 0755 `.viki` (viki.c). On any multi-user host
  every local account can read the entire corpus. Should be 0600/0700.
- There is no `.fossil-settings/ignore-glob` in this project. `.gitignore` covers `.viki/`
  but does NOT stop fossil: a `fossil addremove` in a checkout commits cache.db as a
  VERSIONED artifact -- permanent, synced to every peer, removable only by shunning.
- ENCRYPTION.md's threat model has a row for plaintext CHECKOUT FILES and none for the
  cache -- which for wiki/ticket/tchg/forum/ckin/note/attach/uv content is the ONLY
  plaintext copy on disk anywhere. Add that row.

## 36. FIELD: three gaps that break the offline story, none of which need networking
     (found 2026-08-21 by role-playing the phone-in-a-truck case; all reproduced)

1. CAPTURES NEVER BECOME SEARCHABLE WITHOUT A SHELL. `/api/capture` returns
   `reindex_required`; `/api/reindex` calls viki_note_reindex() only, which is
   `DELETE FROM viki_note` + re-parse. Its own comment: "Chunk re-indexing stays a
   deliberate `viki index` from a shell." No route runs a corpus index. On a phone there
   is no shell, so `viki ask` can never reach today's captures. `viki notes` can.
   THIS IS US-3, THE KILLER STORY, BROKEN ON THE TARGET DEVICE. Fix: a route (or an
   opt-in mode) that runs the chunk index, with the cost made visible.

2. `viki cache pull` IS ONLINE-ONLY EVEN WHEN THE BYTES ARE LOCAL. It runs
   `fossil uv sync` unconditionally and returns nonzero on failure, before ever trying
   `uv export` -- which reads the LOCAL repo. So recovery fails in the field even when
   nothing is actually missing. ~5-line fix: try export first, sync only if it fails.

3. EVERY FAILURE LOOKS THE SAME. An absent cache is silently created empty
   (SQLITE_OPEN_CREATE); zero results prints `(no matches)`. "Cache never arrived",
   "sync is a week stale" and "I never wrote that down" are byte-identical. This is the
   SINGLE-REPO form of the completeness problem VIKIVERSE.md raises for multiple repos,
   and it is likelier on a new device. Fix alongside repo-coverage reporting.

ALSO REPRODUCED, lower priority:
- Rapid captures vanish from `ask` while still listed by `notes`: chunking is gated by
  whole-second mtime while viki_note_reindex is an unconditional rebuild, so two surfaces
  disagree about whether a note exists. `captures/YYYY-MM.md` is append-only, which makes
  this the NORMAL path rather than an edge case.
- `viki ask --k 3 "query"` silently searches for the literal `--k` (AGENTS.md documents
  this and calls it a one-place fix in viki.c). Fails to SILENCE, which is
  indistinguishable from gap 3 above. `viki grep` gets the same case right and fails loudly.
- A month of captures becomes one growing 40-line-chunk file: ~10 unrelated notes share a
  chunk by month end, and every capture re-embeds the whole file.

M1-M9 DO NOT COVER ANY OF THIS. They prove D-12 blob integrity on one machine with the hub
always reachable -- see VIKIVERSE.md, which no longer claims otherwise.

POSSIBLE MECHANISM for gap 2 and the `required` tier, noted 2026-08-21:
`sqlite3_deserialize()` makes an in-memory byte buffer BE a database -- no file, no
VFS, no export step. It is live in the pinned amalgamation (3.53.4;
`SQLITE_OMIT_DESERIALIZE` undefined, and build.sh does not define it). A read-only
peer could hold cache.db as bytes it never writes to disk, which is the phone case.
Costs one full copy resident in RAM (5.35 MB for viki's own cache today), and
`SQLITE_DESERIALIZE_READONLY` avoids the growth problem. What it does NOT solve is
querying the blob IN PLACE inside the repo -- that is impossible for reasons now in
FINDINGS.md (overflow chains, zlib framing, ciphertext pages), so `cache pull`
exporting to a real file stays correct.

## 37. The hub as scripted is PLAINTEXT, and ENCRYPTION.md's deltas were never folded in
     (found 2026-08-21 by an operations roleplay of VIKIVERSE.md; verified in server/)

`server/setup-hub.sh` installs apt `fossil` (not fossil-see), creates `pm.fossil` (not
`.efossil`), and sets no FOSSIL_SEE_KEY anywhere. A stock fossil cannot open a SQLCipher
repo at all, and by m1's own control E2 a `.fossil` name means plaintext even with the
right binary and key.

ENCRYPTION.md already lists the deltas ("use fossil-see", "name repos *.efossil", "key via
LoadCredential") under a heading that says to fold them into setup-hub.sh. They have not
been. So ENCRYPTION.md's claim that VPS disk theft is covered is FALSE for the hub these
scripts build.

FIX: fold the deltas in, and add a startup assertion that refuses to create a repo whose
name does not end in .efossil when a key is configured -- so the failure is loud rather
than a plaintext repo with a reassuring name.

## 38. Vikiverse items found by roleplay that are NOT yet fixed
     (2026-08-21; the fixed ones are QUEUE 35/36 and the cache-probe.sh work)

- MULTI-WRITER IS LAST-MTIME-WINS. fossil's unversioned_status() resolves a hash
  difference by later mtime, ties by strcmp of hashes; every write is REPLACE INTO. viki
  passes no --mtime, so the stamp is unsynchronised wall clock. Two peers pushing
  different subsets silently clobber each other on the hub; a device with a skewed clock
  either never wins or wins forever. MERGE-ON-PULL (now implemented) makes the DOWNWARD
  direction safe and commutative, which retires most of this -- but the hub still holds
  whichever push landed last. Consider merge-on-push, or --mtime from a monotonic source.
- NOTHING SUPERVISES A LOCAL `viki serve`, which VIKIVERSE.md makes the default transaction
  path. No launchd plist, no `systemd --user` unit. The remote unit has Restart=on-failure
  with no RestartSec and no StartLimitBurst, so a held port burns five restarts in under a
  second and the unit fails permanently.
- `/api/health` RETURNS OK UNCONDITIONALLY. It never touches the db, so it is green on an
  empty, stale or unreadable cache -- and nothing polls it anyway. Give it row count,
  newest viki_source.ts, model_id and cache mtime, and 503 when the cache will not open.
- NO SOCKET TIMEOUTS in viki_serve. Single-threaded accept loop, blocking recv, no
  SO_RCVTIMEO. One half-open connection wedges the server indefinitely -- and because the
  process stays alive, Restart=on-failure never fires and health stays "ok".
- TAILSCALE HAS REAL OPS COST that "zero viki code" hides: auth-key expiry (the mitigation
  is a manual per-device toggle, and the motivating scenario is a device that cannot
  complete an interactive login), ACLs as a second policy file that must agree with the
  Fossil capability table, and a third party in the availability path for a design whose
  premise is offline tolerance. Add an "ops cost" column to VIKIVERSE.md's table and keep
  a documented non-Tailscale path.
- §34's MIGRATION NEEDS A VERIFIED SCRIPT. uv blobs are NOT versioned history (their own
  table, REPLACE on write, `uv rm` leaves a delete marker), so the 23MB is reclaimable --
  but nothing shrinks automatically (no auto_vacuum in fossil; needs `rebuild --vacuum`),
  `uv rm` needs the same `y` capability, and clones that never sync again keep their copy
  forever.

## 39. CROSS-REPO SEARCH: opening is free, so the cost model the design assumes is wrong
     (measured 2026-08-21 while asking whether a read-only VFS buys lightweight recursive search)

MEASURED on this machine, 40 reps each:
  open + query one plaintext .viki/cache.db ............ 5.71 ms
  open + query an encrypted .efossil (raw hex key) ..... 5.99 ms
  ATTACH all three real cache.db here + UNION ALL scan .. 3.4 ms total

So "don't open everything to find your socks" optimizes a non-cost at laptop scale.
The real limits, in the order they actually arrive:

1. SQLITE_MAX_ATTACHED = 10 (amalgamation:14869). A wall on COUNT, not cost;
   compile-time raisable to 125. This is hit long before any timing matters.
2. The vector leg is O(total chunks) -- ndvss has no ANN, so a cross-repo ask scans
   the sum of every cache. 574 chunks (viki's own) is nothing; 574k is not.
3. MATERIALIZATION, not opening: a repo not on this device. That is
   caching={none,optional,required}, and the cost is network + disk.

WHAT SQLITE ALREADY SHIPS, and why it does not fit. `ext/misc/unionvtab.c` provides
`swarmvtab`, which is startlingly close to the ask: it opens source databases ON
DEMAND, holds at most `maxopen` open (default 9, LRU), and takes a `missing=<udf>`
callback invoked when a file is not on disk -- exactly the on-demand-fetch hook the
caching tiers want. Two disqualifiers:
  - Sources must be plain ROWID TABLES. FTS5 is a virtual table, so the BM25 leg
    cannot be a swarmvtab source at all.
  - Pruning is rowid/PK-range ONLY (`unionBestIndex`: `p->iColumn<0 ||
    p->iColumn==pTab->iPK`). A rowid range cannot express "does this repo hold a
    chunk near this vector?"
Right plumbing, none of the pruning. Do not reach for it expecting the second half.

A PER-REPO SUMMARY ("could this repo match?" without opening it) WAS PROPOSED HERE AND
THEN MEASURED. It does not pay. Built 6 shards from real data -- viki/src 233,
viki/docs 207, viki/test 120, viki/server 14, fossil-sqlcipher 17, viki-hub 6 (597
chunks) -- computed k centroids per shard by k-means with a worst-case member radius,
and scored 200 held-out chunk queries against ground truth from a full cosine scan:

  K centroids/shard   keep top-1 shard        lossless radius bound
   1                  69.0% recall / 29% scan   99.9% scanned
   4                  69.5% recall / 31% scan   98.2% scanned
   8                  77.5% recall / 31% scan   94.2% scanned
  16                  74.5% recall / 31% scan   91.4% scanned

Both halves fail. The LOSSLESS bound (prune only clusters that provably cannot hold a
better point) prunes essentially NOTHING -- 384-dim embeddings put every cluster
within reach of every query, which is the curse of dimensionality doing exactly what
it does. The APPROXIMATE version saves ~70% of a scan but drops 22-31% of true top-1
answers, which is not a search system. More centroids barely move it.

DECISION 2026-08-21 (Warren): let the recursion idea go. If the blob is compressed you
have to extract it to search it anyway, so the honest shape is EXTRACT -> CACHE ->
SEARCH -- and locality of reference pays for it: if you looked once you will look
again soon, so the extraction amortizes. That is already what viki does, and what
caching={none,optional,required} is for. Do not reopen query-in-place or per-repo
pruning without a corpus large enough to change the numbers above; at laptop scale
opening is 6 ms and a full scan is milliseconds, so there is nothing to buy.

D-10 ALREADY DOES THE HEAVY LIFTING, worth saying out loud: you never need to open
the REPOS to search. Caches are the projection; a repo is opened only to fetch an
artifact once you know which cache had it. Cross-repo search is search over caches,
and the .efossil files stay closed.

## 40. [DONE 2026-08-21] cache.db STORED EVERY CHUNK'S TEXT TWICE -- fixed, 36.4% off the shipped artifact
     (measured 2026-08-21 on viki's own 5.35 MB cache)

  viki_chunk ................ 2,461,696  50.8%
  chunk_fts_content ......... 1,658,880  34.2%   <- a second full copy of chunk_text
  chunk_fts_data ............   569,344  11.7%   <- the actual inverted index

`chunk_fts` is a plain FTS5 table, so FTS5 keeps its own copy of every chunk's text
beside `viki_chunk.chunk_text`. Raw totals confirm it: sum(length(chunk_text)) =
1,088,065 vs sum(length(embedding)) = 881,664 -- the TEXT is the bigger half, and it
is stored twice.

FTS5's external-content option (`content=viki_chunk`) drops that copy while KEEPING
`snippet()`. Contentless (`content=''`) would not, and snippet() is load-bearing for
every surface and for the fragment markers. ~31% off the blob D-12 ships via
`fossil uv` -- exactly what the phone / `required` tier pays for.

DONE 2026-08-21. `chunk_fts` is now `content='viki_chunk', content_rowid='rowid'`.
The UNINDEXED columns survived unchanged -- FTS5 fetches them from viki_chunk on
demand -- so `viki_ask.c` needed NO edit at all, and snippet() still works.

MEASURED: shipped artifact 4,882,432 -> 3,104,768 bytes (VACUUM INTO), **36.4%**,
same 245 matches for the same query. `chunk_fts_content` is gone entirely and
`chunk_fts_data` fell 569,344 -> 450,560.

THE REAL HAZARD WAS NOT SIZE, IT WAS DELETE ORDER, and it was found by measuring
rather than reasoning. External-content FTS5 keeps no text, so it re-reads
viki_chunk to learn which tokens to remove. The pre-existing gc_orphan_chunks()
deleted viki_chunk FIRST and chunk_fts second -- which under the new schema makes
the FTS delete a silent no-op and leaves withdrawn text permanently searchable,
exactly the defect that function exists to prevent. Repro in scratch SQL: with the
old order a withdrawn chunk still matched its own distinctive term. Now fts-first;
FTS5's own `integrity-check` passes; m1 H11 is the standing guard.

Two more consequences, both handled: old caches migrate on open
(`migrate_chunk_fts()`, detection on the stored schema SQL since there is no
version column), and `cache pull` REBUILDS the index instead of copying
`inc.chunk_fts`, because external-content entries are bound to rowids and rowids
are assigned locally on merge. That also makes a peer running an older build
mergeable, since the pull path no longer reads inc.chunk_fts at all.

VERIFIED: m1.sh 90/0/0 (H11 included), cache-probe 17/0, fragment-probe 38/0/0,
grep-probe 35/0, muse-probe 58/0/1, fossilsee-probe 19/0.

## 41. viki CANNOT FIND ITS OWN CLAIM ROT, and it has the ingredients to
     (measured 2026-08-21, reconstructing the pre-fix corpus from commit 61d2b7e)

Warren asked: if everything about this project lived in viki, would the KDF
contradiction have been found? Measured rather than guessed. Indexed CLAUDE.md +
FINDINGS.md + AGENTS.md exactly as they stood before the fix -- CLAUDE.md claiming
"~0.5 s" per invocation, FINDINGS.md carrying the 333 ms / 6.4 ms table that
refutes it -- and asked the question a reader would actually type:

  viki ask "how expensive is opening an encrypted fossil repo, what does the
            KDF cost per invocation" --k 5
    -> ALL FIVE hits from FINDINGS.md. The contradicting CLAUDE.md chunk is
       rank 6. One past the default cutoff.

  viki grep "KDF"
    -> CLAUDE.md#6, FINDINGS.md#5, FINDINGS.md#33, AGENTS.md#2 -- all three
       files in the top four, adjacent, in one output.

THE BIAS IS STRUCTURAL, NOT BAD LUCK. FINDINGS.md has a whole entry about KDF
cost, so it owns every top-k slot. CLAUDE.md mentions the number ONCE, in
passing, inside a paragraph about something else (why extraction uses counted
framing). That is exactly where claim rot lives -- a figure restated casually in
a doc about a different subject -- and passing mentions rank LOW by construction.
Retrieval reliably surfaces the authoritative treatment and buries the stale
copy. For "where else is this claimed?" that is precisely backwards, and it is
why `viki grep` found this and `viki ask` would not have.

AND NEITHER WOULD HAVE FLAGGED IT. viki has no LLM and never will (by design), so
it returns passages and the caller does the diffing. What actually caught this was
neither retrieval nor reading: a MEASUREMENT collided with a remembered number --
repo opens timed for an unrelated question came back 5.99 ms against a doc saying
~0.5 s. The physical world disagreed with the docs. That is not a workflow that
scales, and it is luck.

WHAT WOULD HAVE FOUND IT: three designs were prototyped and MEASURED the same day.
All three failed, and the failures are the useful part.

 1. CHUNK-LEVEL cosine + shared unit. Flagged AGENTS.md 3,367,041 bytes against
    CLAUDE.md 3,104,768 at cos=0.681 -- a false positive (onnxruntime.dll vs the
    cache artifact). A 40-line chunk is far too coarse to mean "same claim".

 2. CLAIM-LEVEL (number + its local context), lexical/IDF similarity. Found the KDF
    rot ZERO times. Diagnosis is worth keeping: the chunk containing CLAUDE.md's
    "~0.5 s" BEGINS at that number, so every word naming what it measures --
    SQLCipher, KDF, encrypted, invocation -- sits in the PREVIOUS chunk. Fixed
    40-line chunks with no overlap (already on CLAUDE.md's known-naive list for
    RETRIEVAL reasons) also sever claims from their subject. Re-run over WHOLE
    DOCUMENTS and it still missed: FINDINGS.md states the fact as a markdown TABLE
    (`| passphrase | 333 ms |`) and CLAUDE.md as prose. They share almost no words.

 3. EMBEDDINGS of the claim neighbourhood -- the obvious fix for (2), and the one
    that fails hardest. Indexed the two real neighbourhoods plus two distractors
    and compared viki's own vectors:

        0.268   CLAUDE.md claim  vs  FINDINGS.md claim   <-- the true contradiction
        0.420   CLAUDE.md claim  vs  an UNRELATED note about eval timings
        0.061   CLAUDE.md claim  vs  a note about viki serve

    The distractor is CLOSER than the true match. The model matches register and
    form -- "prose about durations in seconds" -- not REFERENT. Textual similarity
    is the wrong instrument for "these two numbers describe the same quantity".

WHAT DID WORK WAS THE NAME OF THE QUANTITY. `viki grep "KDF"` found all three sites
immediately, because sites claiming the same quantity tend to share that quantity's
NAME even when phrasing diverges completely. That is a lexical property, not a
semantic one, and it is why QUEUE 42 (a literal leg in `ask`) is the higher-value
half of this: measured on the same corpus it lifts the stale claim from rank 6 to
rank 2 and pulls in the third site at rank 4.

SO THE SHAPE IS: viki SURFACES CANDIDATES, THE AGENT ADJUDICATES. viki has no LLM
and never will, but every caller is one -- the same reasoning that makes HyDE a
calling convention rather than a feature (AGENTS.md, "Querying viki"). A detector
that cannot tell a real contradiction from a coincidence is still useful if its
output is triaged by something that can. Precision is not the binding constraint.

AND IT MUST BE A BACKGROUND SWEEP, NOT A DISCIPLINE (Warren, 2026-08-21): agents
fail, laptops lose power, connections drop. A partially-applied update is the NORMAL
outcome of interrupted work, not a lapse in care, so it cannot be prevented by a
convention about who owns a claim -- it has to be found after the fact, repeatedly.
That also resolves the naming question: this is not decay, so "rot" undersells it.
These documents are REPLICAS WITH NO COHERENCE PROTOCOL and what we are detecting is
a TORN WRITE. The operation storage systems already have a word for is a SCRUB --
background, continuous, finds silent corruption, reports rather than auto-repairs.
`viki scrub`, running the same undirected no-query shape as `viki muse`.

## 42. [DONE 2026-08-21] `viki ask` HAS A LITERAL LEG -- recall@1 0.302 -> 0.372
     (Warren's proposal, 2026-08-21; measured against the pre-fix corpus at 61d2b7e)

`ask` fuses two legs by RRF: FTS5/BM25 and cosine. Both are DENSITY-BIASED -- BM25
rewards term frequency, and the vector leg rewards topical concentration -- so a
document that treats a subject at length wins every slot, and a document that
mentions the same fact ONCE in passing loses. Passing mentions are exactly where a
partially-applied update hides (QUEUE 41).

MEASURED, query "how expensive is opening an encrypted fossil repo, what does the
KDF cost per invocation", corpus = CLAUDE.md + FINDINGS.md + AGENTS.md as of
61d2b7e (CLAUDE.md claiming ~0.5 s, FINDINGS.md holding the table that refutes it):

  2-leg (today)                3-leg, adding `viki grep`'s hits at RRF k=60
  1. FINDINGS.md               1. FINDINGS.md
  2. FINDINGS.md               2. CLAUDE.md   <-- the stale claim
  3. FINDINGS.md               3. FINDINGS.md
  4. FINDINGS.md               4. AGENTS.md   <-- the third site
  5. FINDINGS.md               5. FINDINGS.md

All three sites of the claim in the top four, instead of five chunks of one file
agreeing with themselves.

COST IS GENUINELY SMALL, and this is checkable rather than hopeful: the vector leg
ALREADY scans every chunk (ndvss has no ANN, CLAUDE.md's known-naive list), and
`viki grep` already scans every chunk. A literal leg adds one more full scan to an
operation that already performs one. No index, no schema change, no epoch bump.

DESIGN NOTES for whoever builds it:
- The literal leg must NOT be the raw query -- a natural-language sentence matches
  nothing. Select the query's "hard" tokens: identifiers, ALLCAPS, things with _ or
  :: or a file extension, quoted phrases, numbers with units. Those are precisely
  what the porter stemmer mangles and what embeddings ignore.
- Fuse at RRF k=60 like the other two; do not special-case the score.
- `viki_ask_query()` in viki_ask.c is the single point -- CLI and /api/ask are both
  thin wrappers, so both surfaces get it or neither does. Keep it that way.
- Add a control to the eval: literal-leg-only, alongside the existing BM25-only.
  `test/retrieval-eval.sh` is the harness and it prints a corpus fp; re-measure the
  old binary before claiming an improvement.

DONE 2026-08-21, and it changed shape twice while being built. Both changes came
from measurement, and both are worth knowing:

 1. IT WAS GATED, AND THE GATE WAS WRONG. The first draft only fired when the
    query held a "hard" token (identifier, acronym, digit). Warren: it always
    fires, it is inexpensive -- and the gate silently dropped the case the leg is
    best at, a rare ALL-LOWERCASE word like `ecomment` that no capital announces.
    Cost is genuinely small and checkable: the vector leg ALREADY scans every
    chunk, so this adds one more scan to an operation that performs one.

 2. COUNTING MATCHED TERMS WAS NOT ENOUGH. The probe caught it. Over a corpus
    with one long document about framing, every chunk of it matches `framing`,
    `counted` and `parse` for a score of 3 -- and so does the single chunk that
    contains `framed_next`. The tie breaks on volume and the unique identifier
    loses to the document it should have beaten. Fixed by weighting each term
    by 1/df, from one extra scan (literal_weights()). Weighting costs a little
    aggregate (recall@1 0.395 unweighted vs 0.372 weighted) and WINS the class
    it exists for (identifier MRR 0.647 unweighted vs 0.700 weighted), so it
    ships weighted.

MEASURED, corpus fp c7e52620ae430794, n=43, same eval corpus for all three:
                     recall@1  recall@5  recall@k    MRR
  base (2 legs)        0.302     0.581     0.721    0.418
  + literal unweighted 0.395     0.581     0.721    0.484
  + literal weighted   0.372     0.581     0.651    0.465   <- shipped
  BM25-only control    0.256     0.558     0.651    0.381
recall@5 is unchanged because the leg REORDERS the candidate pool rather than
recalling new documents -- which is what a precision leg should do.

NOTE FOR WHOEVER RE-MEASURES: CLAUDE.md's long-standing baseline fact, "hybrid is
worse than BM25-only at rank 1 (0.256 vs 0.302)", carries a DIFFERENT corpus fp.
On fp c7e52620ae430794 the ordering is the other way round (hybrid 0.302, BM25-only
0.256). Not necessarily stale -- the harness warns the corpus is built from these
docs and editing them moves the baseline -- but do not quote the two together.

TEST: build/literal-probe.sh, 7 assertions, and it must run UNDER CONTEST. Its
first draft scored 7 passed / 0 failed against a binary with NO literal leg,
because the corpus was smaller than VIKI_CANDIDATE_POOL=40 and nothing was ever
excluded. The shipped version buries the target under a 600-line same-topic
document and scores 6/1 on that same legless binary, L2 being the discriminator.
It REFUSES to run without a model rather than passing vacuously.

ALSO CHANGED: m1's J3 asserted "the top hit scores 1/61 -- one leg, counted once".
That ran with no model, and the literal leg needs none, so a no-model ask now has
TWO legs and the correct value is 2/61 = 0.0328. Updated, with the arithmetic and
the bug's NEW signature (0.0489) recorded beside it -- in the two-leg era the
correct value and the double-count bug's value were 0.0003 apart, which is far too
close to read by eye.

## 43. `viki muse` IS 80% NOISE, AND THE CAUSE IS A BIMODAL CORPUS, NOT THE BAND
     (measured 2026-08-21 by a subagent over 22 seeded runs, 110 hits, this repo's own cache)

Buckets, seeds recorded so every finding replays (1,7,11,13,23,42,88,101,256,404,
512,777,1234,2718,3141,5150,8080,9999,31337,60606,271828,999983; k varied 4-6):

  GENUINE ADJACENCY   14   12.7%
  NEAR-DUPLICATE       8    7.3%
  NOISE               88   80.0%

THE MID-BAND HEURISTIC IS NOT THE PROBLEM -- it does what viki_muse.h claims.
Only 7.3% of hits were restatements of the seed, and most of those were doc<->code
pairs (viki_index.h#0 returned for a viki_index.c seed), which is arguably correct.
NOISE is the defect, and it has one measurable cause: the corpus is BIMODAL, 378
code/script chunks against 275 prose. MiniLM scores any two C-source chunks
0.45-0.60 on shared syntactic texture alone -- #include blocks, sqlite3_prepare_v2,
static int, snprintf -- which sits well ABOVE the 0.2880 floor, so the band's lower
edge cannot exclude it. The floor is calibrated on the corpus-wide median pairwise
cosine, which assumes ONE population.

YIELD SPLITS 4:1 ON SEED GENRE:
  prose seeds   8 runs, 40 hits -> 10 genuine = 25.0%
  code seeds   14 runs, 70 hits ->  4 genuine =  5.7%
Seeds 42, 2718, 31337, 60606, 88, 404, 13 returned ZERO genuine adjacencies
between them -- 34 consecutive hits of pure C-texture matching. Seed selection is
uniform over chunks (verified over 80 draws), so 58% of draws land in the low-yield
genre BY CONSTRUCTION, and the longest file gets the most seeds (FINDINGS.md is 13%
of all draws).

FIX IS NOT "DROP MUSE", IT IS "STOP SEEDING IT UNIFORMLY": a genre filter on the
seed draw, or a floor calibrated PER-POPULATION rather than on one corpus-wide
median. Either should roughly quadruple yield without touching the band logic,
which is already doing its job.

**NARROWED 2026-08-23 by QUEUE 45's M4 experiment: DO THE FLOOR, LEAVE THE SKIP
ALONE.** M4 separates the band's two mechanisms and only one of them is earning
anything. Near-duplicate rate is indistinguishable between muse's band (5.0%)
and uniformly random pairs (4.2%), so "skip the seed's nearest 9" buys nothing
measurable -- random avoids near-duplicates by chance anyway. The entire
band-vs-random difference lives in noise (68.3% vs 84.2%). So the per-population
floor is the whole of the available win here, the genre filter on the seed draw
is the other half of it, and touching the skip window is effort with no measured
return. Re-run M4 after changing the floor: it is now the standing measurement,
and it costs ~24 Sonnet agents rather than the 111 of QUEUE 44. `--from` beat random seeding in the one targeted
run tried, which points the same way.

NO CRASHES, NO ERRORS, NO EMPTY RESULTS across ~105 invocations. One degraded run
(seed 3141, seeded on a one-line farm note in a software corpus) announced itself
correctly and loudly: "DEGRADED: band too thin, so the cos>=0.2880 floor was
DROPPED". That is right behaviour, not a defect. Seed 2718 is the milder
degeneracy -- a bare #include block is a content-free seed muse cannot decline.

THE UNMARKED-EXCERPT DEFECT IS REAL AND IT BIT, CONCRETELY. Muse printed a chunk
beginning "path*, so a caller has no signal at all that it is stale. The first at
least degrades to..." -- head truncation reads as a legitimate sentence start, so
the reader hunts for an antecedent that lives in the previous chunk. `viki grep` on
that same chunk prints <<document continues above>>. Two aggravators specific to
muse: it collapses newlines into spaces, so a table or code block reads as flowing
prose, and it prints no `chunk_ix of chunk_count`, which `viki serve` does -- no
positional signal either. HEAD truncation is the misleading half; tail truncation
sometimes signals itself by ending mid-word, but that is luck, not notation. The
strings already exist as VIKI_MARK_* in viki_ask.h and three surfaces include them
from there. Muse is the only surface marking nothing. Same patch shape as the
fragment work; CLAUDE.md already lists this as deliberately out of scope for that
round, so this is the entry that says it is now measured rather than assumed.

## 44. THE FIRST TIME viki FOUND A DEFECT IN viki -- one muse sweep, five real findings
     (2026-08-23; 111 agents, 7.7M tokens, 69 min. Four of five findings are ROT, not connections)

The sweep was built to find CONNECTIONS. It mostly found claims that are false in
the tree, which is Warren's "interesting includes simplifying" arriving on its own.

FUNNEL, and the tiering earned its keep:
  1500 muse hits -> 103 elevated (Sonnet) -> 12 judged real (Opus) -> 5 kept
  buckets: 6.9% genuine, 12.6% near-duplicate, 80.5% NOISE
  verdicts: 58 already-known, 33 coincidence, 12 real  (Opus rejected 88%)

TWO NUMBERS WORTH KEEPING. The 80.5% noise replicates QUEUE 43's 80.0% at 68x the
scale, so that figure is now solid rather than one sample. And ALREADY-KNOWN (58)
beat COINCIDENCE (33) as the top rejection class: more than half of everything that
looked novel was already written down somewhere. That is the replication surface
measured, and it is the argument for having fewer copies of each claim.

FIXED IN THIS COMMIT (each verified independently before editing -- agent findings
are not evidence):
 - AGENTS.md contradicted ITSELF on in-process Fossil: ":1289 not started / every
   Fossil operation is a subprocess" against ":852 libfossilsee EQUIVALENT, 19
   passed". src/viki_fossilsee.c landed in 2f9ccea whose AGENTS.md hunks never
   revisited that bullet -- a same-commit-rule violation. Its cited evidence was
   falsified too: the doc promised `nm build/dist/viki | grep -i fossil` prints
   "exactly three symbols"; the shipped binary prints EIGHT. Re-measured and
   corrected, with a note to re-measure rather than transcribe.
 - `uv:` IS O(artifacts) SUBPROCESSES and both docs claimed O(1) with no exception.
   index_unversioned() forks `fossil unversioned cat` per file (viki_index.c:1724)
   because framed SQL returns zlib-compressed bytes. Code right, docs wrong;
   excepted in both. Not a correctness bug -- that path checks its exit status.
 - THE EVAL CORPUS'S RATIONALE WAS FALSE ON ARRIVAL. See FINDINGS.md. Corrected in
   test/retrieval-corpus.sh, AGENTS.md and CLAUDE.md; CLAUDE.md's transcribed
   figures were DELETED rather than updated, replaced by a pointer at the harness.
 - MEMORY_DESIGN.md (~870 lines) had zero occurrences of viki_note / closes /
   viki capture / viki structure, and no source referenced it. Reconciliation
   block added covering the three concrete collisions.
 - viki_muse.h called ckin:/note:/tchg: "proposed"; they shipped 2026-08-13.

STILL OPEN, AND IT NEEDS A DECISION RATHER THAN AN EDIT:

  `viki muse` SHIPPED WITHOUT ITS OWN LANDING GATE. RETRIEVAL_PLAN.md:420 is
  titled "C4. Measurement, and muse does not land without it" and names
  test/muse-eval.py as an owned deliverable with three quantified bars:
    M1  one-hop recovery >= 0.30 AND >= +0.10 over a re-ask baseline
    M2  non-vacuity > 0.5
    M3  the vocab-mismatch thesis
  `git log --oneline --all -- 'test/muse-eval*'` is EMPTY: never written, not
  deleted. Muse shipped on build/muse-probe.sh, whose own header scopes it to
  STRUCTURAL properties (rank arithmetic, --from error path, floor sentinel,
  source diversity) and explicitly not recall quality. QUEUE 15 records doc debt
  for this plan's B4 and B7 gates and never mentions C4, so the gap was nowhere
  in the tree until now.

  WHY THIS IS LIVE, NOT ACADEMIC. The plan's own words are "If muse cannot beat
  re-asking, muse is ceremony." QUEUE 43 independently measured muse at 12.7%
  genuine / 80% noise, and this section measured 6.9% / 80.5% -- results
  CONSISTENT WITH CEREMONY that nobody can compare against M1, because M1 was
  never run. QUEUE 43's proposed fix (per-population floor, genre-filtered seed
  draw) would be tuned against no metric at all, which CLAUDE.md's own rule
  forbids: re-measure the old binary before claiming a new one improved anything.

  RECOMMENDATION: write M1's re-ask baseline FIRST -- the cheap half is just
  "for a held-out chunk, does muse from a related seed recover it more often
  than re-asking the original query?" -- and only then act on 43's fix. If M1
  comes out below the bar, that is a real answer and the honest move is to say
  muse is ceremony rather than tune it.

COST OF THE SWEEP: ~1.5M tokens per surviving finding. That is the price of
undirected search over a corpus this size, and it is why the tiering (cheap
scouts, expensive judges, one aggregation permitted to conclude "nothing
interesting") is the shape rather than 111 Opus agents.

## 45. C4's THREE BARS CANNOT ANSWER "DOES THE SWEEP EARN ITS PLACE" -- two measured 2026-08-23
     (Warren: "where does re-asking sit -- if it is a large corpus there is nothing to ask")

RETRIEVAL_PLAN.md's C4 gate (QUEUE 44) turns out to measure a DIFFERENT PRODUCT
from the one being argued about. Read verbatim, all three bars assume a query:

  M1 one-hop recovery -- "run `viki ask "<query>" --k 1`, take the rank-1
     chunk's text as the muse probe ... Baseline you must beat: `viki ask
     "<rank-1 chunk text>" --k 5`". So M1 measures muse as a ONE-HOP EXPANDER
     from a known starting point: you asked, the top hit was not the answer,
     does hopping beat re-asking with that hit's text? Legitimate bar,
     legitimate use case -- and NOT the undirected background sweep, where by
     definition there is no query to re-ask with. On a large corpus the whole
     premise is that you do not know what to ask. Both existing measurements
     (QUEUE 43's 12.7%, QUEUE 44's 6.9%) probed the sweep. M1 would not have
     answered them even if someone had written it.

  M3 same problem -- "probe = the query text itself".

  M2 TRANSFERS, IS FREE, PASSES, AND IS NON-DISCRIMINATING. It needs no query
     and no judgment: "the fraction of muse hits that plain `viki ask` on the
     same probe would NOT have returned. Bar > 0.5. Below that, muse is `ask`
     with extra steps." Measured over 40 seeds, probe = the seed chunk's own
     text, seed excluded from both sides:

       muse  (mid-band, k=5)      200 pairs   M2 = 0.995   PASS
       CONTROL: 5 RANDOM chunks   200 pairs   M2 = 1.000   "PASS"

     Random scores HIGHER than muse. The bar cannot separate them, because
     what it actually measures is "did you avoid returning the nearest
     neighbours" -- which a mid-band algorithm satisfies BY CONSTRUCTION
     (it skips the seed's nearest 9 explicitly) and random satisfies by
     accident. Passing M2 is structural, not earned. Do not quote the 0.995
     as evidence muse works.

SO THE GATE IS STILL UNMEASURED, and the missing bar is not in the plan. For a
sweep the question is not "is this different from ask" but "is this a better
PAIR-SELECTION STRATEGY than the trivial one". Two controls, in order of value:

  M4 (the null) -- run QUEUE 44's exact scout protocol, forced bucketing and
     all, over UNIFORMLY RANDOM chunk pairs instead of muse's band. muse
     scored 6.9% genuine / 12.6% near-dup / 80.5% noise over 1500 hits. If
     random scores near 6.9%, the band logic is ceremony and the honest move
     is to say so. If random scores ~1%, muse is doing real work. THIS IS THE
     DECISIVE EXPERIMENT and it is a fraction of the 111-agent sweep's cost,
     because only the scout tier is needed.

  M5 (the alternative) -- same protocol on NEAREST-NEIGHBOUR pairs. Mid-band's
     specific claim is that it beats both nearest (near-duplicates) and far
     (noise). Nobody has ever measured nearest. If nearest yields more genuine
     adjacencies than mid-band, the core thesis is wrong regardless of M4.

Neither needs test/muse-eval.py as C4 specified it. Write M4 first.

### M4 RESULT, measured 2026-08-23: MUSE IS NOT CEREMONY (2.3x random, p<0.01)

DESIGN. Blinded and PAIRED, because an unblinded judge grades to expectation.
48 batches of (1 seed chunk + 5 candidates): 24 where the candidates came from
muse's band, and 24 MATCHED batches using the SAME SEED CHUNK with 5 uniformly
random candidates -- so seed difficulty is controlled. Batches were globally
shuffled, given opaque ids, and the arm labels kept in a manifest the scouts
never saw. 24 Sonnet scouts, 2 batches each, identical prompt, forced bucketing,
told explicitly that "some batches may be near-random" and that marking a whole
batch NOISE is a valid and expected outcome. 240 classifications, 0 errors.

  arm       n     GENUINE        NEAR_DUP       NOISE
  band    120    32 (26.7%)     6 ( 5.0%)     82 (68.3%)
  random  120    14 (11.7%)     5 ( 4.2%)    101 (84.2%)

  ratio 2.3x   two-proportion z = 2.95 (p < 0.01)
  paired by seed, 24 pairs: band better 15, random better 1, tie 8

VERDICT: the band logic does real work. RETRIEVAL_PLAN's "if muse cannot beat
re-asking, muse is ceremony" asked the wrong question, but against the RIGHT
null -- random pair selection -- muse wins clearly and the paired split is 15-1.

THREE CAVEATS, and the second one is the one that will get misquoted.

 1. RANDOM IS NOT ZERO. 11.7% of uniformly random chunk pairs from this repo
    were judged genuine adjacencies. Put any two chunks of a coherent project
    in front of a careful judge and about one in nine yields something
    defensible. So a large share of what reads as muse's value is the JUDGE's
    value. The band's marginal contribution is 15 percentage points, not 26.7.

 2. THE ABSOLUTE RATE IS PROTOCOL-DEPENDENT AND IS NOT COMPARABLE TO QUEUE 44's
    6.9%. There, scouts ran `viki muse` themselves and classified ~15 hits over
    3 seeds; here they were handed pre-extracted pairs with source paths and
    judged 10 items carefully. ONLY the within-experiment band-vs-random
    contrast is meaningful. Do not quote 26.7% as "muse's genuine rate".

 3. It measures GENUINE ADJACENCY, not usefulness. If muse's real job is
    hypothesis generation (QUEUE 46), a random pair can still provoke a good
    question. A 2.3x gap is wide enough to act on; a narrow one would not have
    been.

### WHICH HALF OF THE BAND LOGIC EARNS IT: THE FLOOR, NOT THE SKIP

Mid-band makes two separate claims -- skip the seed's nearest 9 (avoid
near-duplicates) and stay above the corpus-median cosine floor (avoid noise).
M4 separates them:

  near-duplicate   band 5.0%  vs random 4.2%   -- indistinguishable
  noise            band 68.3% vs random 84.2%  -- 16 points apart

The SKIP is unfalsifiable here: random avoids near-duplicates by chance anyway,
so skipping the nearest 9 buys nothing measurable. The FLOOR is where all of
the difference lives. That is directly actionable -- see QUEUE 43.

