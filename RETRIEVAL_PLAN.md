# RETRIEVAL_PLAN.md -- the measured plan for round 2 (episodic memory)

**Date:** 2026-08-16. **Baseline:** `test/retrieval-eval.sh` against
`build/dist/viki` (2026-08-13 19:48:15), corpus `/tmp/viki-retrieval-eval`,
**corpus fp `944601a216257d69`**, 114 chunks / 18 sources, 59 queries,
43 with an indexed answer, 16 with an answer viki cannot reach, k=10.

Every number in this file that is labelled MEASURED was measured this round by
a survey agent against that corpus and that binary. Every number labelled
TARGET is a bar somebody has to clear, not a result. Do not quote a TARGET as
if it were a finding. If your change misses its bar, **report the miss** -- a
null result reported loudly is a good round; a null result buried is a bad
one.

This file is the brief. It is not a wish list: §5 is a kill list, and things
on it are dead for this round with reasons and numbers.

---

## 1. Where retrieval actually stands

MEASURED, n=43 answerable queries:

```
                     r@1     r@5     r@10    MRR
hybrid (shipped)     0.256   0.535   0.674   0.368
BM25-only control    0.302   0.465   0.558   0.375
held-out, hybrid     0.333   0.750   0.750   0.493   (n=12)
held-out, BM25-only  0.500   0.667   0.667   0.569   (n=12)

INDEX COVERAGE       0 of 16 unindexed-answer queries answerable.  Score 0.000.
```

Failure taxonomy, MEASURED: `NOT_INDEXED 16, HIT@5 12, HIT@1 11,
NEIGHBOUR_CHUNK 9, HIT@K 6, KEYWORD_TOO_DEEP 3, STALE_OUTRANKS 1, FUSED_OUT 1`.

Four facts drive everything below, and each is a measurement, not a view:

1. **The largest failure class is that the content is not there.** 16 of 59
   questions have no chunk containing their answer at any rank. Nine of those
   live only in check-in comments. Coverage is 0.000 and the project owner
   named all-of-fossil-state as a requirement this round.
2. **Adding the vector leg makes rank 1 worse.** Hybrid 0.256 vs BM25-only
   0.302. Fusion pulled 7 answers into the top 10 that BM25 never found, lost
   2 outright, and demoted 8. AGENTS.md's standing evidence for rung 2 was a
   single zero-keyword-overlap query -- the one case where fusion cannot lose
   by construction.
3. **The two legs are indexing different documents.** `chunk_fts` is bound the
   full `chunk_text`; `viki_embed` sees the first 254 WordPiece tokens of a
   40-line chunk. MEASURED: 90.4% of chunks truncate, **58.9% of the corpus is
   never embedded**, and **24 of 43 gold anchors (56%) sit past the window**.
   Proven, not estimated: replacing everything past token 256 in five long
   chunks gives a byte-identical embedding in 5 of 5 cases.
4. **The keyword leg is a ranker, not a filter.** The OR-of-terms MATCH selects
   a median of 114 of 114 chunks. That is fine -- BM25's IDF still ranks
   correctly -- but it means candidate-pool arithmetic, not term selection, is
   what decides the top 10.

---

## 2. The bar for this round

Integration measures these on the combined tree. The four changes below were
each measured **independently**; their sum has never been measured, and
whoever integrates owns that number.

| # | Bar | Baseline | Bar |
|---|---|---|---|
| B1 | `bash test/m1.sh` | 90/0/0 | **90 passed, 0 failed, 0 skipped** (HARD) |
| B2 | Coverage: unindexed-answer queries answerable | 0 of 16 | **>= 14 of 16** |
| B3 | Hybrid MRR over the 43 baseline-answerable queries | 0.368 | **no regression**; TARGET >= 0.45 |
| B4 | Hybrid r@1, same 43 | 0.256 | **no regression**; TARGET >= 0.32 |
| B5 | Held-out (`--split test`) reported alongside every dev number | -- | reported, never tuned on |
| B6 | BM25-only control reported next to every hybrid number | -- | reported (see §4.1) |
| B7 | `viki ask` latency at >= 2000 chunks | 89 ms @ 2310 | **< 150 ms** |

B3/B4 say "no regression" on the *old* 43 deliberately. Coverage work grows
the corpus, and MEASURED, adding correct content costs precision@1 on queries
that already worked: in simulation the same 38 queries went `r@1 0.184 ->
0.132` while overall MRR rose `0.303 -> 0.378`. **Both numbers must be quoted
every time.** A round that raises the average by breaking what worked is not
obviously a good round, and we will only know which it is if both are on the
table.

---

## 3. Ranked changes, by measured gain per unit of risk

| # | Change | Owner file | Measured gain | Cost | Risk | Epoch? | Verdict |
|---|---|---|---|---|---|---|---|
| **1** | Adjacent-bigram phrases in `build_or_query`, gated off when a term contains `_` | `viki_ask.c` | hybrid r@1 **0.256->0.326**, MRR **0.368->0.453**; BM25-only r@1 **0.302->0.395**, MRR **0.375->0.471**; held-out BM25-only r@1 **0.500->0.583**, MRR **0.569->0.677**; wrong-chunk 21->17 | ~40 lines, one function | very low -- query-side only, no reindex, no cache invalidation | **local** | **MUST LAND** |
| **2** | Index check-in comments, tech notes, ticket changes, attachments, unversioned text | `viki_index.c` | coverage **0/16 -> 15/16**; simulated overall MRR **0.303->0.378**, r@5 **0.447->0.527**, r@10 **0.605->0.655**; costs regression-set r@1 **0.184->0.132** | ~400 lines C, 5 extractors + one transport parser | medium -- new subprocess parsing, sweep authority, binary payloads | **local** (no `content_hash` of existing sources moves) | **MUST LAND** |
| **3** | Fix `index_forum`'s false authority claim (sentinel) | `viki_index.c` | correctness: today a repo that never had a forum post **deletes every `forum:` row in the cache** | ~10 lines | very low | local | **MUST LAND** |
| **4** | Drop query terms whose df > N/2 before building the MATCH | `viki_ask.c` + `viki_db.c` | ranking **provably unchanged** (top-40 byte-identical for 42/43, MRR 0.3926->0.3923); **query time 29.2ms -> 14.6ms at 5700 chunks**; snippets stop centring on stopwords | ~30 lines | low, but see §4.2 -- neutrality was measured *without* change 1 | local | **SHOULD LAND, bar-gated** |
| **5** | Fusion calibration: weighted RRF / `VIKI_RRF_K` / pool size / vector-leg gate on identifier queries | `viki_ask.c` | opportunity MEASURED (hybrid loses r@1 by 0.046 today and by 0.069 after change 1; identifier class BM25 16/16 @1 vs fused 15/16 vs vector-alone 8/16); gain **unmeasured** | ~60 lines + a sweep | medium -- easy to overfit 31 dev queries | local | **MEASURE, land only on the bar** |
| **6** | Neighbour-chunk credit at query time | `viki_ask.c` | opportunity MEASURED (21/43 right-doc-wrong-chunk, 9 at rank 1, Q07/Q13 immediately adjacent); gain **unmeasured** | ~50 lines | medium -- can inflate the pool with near-duplicates | **local** -- this is the free half of the chunking problem | **MEASURE, land only on the bar** |
| **7** | `viki muse`: associative recall over working context | `viki_muse.c/.h` (new) | new capability; opportunity MEASURED (9 of 43 rank-1 hits are the wrong chunk of the *right* document; 7 answers found only by the vector leg; vocab-mismatch class dev r@1 **0.000**) | new file, ~350 lines | medium -- unproven idea, so it ships with its own harness or not at all | local | **BUILD + MEASURE** |
| **8** | WordPiece conformance, landed **dark** (default OFF) | `tokenizer.c/.h` | gain on this corpus **~0** (14 divergent words in 37,363; git log 100.00% conformant); value is that the next epoch bump is pre-paid and proven | ~150 lines + a fixture | low **while off**; fleet-wide re-embed if ever switched on | **on = EPOCH BUMP**, off = local | **LAND DARK ONLY** |

Note the ordering claim honestly: **change 1 has the best gain-per-risk, change
2 has the largest absolute gain and is the round's stated requirement.** They
are owned by different agents and run in parallel, so the ordering is about
what must survive triage, not about scheduling.

---

## 4. The three judgement calls, spelled out

### 4.1 Hybrid still loses to BM25-only at rank 1. That is the sharpest open question in the round.

MEASURED today: 0.256 vs 0.302. MEASURED after change 1: **0.326 vs 0.395** --
the gap gets *wider*, not narrower, because the bigram change helps the keyword
leg more than it helps fusion.

Two mechanisms are measured and they point the same way:

- **The pool makes "both legs saw it" nearly free.** With `poolSize = min(4k,40)`
  over 114 chunks each leg nominates 35% of the corpus. Q35 is the arithmetic
  in one line: the gold chunk scores `1/61 = 0.0164` (one leg, rank 1) and
  loses to `1/72 + 1/74 = 0.0274` (two legs, ranks 12 and 14). `VIKI_RRF_K=60`
  sets that exchange rate and has never been tuned.
- **The vector leg is structurally blind.** It sees 41% of each chunk, and 56%
  of gold anchors are in the other 59%. On identifier queries it is a strict
  net loss: BM25 16/16 at rank 1, vector alone 8/16 (`find_w_card`'s chunk at
  rank 83 of 114), fused 15/16.

So down-weighting the vector leg is **not** a hack that contradicts D-11's
rung-2 universality. It is calibration to what that leg can currently see, and
it stops being right the day the chunking epoch bump lands. Say it that way in
any FINDINGS entry.

**"Flip the default to BM25-only" is an admissible recommendation** if the
numbers say so -- but it is a design decision touching `viki.c`/VIKI_DESIGN.md,
so it is **REPORTED, never implemented** (§7).

### 4.2 Stopword dropping is a latency fix. Never sell it as a recall fix.

The intuition that a 114-of-114-chunk MATCH is a recall bug is **measured
false**. `sqlite3.c:245274` clamps IDF to `1e-6` for any term in more than half
the rows, so 255 of 742 query-term occurrences (34%) already contribute
nothing. Dropping all 43 clamped terms leaves the top-40 **byte-identical for
42 of 43 queries**. Lucene's 33-word list costs MRR (0.393 -> 0.385); an
aggressive 190-word list trades tail for head (r@1 +0.047, r@10 -0.116) and
drops Q34 out of the ranking entirely.

The reason to do it anyway is that **change 1 roughly doubles the MATCH term
count** and change 2 grows the corpus, and 29.2ms -> 14.6ms at 5700 chunks is
real. It is the budget for change 1, not a ranking improvement.

### 4.3 Do not bump the epoch this round. Try the free half first.

The chunking number is the biggest in any survey: 58.9% of the corpus is never
embedded, and 10-line chunks would take that to 1.4%. It is still a **NO** for
this round:

- `VIKI_CHUNK_LINES` is in `chunk_params`, which is in the D-11 pin. Changing
  it changes what **every peer** computes: fleet-wide re-embed, `viki-manifest`
  epoch field, `viki_cache.c` push/pull semantics, and every `content_hash`
  in every shared cache. That is a round of its own.
- Nobody has measured that smaller chunks improve **retrieval**. We have
  measured that they improve **coverage of the embedding window**. Those are
  different claims, and the leg being un-blinded is the leg that currently
  loses at rank 1.
- The same failure class has a **local, free** attack that has never been
  tried: neighbour-chunk credit (change 6) and `viki muse` (change 7). 21 of 43
  answerable queries have the answer in a chunk adjacent to or nearby the one
  we already return. Spend the epoch bump *after* you know how much of that 21
  the free fix recovers.

The measurement that would justify the bump is cheap and is assigned in §6.4
as a **measurement-only** deliverable: no `src/` edit, scratch build, report
the number so a future round can bump with evidence instead of adjectives.

---

## 5. Kill list -- do not do these, and here is the number

| Killed | Why (MEASURED) |
|---|---|
| **`trigram` FTS tokenizer** | Best ranking of any tokenizer tried (hybrid r@1 0.302, MRR 0.396, 0/26 imprecise identifier probes) **but**: 6.9x FTS index, +34% query time, +64% cache size, loses stemming, visibly degrades `snippet()`, and the measurement is on 114 chunks while change 2 is about to grow the corpus. Revisit only with a >= 5000-chunk measurement. |
| **Custom colocated FTS5 tokenizer** | Proven to work against viki's own `sqlite3.o`, but identifier retrieval is not a top failure class: the keyword leg is already **r@1 0.857** on the identifier class, and `build_or_query`'s quoting is what makes `find_w_card` exact. Real code for a problem we do not have. |
| **`tokenchars` / `categories`** | Worse in **both** directions: `slack` -> `VIKI_FTS_EPOCH_SLACK` coverage **1.00 -> 0.00**, `fts5` stops matching sentence-final `FTS5.`, identifier-class r@1 **0.857 -> 0.714**, 10 of 26 probes imprecise vs 6. |
| **Removing `porter`** | r@1 **0.302 -> 0.233**, MRR **0.393 -> 0.364**. Keep it. |
| **`bm25()` per-column weighting** | Rebuilt `chunk_fts` with the source path as a second indexed column and swept the weight 0.0 -> 20.0: r@1 and MRR **unchanged to three decimals on both splits at every weight**. |
| **`NEAR` / prefix indexes** | NEAR-based fusion is worse than baseline. Prefix indexes are speed-only and **unreachable**: `manif*` matches 21 chunks raw, `"manif*"` (what viki actually sends) matches **0**. |
| **`fver:` historical file versions** | +1 answerable query for +122 chunks (index more than doubles) and the regression set's r@10 falls **0.605 -> 0.553**. The single worst trade measured this round. |
| **`tag:` / `event type='g'` namespaces** | 120 bytes of prose across 18 valued rows in the whole corpus. Q56 (the one tag query) is **already answered at rank 2** by putting the branch name in the `ckin:` header. |
| **Re-chunking / raising `VIKI_MAX_SEQ_LEN`** | Epoch bump, unmeasured retrieval gain, and the free half is untried. See §4.3. |
| **Live WordPiece conformance changes** | Epoch bump for a measured cost of 14 words in 37,363 on this corpus. Land dark (§6.2). |
| **Implicit-AND FTS queries** | Settled: returned nothing at all. Do not relitigate. |
| **Filtering the keyword leg by `model_id`** | Settled (D-10/D-11): converts a scoring bug into a silent recall bug. Do not relitigate. |
| **Boosting non-file artifacts** | One instance (Q58). n=1 is not a class. |

---

## 6. The four assignments

**No two assignments touch the same file.** If you need a file you do not own,
you **REPORT** the change (§7); you do not make it. This is not politeness, it
is the only reason four agents can work at once without a merge.

| Agent | Owns (src) | Owns (test) |
|---|---|---|
| **A. Ranking** | `src/viki_ask.c`, `src/viki_ask.h`, `src/viki_db.c` | none -- uses the harness as-is |
| **B. Tokenizer** | `src/tokenizer.c`, `src/tokenizer.h` | `test/tokenizer-conformance.sh` (new) |
| **C. Muse** | `src/viki_muse.c`, `src/viki_muse.h` (both new) | `test/muse-eval.py`, `test/muse-eval.sh` (new) |
| **D. Coverage** | `src/viki_index.c` | `test/retrieval-queries.tsv`, `test/retrieval-eval.py` |

Nobody owns `src/viki.c`, `src/viki_serve.c`, `src/viki_cache.c`,
`src/embed.c`, `test/m1.sh`, `AGENTS.md`, `CLAUDE.md`, `FINDINGS.md`. All four
are read-only to all four agents; changes to them are reported to integration.

---

### 6.1 Agent A -- ranking, fusion, FTS schema

**Owns:** `src/viki_ask.c`, `src/viki_ask.h`, `src/viki_db.c`.

**A1. Bigram phrases (MUST LAND).** In `build_or_query()`, keep the current
OR-of-quoted-terms and additionally OR in **every adjacent-word pair as a
quoted phrase** -- `"why did" OR "did we" OR "we not" ...` -- **unless any term
in the query contains `_`**, in which case emit unigrams only.

- MEASURED, real scratch binaries, control binary reproduced `build/dist/viki`
  exactly: hybrid `0.256 -> 0.326` r@1, `0.535 -> 0.628` r@5, MRR
  `0.368 -> 0.453`; BM25-only `0.302 -> 0.395` r@1, MRR `0.375 -> 0.471`;
  held-out BM25-only r@1 `0.500 -> 0.583`, MRR `0.569 -> 0.677`;
  right-doc-wrong-chunk `21 -> 17`. Biggest single moves: Q02 BM25 rank 49->1,
  Q24 31->1, Q22 13->1, Q11 52->11.
- **Report this honestly:** the **held-out hybrid split does not move at all**
  (Q32 gains rank 1, Q26 loses it -- a wash). The held-out gain is in the
  keyword leg. Do not present it as a hybrid win on unseen queries.
- The `_` gate is not decoration: ungated, P2 costs Q36/Q37/Q40/Q59 their
  rank-1 (all 1->2). Gating recovers all four and improves **both** splits.
- **Trap:** `build_or_query`'s `cap = strlen(zQuery)*3 + 16` is sized for
  unigrams only. Bigrams roughly double the output. Resize (or compute the
  bound) or you have a heap overflow, not a ranking change.
- Rejected variants, already measured, do not re-run them: whole query as one
  phrase (no effect), bigrams without unigrams (r@1 0.163), RRF of a separate
  bigram ranking (0.140), RRF with `NEAR(pair,10)` (0.233), df-filtered
  bigrams (all <= P2).

**A2. df-based term dropping (bar-gated).** Drop query terms whose document
frequency exceeds N/2 before building the unigram side of the MATCH.

- **Bigram construction uses the ORIGINAL adjacency, stopwords included.** The
  P2h measurement was made on the raw term sequence; dropping a stopword before
  pairing changes which bigrams exist and is a different, unmeasured, change.
- Two acceptable implementations, your choice, measured:
  (i) exact df via one `SELECT count(*) FROM chunk_fts WHERE chunk_fts MATCH ?`
      per distinct term (corpus-adaptive, matches the measured-neutral rule);
  (ii) a compiled-in list of the 43 measured-clamped function words
      (`a all an and are as at build by c content every file files fix fixed,
      fixes for fossil from in indexer is it its no not nothing of on one only
      run same test that the this to two was which with`).
  If you add an `fts5vocab` shadow table to get df cheaply, that is a
  `viki_db.c` schema addition and you own it -- make it `IF NOT EXISTS` so a
  cache pulled from a peer (D-12) gains it on first open.
- **Never drop every term.** If the filter would empty the query, keep them all.
- **Bar:** with A1 in place, r@1/r@5/r@10 unchanged and MRR within -0.01 on
  **both** splits, **and** a measured latency win on an inflated corpus
  (>= 2000 chunks; the survey's recipe built 5700). Miss either and **do not
  land it** -- report the null.

**A3. Fusion calibration (measure; land only on the bar).** The levers, all
local and free: `VIKI_RRF_K` (never tuned), `poolSize = min(4k,40)`, a per-leg
weight in `leg_hit()`, and a vector-leg down-weight when the query looks like
an identifier (reuse A1's `_` predicate).

- Read §4.1 before you start. The vector leg sees 41% of each chunk; a weight
  below 1.0 is calibration, not defeatism, and it must carry a comment saying
  it becomes wrong when the chunking epoch bump lands.
- **Bar:** must improve **held-out** hybrid MRR, or be reported as a null. A
  sweep that improves dev only is overfitting 31 queries and does not land.
- **Do not** filter the keyword leg by `model_id`, and do not remove
  `leg_hit()`'s once-per-leg guard: both are settled fixes with their own
  FINDINGS entries.

**A4. Neighbour-chunk credit (measure; land only on the bar).** 21 of 43
answerable queries return a different chunk of the right document; 9 of those
are at rank 1; Q07 and Q13 lose to the **immediately adjacent** chunk. Try:
after fusion, for each surviving candidate, consider `chunk_ix +/- 1` of the
same `content_hash` and either promote the better-scoring neighbour or merge
the pair into one hit.

- **Bar:** hybrid r@1 and MRR improve on **both** splits, and the
  right-doc-wrong-chunk count falls. It is easy to make this look good by
  flooding the pool with neighbours; the harness will show you.
- **Boundary with Agent C:** you may do this **inside** `viki_ask_query()`.
  You do not implement `viki muse`, and Agent C does not modify your files.
  If both land, integration re-measures -- C's headline metric is defined
  against the shipped binary and A4 moves it.

**Do not** change `chunk_fts`'s `tokenize=` (§5). If you ever do, you must also
add a schema-version stamp plus `DROP TABLE chunk_fts; CREATE ...; INSERT ...
SELECT FROM viki_chunk` on first open -- because `chunk_fts` travels inside
`viki-cache.db` as a uv blob (D-12) and a peer pulling a cache built under the
old tokenizer would otherwise get a silently stale index.

**Report at the end:** every number above for dev, test and all; BM25-only next
to hybrid every time; and an explicit recommendation on §4.1.

---

### 6.2 Agent B -- WordPiece conformance, landed dark

**Owns:** `src/tokenizer.c`, `src/tokenizer.h`, `test/tokenizer-conformance.sh`
(new). This is the smallest assignment in the round and that is deliberate:
its measured value on this corpus is approximately zero, and its cost if done
wrong is a fleet-wide re-embed.

**B1. Fix the header's false claim (MUST LAND, zero risk).** `tokenizer.h`
says non-ASCII input "degrades to more `[UNK]` tokens rather than being
mis-tokenized silently wrong". That is **measured false**, via a third gap the
header does not mention at all: `is_ascii_punct()` covers only the four ASCII
ranges, so a Unicode `P*` character stays glued to its neighbours, and because
the vocab carries `##-`-style continuations greedy WordPiece **succeeds** with
the wrong pieces instead of failing to `[UNK]`:

```
"decision<EM-DASH>recorded"   viki: decision ##<EM> ##re ##cor ##ded
                              ref : decision   <EM>   recorded
```

Note the contagion -- the entire following word becomes `##` continuations.
One smart quote corrupts two words. 16 of 27 tested characters diverge with
different real ids. Also document: FTS5's `unicode61` strips diacritics by
default while `tokenizer.c` does not, so **viki's two legs disagree with each
other on accented text today** (`cafe` matches in FTS5, `[UNK]`s in the model).

**B2. Land the conformance fixes behind `#ifdef VIKI_TOKENIZER_CONFORMANT`,
default OFF.** Four behaviours, all of which change token ids and are therefore
epoch-bump material:

1. Unicode `P*` punctuation splitting (the silent-wrongness path).
2. NFD accent stripping. **Scope it**: Latin-1 Supplement + Latin Extended-A is
   what the 31.3%-whole-word-`[UNK]` measurement covers. Do not ship a UCD.
3. CJK per-character splitting. MEASURED to be worth less than the header
   implies (the vocab has 244 bare CJK chars, so reference BERT `[UNK]`s most
   of it too) -- it closes a silent-wrongness path, not a recall gap.
4. The reference `max_input_chars_per_word` rule (>100 chars -> `[UNK]`).
   Today viki tokenizes up to 127 chars into as many as 64 pieces and
   **silently drops the remainder of the run** (`tokenizer.c:149`).

**B3. Acceptance bar, and it is a strict one.** With the flag **OFF**, token
ids must be **byte-identical** to today for a fixture covering AGENTS.md +
FINDINGS.md + all of `src/*.c` `*.h` (183 chunks, 107,732 tokens; the current
tree is 183/183 id-identical to reference BERT and 0.000% `[UNK]`). With the
flag **ON**, `test/tokenizer-conformance.sh` must show the 27-character
divergence table resolving to reference ids. A single changed id with the flag
off is a failed assignment.

**B4. State the epoch consequence in the header, in one sentence:** switching
`VIKI_TOKENIZER_CONFORMANT` on changes every embedding every peer computes and
therefore requires a `viki-manifest` epoch bump and a new `model_id`; the flag
alone must never be flipped in a release build.

**Do not** touch `src/embed.c`. `VIKI_MAX_SEQ_LEN` is 256 (not 512 -- the
task's premise was wrong, and 256 is all-MiniLM-L6-v2's own
`sentence_bert_config.json` default, so it is conformant). Raising it is an
epoch bump and is REPORTED, not done. Note in your report that
`MAX_BASIC_TOKENS 512` is dead code while `VIKI_MAX_SEQ_LEN` is 256 (verified:
a 4000-word input gives byte-identical output from a 200000-cap build).

**Report:** the divergence rates, the fixture result both ways, and an explicit
recommendation that B2 be switched on **in the same epoch bump as chunking**,
never in one of its own.

---

### 6.3 Agent C -- `viki muse`, associative recall

**Owns:** `src/viki_muse.c`, `src/viki_muse.h` (both new), `test/muse-eval.py`,
`test/muse-eval.sh` (both new).

**The problem this exists for.** `viki ask` answers a question. Episodic memory
also has to surface **the thing you do not know exists, so cannot query for**.
The measured openings:

- **9 of 43** queries return, at rank 1, the wrong chunk of the **right**
  document (2 of them immediately adjacent). Those are one association away.
- **7 answers** (Q04 Q18 Q20 Q22 Q25 Q28 Q33) were found **only** by the vector
  leg -- and fusion then averages that contribution against BM25, demoting 8
  others. Muse is the command that does not average it away.
- The **vocab-mismatch class scores dev r@1 = 0.000, r@5 = 0.000, MRR = 0.062**.
  That class is the entire thesis.

**C1. The command.** `viki muse "<context text>" [--k N]`, plus `--from <file|->`.
The input is *working context*, not a question: a task description, a file
you are editing, the last thing you read.

**C2. The algorithm (v1, no schema changes).**

1. Split the input into probes (whole text if short; else blank-line
   paragraphs, capped at ~8).
2. Per probe: embed it (`viki_embed`, `embed.h` is public) and run a cosine
   top-P over `viki_chunk` filtered by `model_id`; separately run the probe's
   rarest terms through `chunk_fts` to compute what plain retrieval would have
   returned anyway.
3. **Novelty re-rank.** Score = cosine, penalised by lexical overlap with the
   probe. A chunk the keyword leg would have found is not a musing; a chunk
   that is semantically near and lexically far is exactly the vocab-mismatch
   class we score 0.000 on.
4. **Source diversity.** At most one hit per `viki_source.path`. Muse must not
   return five chunks of FINDINGS.md.
5. **Neighbour expansion.** For each surviving hit, also score `chunk_ix +/- 1`
   of the same `content_hash` and keep the better. This is the 9-of-43 floor.
6. **Degraded mode.** With no model, print the same honest notice `viki ask`
   prints and fall back to rarest-term FTS plus neighbour expansion. Do not
   pretend, and do not fail.

**C3. Temporal binding, and the contract it depends on.** Agent D's `ckin:`
sources carry a frozen header line
`check-in <uuid16> on <ISO-8601> by <user> branch <branch>` as the first line
of the composed text. You may parse that prefix out of `chunk_text` to answer
"what else happened around this" and to label hits with a date. **This is a
cross-agent contract**: the header format is frozen when D lands it (§7).
**Do not** read `viki_source.mtime` for this -- see §7's trap.

**C4. Measurement, and muse does not land without it.** Write
`test/muse-eval.py`. Three metrics, all against the frozen corpus copy:

- **M1, one-hop recovery.** For each of the 43 answerable queries: run
  `viki ask "<query>" --k 1`, take the rank-1 chunk's text as the muse probe,
  and check whether the **gold** chunk appears in muse's top 5. The floor is
  the 9 same-document cases; the other 23 (rank 1 in a different document
  entirely) need real cross-document association.
  **Baseline you must beat: `viki ask "<rank-1 chunk text>" --k 5`** -- i.e.
  re-asking with the hit text. If muse cannot beat re-asking, muse is
  ceremony. **Bar: TARGET M1 >= 0.30 and >= +0.10 over the re-ask baseline.**
- **M2, non-vacuity.** The fraction of muse hits that plain `viki ask` on the
  same probe would **not** have returned. **Bar: TARGET > 0.5.** Below that,
  muse is `ask` with extra steps -- say so and do not wire it up. This is the
  same discipline as the retrieval harness's degraded-mode shim.
- **M3, the thesis.** On the vocab-mismatch class (4 dev + 1 held out), probe =
  the query text itself. Does muse beat `viki ask` (dev r@1 0.000, r@5 0.000,
  MRR 0.062)? Tune on the 4; report the held-out one separately and only at the
  end. Report regardless of outcome -- a null here is the most interesting
  result available to you, because that class is the reason muse exists.

**C5. Hard rules.** Muse implements its own SQL against `viki_chunk`,
`chunk_fts` and `viki_source`. It may `#include "viki_ask.h"` and call
`viki_ask_query()` for the M1 baseline, but it **must not require any change to
`viki_ask.h` or `viki_ask.c`** -- if you think you need one, that is a REPORT,
and Agent A is not obliged to take it. No new tables: muse is stateless this
round. If you want persistence (a musing journal, a seen-set), report it as a
`viki_db.c` schema request.

**C6. Testing your own command.** `viki muse` needs dispatch in `src/viki.c`,
which you do not own. Build a scratch binary with a **local patched copy** of
`viki.c` in your scratch `$D/src` (§8.1) and report the exact dispatch + usage
diff for integration (§7).

---

### 6.4 Agent D -- artifact coverage: all of fossil state

**Owns:** `src/viki_index.c`, `test/retrieval-queries.tsv`,
`test/retrieval-eval.py`.

This is the round's stated requirement and its largest coverage number:
**0 of 16 -> 15 of 16**, MEASURED in simulation. Nine of those sixteen live
only in check-in comments. A memory that ranks perfectly over a corpus missing
the commit log is still a bad memory.

**D0. Read the transport section first.** On an **encrypted** repo every
`fossil` subprocess costs **~470 ms of SQLCipher key derivation**, independent
of repo size, 71x the plaintext cost. MEASURED: one `viki index` on a 4-file
checkout is 1.51 s wall of which **0.010 s is viki**. The existing "list ids,
then fetch each artifact separately" idiom is therefore `O(N) x 0.47 s` and
**must not be reused** for check-ins: 2,001 check-ins would take ~16 minutes.

Use **one call per class with counted framing**:

```sql
SELECT <key> || ' ' || length(cast(<payload> AS BLOB)) || ' ' || <free header text>
    || char(10) || <payload> ...;
SELECT '#viki-eof';
```

Parse: read to `\n`; token 1 = key; token 2 = decimal byte count; **the rest of
the line is opaque free text** so a branch name or username can never
desynchronise the parse; read exactly n bytes; consume one `\n`; repeat until a
header line equal to `#viki-eof`. Verified byte-exact on real manifests and on
a UTF-8 commit message. Extracting all 2,001 check-in comments (3.26 MB framed)
took **25 ms**.

**Two traps that will bite:**

- **`length()` on TEXT counts CHARACTERS.** One accented commit message
  measures `length()=55`, `length(cast(X AS BLOB))=70`. Cast to BLOB or the
  framing desynchronises on the first non-ASCII artifact.
- **`fossil sql` exits 0 when the QUERY fails**; it exits 1 only when the
  repository cannot be opened. So exit status is not authority. A failing
  statement aborts the rest of the script, which is why the trailing
  `SELECT '#viki-eof';` is emitted **iff** the real query prepared and ran.
  **Authority rule for every extractor: exit 0 AND last line == `#viki-eof`.**

**D1. Fix `index_forum()`'s false authority claim (MUST LAND).** Its comment
claims a repo with no `forumpost` table makes the query fail, reading as "not
authoritative". It does not: `rc == 0`, `authoritative = 1`, and
`sweep_sources()` **deletes every `forum:` row in the cache**. Reproduced live
on a repo that never had a forum post. This is a real latent bug in shipped
code. Fix it with the sentinel, and fix the comment.

**D2. The five classes, in priority order.** Full SQL, header formats,
supersession rules and parsing notes are in the coverage survey; the decisions
that matter:

| Class | Key | Supersession rule | Note |
|---|---|---|---|
| `ckin:<uuid>` | check-in uuid | `coalesce(ecomment, comment)` -- Fossil's own timeline rule | **rank 1**. Amend updates in place under a stable key, so re-hash + `gc_orphan_chunks` handles it exactly like an edited file. Header carries ISO-8601 time, user and **branch** -- the branch is load-bearing, it is what answers Q56 and is why no `tag:` class is needed. Filter `<> ''`: `index_text_blob()` returns early on `len == 0` **without** calling `mark_seen()`, so an empty comment becomes a phantom sweep candidate every run. |
| `note:<technote-id>` | `substr(tagname,7)` | `event.type='e'` alone -- Fossil DELETEs the superseded row | This is `index_forum()` with three substitutions: type `f`->`e`, card `H`->`C`, prefix `forum:`->`note:`. Reuse `find_w_card()`, `find_line_card()` bounded at the W body, and **`unquote_fossil()`** -- skipping the unquote reproduces forum bug #1 verbatim. |
| `tchg:<artifact-uuid>` | change artifact uuid | none -- immutable by design, which is the point | `ticketchng.icomment` is **NULL in every row** via the CLI path; the text is in the artifact's `J` cards. Split each `J ` line once on the first space, `unquote_fossil()` the value, keep a leading `+` (append) as the episodic signal. |
| `attach:<src-uuid>` | content blob uuid | `isLatest AND src IS NOT NULL AND src <> ''` | The `src <> ''` deletion filter is **read from Fossil's `attach.c:648`, not measured** -- there is no `fossil attachment rm`. Say so in the comment. Gate on `looks_binary()`. |
| `uv:<name>` | uv name | latest-wins by construction | **Two calls, and it must be two**: `unversioned.content` is zlib-compressed with a 4-byte length prefix when `encoding=1`, so SQL returns garbage; `fossil unversioned cat` is the only path that does not need zlib. **Mandatory exclusions: `viki-cache.db` and `viki-model/*`** -- viki's own blobs; `vocab.txt` alone would chunk into ~760 chunks of pure token noise. `looks_binary()` gate. |

**D3. Plumbing you must change (all inside your file).**

- `run_capture()` needs an out-length (`size_t *pnOut`). It currently returns a
  NUL-terminated buffer with no length, and attachment/uv payloads contain NUL
  bytes -- `strlen()` would silently truncate them. With a length,
  `looks_binary(buf, len)` gates them exactly as `index_file()` gates files.
- `VIRTUAL_PREFIX[]` (line 38) **and** the `strncmp` chain in `sweep_sources()`
  (line 885) must learn every new prefix. Leave the
  `else if( is_virtual_path(zPath) ) drop = 0;` fallback: it is the correct
  safety net if only one of the two gets updated.
- **Free win while you are there:** convert `index_forum()` to the same
  one-call form. 1+N invocations -> 1, saving N x 0.47 s, and the extracted
  text is byte-identical (the W-card body is a counted string; only the
  transport changes), so **no `content_hash` moves and no shared cache is
  invalidated**. Gate: re-run `sh build/forum-e2e-probe.sh <empty-dir>` and
  report `PASS=26 FAIL=0`.

**D4. The trap that will cost you a day if you skip it.** Six of the nine
`checkin-comment` anchors in `test/retrieval-queries.tsv` (**Q41 Q42 Q43 Q46
Q48 Q49**) **span a hard line break** in the comment, and the harness resolves
ground truth with a verbatim `instr()`. They will resolve to nothing after your
extractor lands and will read, in the report, as an unchanged coverage gap.
MEASURED: verbatim anchors give 3/9 resolvable and r@1 0.333; whitespace-
normalised give 9/9, r@1 0.444, MRR 0.500.

**Fix the six anchors, not the resolver.** Rewrite each to a distinctive
substring that sits on one line of the comment. Changing `instr()` to a
normalising match would silently change what every number ever measured by this
harness meant, and the harness's QUERY-SET DEFECT flag exists precisely to
catch this class. Do not renumber any query id -- Q01..Q59 are cited by
baselines.

**D5. `retrieval-eval.py`, minimal edits only.** The corpus fingerprint
excludes `ticket:` and `forum:` rows because their uuids are timestamp-derived;
your `ckin:`, `tchg:` and `attach:` keys are too, so **add them to the same
exclusion list** or the fingerprint stops being comparable across rebuilds.
Update the NOT_INDEXED explanation table so it stops claiming viki reads only
four classes. **Do not change how any query is scored** -- Agents A and C are
reading this file's output while you edit it.

**D6. Measurement-only, no `src/` edit: the chunking number (do this last).**
§4.3 kills the epoch bump for lack of evidence. Produce the evidence for a
future round: build a scratch binary with `VIKI_CHUNK_LINES` at 40, 20 and 10,
index the frozen corpus copy with each, and report r@1 / r@5 / r@10 / MRR on
both splits plus chunk count and index time. MEASURED already, so you do not
need to re-derive it: 40 lines -> 58.9% of the corpus never embedded, 20 ->
22.9%, **10 -> 1.4%**, and 10 lines is the knee. What is missing is whether
that converts into retrieval. Report it as a recommendation with numbers, in
`FINDINGS.md` form, and state plainly that acting on it is an EPOCH BUMP
(`chunk_params` is in the D-11 pin).

**D7. Report both numbers, always.** Coverage `0/16 -> N/16` **and** the
regression-set r@1 on the same 43 queries the baseline used. In simulation the
regression set fell `0.184 -> 0.132` while overall MRR rose `0.303 -> 0.378`.
Anyone quoting only the second number is misreporting.

**Also worth saying in your report, because it corrects the round's premise:**
check-in comments are **not** denser in decision language than the docs
(0.48 hits/KB vs 0.59) and only ~5% of their long tokens are novel. The case
for them is **coverage and recency-binding**, not prose quality: 0 of 16
un-indexed anchors appear verbatim anywhere in the files viki already indexes.
The statements are unique even though the words are not.

---

## 7. Wiring reports -- REPORT, do not implement

`src/viki.c` (dispatch, usage), `src/viki_serve.c`, `src/viki_cache.c`,
`src/embed.c`, `test/m1.sh`, `AGENTS.md`, `CLAUDE.md` and `FINDINGS.md` belong
to the integration agent. Each agent returns these as exact, applyable text.

| Agent | Must report |
|---|---|
| A | Any new env knob (e.g. `VIKI_RRF_K`, a vector-leg weight) for `usage()`; a recommendation on §4.1 including, if the numbers say so, "flip the default to BM25-only" -- which is a VIKI_DESIGN.md change and is **never** implemented here. |
| B | The `VIKI_TOKENIZER_CONFORMANT` flag: that it is OFF, what turning it on costs (epoch bump + new `model_id` + `viki-manifest`), and a request to raise `VIKI_MAX_SEQ_LEN` **only** as part of that same bump. |
| C | The exact `src/viki.c` dispatch block and `usage()` line for `viki muse`; optionally a `/api/muse` sketch for `viki_serve.c`. Both as diffs, not as prose. |
| D | New `viki index` summary lines (check `test/m1.sh` does not assert on the old wording), the **frozen** `ckin:` header format (Agent C parses it), and anything you concluded needs a `viki_db.c` schema change -- Agent A owns that file. |

**The `content_hash` contract nobody wrote down, and D must state:** because
`content_hash = sha256(the composed extracted text)`, the **composition recipe**
for a class is de facto part of the D-11 sharing contract even though
`viki-manifest` says nothing about it. Two peers on different viki versions
that compose a check-in header differently produce different hashes for the
same check-in, both embed it, and both appear in `viki ask`. That is cache
fragmentation, not corruption, and it is **not** an epoch bump -- but the
header format must be frozen when it lands, and any later change to it called
out as cache-fragmenting. It probably deserves a `viki-manifest` field; report
that, do not add it.

**Trap, so nobody rediscovers it the hard way:** do **not** put an artifact's
real timestamp in `viki_source.mtime` for virtual sources. `index_text_blob()`
takes the fast-skip path when `mtime != 0` and the stored mtime matches -- and
a `fossil amend` changes `ecomment` while leaving the check-in's time alone, so
the skip would permanently serve the pre-amend text. Time belongs in the
composed header text, where it is both FTS-indexed and embedded.

---

## 8. Operating rules

### 8.1 Building -- nobody runs `build/build.sh`

`build/obj` is shared state and concurrent rebuilds corrupt each other. Every
agent builds privately with the recipe FINDINGS.md already documents, which
never touches `build/obj` or `build/dist`:

```sh
D=$SCRATCH/build; mkdir -p $D/src $D/obj $D/dist
cp src/*.c src/*.h $D/src/                      # then edit ONLY your files in $D/src
cp build/obj/sqlite3.o build/obj/sqlite-ndvss.o $D/obj/
for f in build/dist/libonnxruntime*.dylib; do ln -sf "$PWD/$f" $D/dist/$(basename $f); done
for f in viki sha256 viki_db viki_index viki_ask viki_cache viki_serve tokenizer embed; do
  cc -O2 -g -Wall -Wno-unused-parameter \
     -Ivendor/download-cache/sqlite-amalgamation-3530400 \
     -Ivendor/download-cache/onnxruntime-Darwin-arm64-1.29.0/include \
     -I$D/src -c $D/src/$f.c -o $D/obj/$f.o
done
cc -O2 -o $D/dist/viki $D/obj/*.o -L$D/dist -lonnxruntime \
   -Wl,-rpath,@executable_path -lm -lpthread
```

Agent C adds `viki_muse` to both loops. Every agent must first build an
**unpatched control** and confirm it reproduces the shipped baseline
(`0.256 / 0.535 / 0.674 / 0.368`); a survey did this and the control was exact.
A control that does not reproduce means your recipe is wrong, and every delta
you measure afterwards is noise.

Integration performs the one real `build/build.sh`.

### 8.2 The corpus is frozen. Do not rebuild it.

The corpus is built from **this repo's own docs**, so editing `FINDINGS.md`,
`AGENTS.md` or `CLAUDE.md` **moves the baseline**. This round will edit all
three. Therefore:

- **Nobody runs `test/retrieval-corpus.sh --rebuild` or deletes
  `/tmp/viki-retrieval-eval`.** That directory is the frozen reference at
  `corpus fp 944601a216257d69`.
- Each agent works on a **copy**: `cp -a /tmp/viki-retrieval-eval $SCRATCH/eval`,
  then `bash test/retrieval-eval.sh --no-build --corpus $SCRATCH/eval --viki <bin>`.
  `--no-build` is what stops the harness from rebuilding a missing corpus under
  you.
- Agents A, B and C do not need to re-index: the harness scores an existing
  `cache.db` and only varies the binary that runs `viki ask`. **Vary
  `VIKI_BIN`, keep the corpus.**
- Agent D **must** re-index (they change indexing). In the same copy, and
  sequentially: `rm -rf co/.viki`, index with the control binary, score; then
  `rm -rf co/.viki`, index with the new binary, score. Same directory, same
  repo, same uuids -- which is the only way `recall@k` and MRR stay comparable
  (they move ~0.02 across a genuine corpus rebuild, because ticket and forum
  uuids are timestamp-derived and are part of the indexed text).
  Env for a manual re-index, from `retrieval-corpus.sh`:
  `FOSSIL_HOME=<copy>/fossilhome`, `FOSSIL_SEE_KEY=viki-retrieval-eval-key-7d3a`,
  `FOSSIL_USER=vikieval`, `VIKI_FOSSIL_USER=vikieval`,
  `VIKI_FOSSIL_BIN=vendor/fossil-see/build/dist/fossil-see`,
  `VIKI_MODEL_DIR=build/dist/model`.
  Note the copied checkout's `_FOSSIL_` still names the original `co.efossil`
  by absolute path; that is fine (reads only), but do not write to it.
- `RETRIEVAL_PLAN.md` is safe to add: `retrieval-corpus.sh` commits a **named**
  list of docs and this file is not on it.

### 8.3 Measuring

- Quote `corpus fp`, chunk count, binary mtime and query-set path with every
  number. A number quoted against a different fingerprint is a different
  experiment.
- Tune with `--split dev` (41 queries; it reports `indexed_test n=0`, so you
  literally cannot see held-out numbers). Report `--split all` **and**
  `--split test` at the end. Looking at held-out during tuning burns it.
- `--failures` for the taxonomy, `--json out.json` for machine-readable.
- The harness gates nothing: exit 0 means it produced numbers, not good ones.
- **Report null results prominently.** "I tried X, here are the before/after
  numbers, it did not help" is a good outcome and belongs at the **top** of
  your report, not in a footnote.

### 8.4 Finishing

- Run `bash test/m1.sh` against your private binary and report the line. **90
  passed, 0 failed, 0 skipped** is the Milestone 1 claim and is not negotiable.
- Agent D additionally runs `sh build/forum-e2e-probe.sh <empty-dir>` and
  reports `PASS=26 FAIL=0`.
- **Do not run `git commit`, `git push` or `git stash`.**
- **Do not edit `FINDINGS.md` directly** -- four parallel writers appending at
  the top will collide. Write your entries to
  `$SCRATCH/findings-<agent>.md` in FINDINGS form (`## <one-line claim>`,
  `**Date:**`, the repro, the wrong assumption it replaces) and return them in
  your final report. Integration merges them newest-first.
- Leave your repro artifacts in the scratchpad and name the paths in your
  report. Prior rounds' artifacts are still there and are worth reading before
  you re-derive anything: `scratchpad/kwprobe_ktc/` (FTS variants, bigram
  binaries, latency benches), `scratchpad/tok/` (reference BERT, conformance
  probes), `scratchpad/audit/` (fossil extraction probes, `mkprobe.sh`,
  `census.sh`, `sim.sh`).

---

## 9. What this round is claiming, in one paragraph

viki's retrieval today ranks a corpus that is missing the record of what was
actually done. The round closes that first (Agent D: coverage 0/16 -> 15/16),
takes the one measured free ranking win (Agent A: bigram phrases, MRR
0.368 -> 0.453), builds the command that surfaces what you did not know to ask
for and proves it beats re-asking or reports that it does not (Agent C), and
pre-pays the tokenizer conformance debt without spending an epoch bump on it
(Agent B). It deliberately does **not** re-chunk, does **not** adopt trigram,
and does **not** touch a single token id -- three changes that are individually
defensible and collectively an epoch bump we have not yet earned the evidence
for. The evidence is itself a deliverable (§6.4 D6), so the round after this
one can make that call with numbers.
