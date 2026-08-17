/*
** viki_muse.h -- `viki muse`: undirected recall for the question you did
** not know to ask.
**
** `viki ask` answers a question. Musing has no question. It picks a SEED
** chunk at random out of the cache and returns chunks whose cosine
** similarity to that seed falls in a calibrated MIDDLE BAND -- close
** enough to be about something, far enough that `viki ask` would never
** have handed it to you. It is aimed at the one episodic-memory failure
** the query path cannot reach by construction: you cannot query for what
** you do not know exists.
**
** ONE MODE. There is no --mode flag and no pluggable scorer. The
** alternatives below were measured and lost; a switch that let a caller
** re-select a losing scorer would be a framework built to preserve
** options the numbers already closed.
**
** WHAT WAS REJECTED, AND WHY. Recorded here rather than in a commit
** message because this project has measured that a decision without its
** rejected alternatives is exactly the class of thing agents cannot
** retrieve later (FINDINGS.md, and the `decision-rationale` query class
** in test/retrieval-queries.tsv).
**
** Every alternative was re-measured on three real caches built with the
** pinned model, every chunk used once as a seed, k=5, against a
** UNIFORM-RANDOM control -- because novelty without a null hypothesis
** measures nothing, and a scorer is only interesting if it is BOTH more
** novel than plain cosine AND more related to the seed than noise.
** ("novel" = fraction of picks NOT in plain cosine's top-5; "related" =
** mean lexical Jaccard of a pick with the seed; repro:
** scratchpad/museH/musealt.c.)
**
**     eval corpus, 114 chunks        novel   related   mean cos
**       plain cosine top-5           0.000    0.1454     0.6341
**       MID-BAND (shipped)           1.000    0.1114     0.4479
**       |cos| absolute               0.000    0.1454     0.6341
**       sum|xi*yi|                   0.270    0.1446     0.6215
**       random 64-dim subspace       0.402    0.1430     0.6081
**       anti-search (lowest cos)     1.000    0.0723     0.1408
**       UNIFORM RANDOM  <- control   0.954    0.1046     0.3871
**
**     repo corpus, 291 chunks / fossil src corpus, 4382 chunks, same shape:
**       MID-BAND      1.000 / 0.0962 / 0.4069   and   1.000 / 0.0715 / 0.3762
**       anti-search   1.000 / 0.0341 / 0.0038   and   1.000 / 0.0386 / -0.0759
**       RANDOM        0.970 / 0.0835 / 0.3141   and   0.998 / 0.0607 / 0.2770
**
**   * |cos| (absolute cosine, "treat opposite as interesting"). Measured
**     an EXACT no-op: identical novelty, relatedness and mean cosine to
**     plain cosine on all three corpora, to four decimals, because it
**     never reorders anything. all-MiniLM-L6-v2's sentence vectors live in
**     a narrow positive cone (min pairwise cosine -0.0233 / -0.1749 /
**     -0.2278; negatives are 0.06% / 1.69% / 1.43% of pairs), and every
**     negative pair is further below the band floor than |cos| can lift
**     it: |cos| moves **0 of 9,647,407 pairs** into the band across the
**     three corpora. Any design premised on antipodal opposites is void in
**     this space -- and note the first measurement of this ("only 4 pairs
**     out of 6441 are negative at all") was a 114-chunk artefact; the
**     claim that survives a bigger corpus is the lift count, not the
**     negative count.
**
**   * Semantic opposition as anti-alignment. Measured false. "the suite
**     has 54 assertions" vs "the suite has 90 assertions" score cos=0.9578
**     as isolated sentences -- the highest non-identity pair measured.
**     Contradictions look like near-duplicates, not like opposites.
**
**   * sum|xi*yi| (per-term absolute similarity). 0.270 / 0.301 / 0.279
**     novel, with relatedness (0.1446 / 0.1507 / 0.1809) statistically
**     indistinguishable from plain cosine's (0.1454 / 0.1548 / 0.1851).
**     It is a slightly noisy cosine: it returns most of what cosine
**     already returns and adds nothing when it deviates. Dominated.
**
**   * Anti-search (LOWEST cosine). Maximally novel and measurably USELESS:
**     novelty 1.000 on all three corpora, but relatedness 0.0723 / 0.0341
**     / 0.0386 against the uniform-random control's 0.1046 / 0.0835 /
**     0.0607 -- i.e. anti-search results are LESS related to the seed than
**     chunks drawn at random, on every corpus tested, and its mean cosine
**     goes negative (-0.0759) on the largest. Surprise is not
**     unrelatedness. This is the failure the band's lower edge exists to
**     prevent.
**
**   * Random 64-dim subspace projection. The one alternative that did NOT
**     lose: 0.402 / 0.421 / 0.517 novel while holding relatedness at
**     0.1430 / 0.1472 / 0.1689 against cosine's 0.1454 / 0.1548 / 0.1851.
**     Novel AND as related as cosine, with novelty rising as the corpus
**     grows. Deliberately NOT shipped this round -- it is a different mode,
**     not a better mid-band, and this round ships one mode done well. It
**     is recorded here rather than deleted so the idea survives; a later
**     `--mode subspace` would start from this measurement. (Correcting an
**     inherited number while we are here: an earlier probe reported 57%
**     novel. At k=5 on these three corpora it measures 40-52%. The verdict
**     is unchanged; the figure was optimistic.)
**
** WHY MID-BAND WORKS HERE, in one paragraph. The corpora are a narrow
** positive cone with a median pairwise cosine of 0.39 / 0.32 / 0.27, so
** "unrelated" does not mean "cosine near zero" -- it means "cosine near
** the median", because the median pairwise cosine is by definition what a
** coin-flip pair scores. Above the band sits what `viki ask` would already
** return; below it sits noise wearing a positive cosine. The band between
** them is where a genuine but non-obvious relationship lives: it is
** disjoint from cosine search by construction (0.000 overlap with plain
** cosine's top-5 on all three corpora, which the rank skip guarantees)
** while staying more related to the seed than random -- paired per-seed
** sign test on lexical overlap, mid-band beats a uniform-random draw on
** 64.0% / 69.8% / 68.6% of seeds (n = 114 / 291 / 4382).
**
** ONE NUMBER THAT DOES NOT SUPPORT THE CLAIM, said here so nobody quotes
** it as if it did. Measured end to end against the REAL hybrid `viki ask`
** rather than against plain cosine -- probe the seed chunk's own text,
** take ask's top-5, count how many muse hits appear in it:
**
**                      overlap with real `viki ask` top-5
**     eval, 114        muse 0.023   uniform random 0.032
**     probe, 334       muse 0.020   uniform random 0.017
**
** Muse is indistinguishable from random on that metric, because ask is
** HYBRID: its top-5 carries BM25 hits that a rank skip over the cosine
** ranking cannot exclude by construction. Low overlap with search is
** therefore necessary but not sufficient, and it is the RELATEDNESS
** column (0.1139 vs 0.1056 at n=114; 0.0964 vs 0.0714 at n=334, sign test
** 69.3% and 85.8% of seeds) that separates musing from noise.
**
** WHAT THE BAND EDGES ARE, AND WHY THEY ARE NOT CONSTANTS. See the long
** comment on viki_muse_band() in viki_muse.c: the originally-proposed
** 0.25-0.55 constants, measured on the corpora this repo actually has,
** select 53-70% of the corpus and produce output statistically
** indistinguishable from returning random chunks. The shipped rule is
** relative on one edge and corpus-calibrated on the other, for reasons
** stated there.
**
** NOT AN EPOCH BUMP (D-11). Musing reads embeddings that already exist and
** writes nothing. It does not chunk, does not tokenize, and never calls
** viki_embed() -- so no content_hash, chunk_params or model_id moves, and
** nothing another peer computes changes.
*/
#ifndef VIKI_MUSE_H
#define VIKI_MUSE_H

#include <sqlite3.h>
#include "embed.h"

#define VIKI_MUSE_MAX_RESULTS 32

/* Excerpt width. 240 is about one terminal line; the cap exists because
** the excerpt is a fixed-size field of viki_muse_result and because a
** muse hit is a POINTER -- it prints the same <content_hash>#<chunk_ix>
** identity `viki ask` prints, so an agent that wants the whole chunk
** fetches it by that identity instead of asking muse to widen. */
#define VIKI_MUSE_MAX_CHARS  1000
#define VIKI_MUSE_EXCERPT_SZ (VIKI_MUSE_MAX_CHARS + 24)

/* Seed-selection bias. Episodic memory's value is recalling what you have
** forgotten, not what you just wrote, so a bias toward OLD and
** rarely-retrieved material is the obviously right thing to want.
**
** It is not the obviously right thing to SHIP, and the reason is a
** measurement, not an opinion: viki has no usable age signal today.
** viki_chunk carries no timestamp column at all, and viki_source.mtime is
** 0 for every virtual artifact by construction (viki_index.c's
** index_text_blob is called with mtime 0 for wiki:/ticket:/forum:, and the
** proposed ckin:/note:/tchg: classes would inherit that) -- 8 of the 18
** sources in the eval corpus have mtime=0. For real files it is the LOCAL
** filesystem mtime, i.e. when this machine last wrote the file, which
** after a fresh clone or a `viki cache pull` (D-11/D-12) is checkout time
** for everything at once.
**
** So VIKI_MUSE_BIAS_OLD is implemented but restricted to sources that
** carry a nonzero mtime, and it says out loud how much of the corpus it
** had to ignore. Sorting mtime=0 first would make "oldest" mean "every
** wiki page, ticket and forum post", which is a bias toward a NAMESPACE
** wearing the costume of a bias toward age.
**
** VIKI_MUSE_BIAS_COLD (rarely retrieved) is deliberately NOT implemented.
** It needs a per-chunk retrieval counter, and the natural place to put one
** -- viki_chunk, in .viki/cache.db -- is wrong: that file travels between
** peers as a `fossil uv` blob under latest-wins (D-12), so a retrieval
** counter written there would sync, and "what I have forgotten" would
** silently become "what the fleet has forgotten". Worse, it would make one
** peer's muse output depend on another peer's query history, destroying
** the reproducibility that --seed exists to provide. If this is ever
** built, the counter belongs in a LOCAL, never-pushed side file. */
typedef enum {
    VIKI_MUSE_BIAS_NONE = 0,   /* uniform over embedded chunks (default) */
    VIKI_MUSE_BIAS_OLD  = 1    /* oldest tertile by viki_source.mtime, mtime>0 only */
} viki_muse_bias;

typedef struct {
    int nResults;              /* how many chunks to surface (default 5) */
    unsigned long long seed;   /* 0 = draw one from the clock; else reproduce exactly */
    viki_muse_bias bias;
    int allowSameSource;       /* 0 (default) = never return a chunk from the seed's
                               ** own source document; see viki_muse.c for the cost */
    int nChars;                /* characters of chunk text to print per hit */
    const char *zSeedHash;     /* non-NULL = use THIS chunk as the seed instead of
                               ** drawing one (with zSeedIx); makes a muse hit
                               ** re-explorable without re-running the whole draw */
    int seedIx;
    const char *zModelId;      /* non-NULL = muse under THIS epoch instead of the
                               ** most-populated one. Exists so `viki muse` need
                               ** not open the ONNX model just to learn its own
                               ** model_id: measured, loading the model costs
                               ** ~72 ms per invocation against ~41 ms for the
                               ** entire rest of the command, and musing never
                               ** calls viki_embed(). The default (most-populated
                               ** model_id) is right for the single-epoch cache
                               ** that is the normal case; this flag is the
                               ** escape hatch for a multi-epoch one. An explicit
                               ** value that matches nothing is an ERROR, not a
                               ** silent fallback -- see VIKI_MUSE_ERR_MODEL. */
} viki_muse_opts;

typedef struct {
    char hash[65];
    int chunk_ix;
    char source[512];
    char excerpt[VIKI_MUSE_EXCERPT_SZ];
    double cos;                /* cosine to the seed chunk */
    int rankInSeed;            /* 1-based rank in the seed's own neighbour list.
                               ** This is the TRUE rank, counted while the
                               ** neighbour list is walked -- deriving it from a
                               ** position in the filtered band was a real bug
                               ** (understated by up to 6 on a measured run),
                               ** because same-source chunks are dropped from the
                               ** band but still occupy ranks. */
} viki_muse_result;

typedef struct {
    /* Everything a caller needs to explain or reproduce a muse run. Printed
    ** by the CLI and intended for a future /api/muse. */
    unsigned long long seed;
    char seedHash[65];
    int seedIx;
    char seedSource[512];
    char seedExcerpt[VIKI_MUSE_EXCERPT_SZ];
    char modelId[128];
    int nCorpus;               /* embedded chunks under modelId */
    int kEffective;            /* results actually asked for after clamping, which
                               ** is what every "too small for N" message must
                               ** quote -- quoting the raw request made `--k 0`
                               ** print "too small for 0". */
    double floorCos;           /* the calibrated lower band edge actually used */
    double floorSampled;       /* the unrelaxed estimate, before any degradation */
    int nSkip, nWindow;        /* the rank-based upper edge actually used */
    int nBand;                 /* how many chunks survived into the band */
    int nBandDocs;             /* distinct documents in the band -- the ceiling on
                               ** how diverse the k results can possibly be */
    int nSameSrc;              /* results that came from the seed's OWN document.
                               ** Counted from the results, never inferred from
                               ** which relaxation rung fired -- inferring it let
                               ** 4 of 114 seeds return same-source hits silently. */
    int nResultDocs;           /* distinct documents actually returned. Reported
                               ** separately because "the band was too narrow to
                               ** be diverse" and "a stratum happened to offer no
                               ** unused document" are different facts and the
                               ** reader can act on only one of them. */
    int degraded;              /* bitmask of VIKI_MUSE_DEGRADED_* below */
    int nAgeUsable, nAgeTotal; /* bias diagnostics */
    int nAgeDistinct;          /* DISTINCT mtimes among the usable ones -- a count
                               ** of nonzero mtimes says nothing if they are all
                               ** the same value; see viki_muse.c */
} viki_muse_info;

#define VIKI_MUSE_DEGRADED_TINY_CORPUS 0x1  /* fewer chunks than the rule wants */
#define VIKI_MUSE_DEGRADED_FLOOR       0x2  /* floor relaxed to fill the band */
#define VIKI_MUSE_DEGRADED_SKIP        0x4  /* skip shrunk to fill the band */
#define VIKI_MUSE_DEGRADED_SAMESOURCE  0x8  /* same-source exclusion abandoned */
#define VIKI_MUSE_DEGRADED_NOBIAS      0x10 /* asked for a bias, had no signal */
#define VIKI_MUSE_DEGRADED_THIN        0x20 /* fewer than nResults exist to return --
                                            ** a fact about the corpus, reported
                                            ** separately from the band edges */
#define VIKI_MUSE_DEGRADED_NOFLOOR     0x40 /* the floor could not be ESTIMATED at
                                            ** all (as against relaxed on purpose);
                                            ** without this flag that case silently
                                            ** looked like a successful run with no
                                            ** lower edge */
#define VIKI_MUSE_DEGRADED_REPEATDOC   0x80 /* the band held fewer than k distinct
                                            ** documents, so a document repeats in
                                            ** the results */

/* Return codes from viki_muse_query, below zero. Distinguishing them
** matters: the first version answered "no embedded chunks in this cache"
** to `--from <hash not in the cache>` against a cache holding 114 embedded
** chunks, which is a false statement about the corpus and sends the reader
** to re-run `viki index` for no reason. */
#define VIKI_MUSE_ERR_NOCORPUS  (-1)  /* nothing embedded under any model_id */
#define VIKI_MUSE_ERR_SEED      (-2)  /* --from names a chunk this cache does not
                                      ** have embedded under this model_id */
#define VIKI_MUSE_ERR_MODEL     (-3)  /* an explicit model_id matched no rows */
#define VIKI_MUSE_ERR_INTERNAL  (-4)  /* allocation or SQL failure */

/* Core. Reads only; writes nothing. Returns the number of results written,
** or one of VIKI_MUSE_ERR_* above, in which case pInfo->modelId/nCorpus
** still describe what it found.
**
** emb may be NULL. Musing is the ONE retrieval mode that needs vectors but
** NOT the model: it never embeds a query, because the "query" is a chunk
** whose embedding is already in the cache. A peer that pulled a cache built
** by a model-having peer (D-11/D-12) can muse over it with no model.onnx on
** disk at all -- so this must not be gated on emb like the rung-2 leg of
** viki_ask is. When emb IS present its model_id is used; when it is not,
** the model_id with the most embedded chunks in the cache is used, because
** a cache in the normal multi-epoch steady state has several and comparing
** vectors across epochs is meaningless.
**
** There is no BM25 fallback and there will not be one: "the middle of the
** similarity distribution" has no keyword analogue, so a cache with no
** embeddings makes musing impossible rather than degraded. That is stated
** as an error, not simulated with a keyword shim. */
int viki_muse_query(sqlite3 *db, const viki_muse_opts *opts, viki_embedder *emb,
                    viki_muse_result *results, int maxResults, viki_muse_info *pInfo);

/* `viki muse` CLI: calls viki_muse_query and prints. Returns 0 on success,
** 1 if there was nothing to muse over. */
int viki_cmd_muse(sqlite3 *db, const viki_muse_opts *opts, viki_embedder *emb);

/* Fills opts with the defaults documented above. */
void viki_muse_defaults(viki_muse_opts *opts);

#endif
