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

## 11. sqlite-vec, as a LOCAL DERIVED INDEX only  (user asked 2026-08-16)
Do NOT move the shared cache to vec0. Keep `viki_chunk` exactly as it is -- that is the
artifact D-12 ships between peers and D-11 says is computed once -- and build the vec0
table as a local index rebuilt from it. D-10 compliant by construction, NO epoch bump, no
peer coordination, revertible.
Wins: better-maintained engine; retires the aarch64 SVE2 bug that has linux-arm64 CI
marked experimental; unlocks quantization later to shrink what travels over sync.
VIKI_DESIGN.md already names sqlite-vec as the intended swap, so this is executing a
settled decision, not a new one.

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
