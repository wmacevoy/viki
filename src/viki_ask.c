#include "viki_ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIKI_RRF_K 60.0      /* standard RRF damping constant */

/* How many FTS rows to fetch per candidate slot. LIMIT counts ROWS, but the
** candidate pool wants distinct chunks, and a cache holding E model epochs of
** the same content hands BM25 up to E rows for one chunk (see leg_hit below).
** Over-fetching by this factor and stopping once poolSize DISTINCT chunks are
** in hand keeps a bounded LIMIT -- which is what lets SQLite's sorter discard
** rows as it goes, since ORDER BY bm25() has to score and sort the whole MATCH
** set either way. A cache with more than this many epochs of one chunk simply
** yields fewer candidates than requested; it never yields wrong scores. */
#define VIKI_FTS_EPOCH_SLACK 4

/* FTS5's default MATCH syntax treats space-separated barewords as an
** IMPLICIT AND -- every term must appear, or the row doesn't match at
** all (not even a low score; excluded outright). That's wrong for a
** natural-language "ask viki" query: real queries contain stopwords and
** phrasing that won't literally appear in a matching chunk ("horses at
** the water trough" fails to match text that says "horses...near the
** water trough" purely because "at" isn't present). Discovered empirically
** while smoke-testing -- see FINDINGS.md.
**
** Fix: build an explicit OR-of-terms query. bm25() then ranks rows that
** match MORE/rarer terms higher, same as any standard BM25 search (Lucene/
** Elasticsearch default to this "should match any, score by how many"
** shape for exactly this reason) -- rather than requiring all terms.
** Each term is double-quoted as an FTS5 string literal (embedded quotes
** doubled) so punctuation in the raw query can't be misparsed as FTS5
** query syntax (NEAR, column filters, etc.). */
static char *build_or_query(const char *zQuery){
    size_t cap = strlen(zQuery) * 3 + 16; /* generous: worst case every char quoted */
    char *out = malloc(cap);
    size_t pos = 0;
    const char *p = zQuery;
    int first = 1;

    if( !out ) return NULL;
    out[0] = '\0';

    while( *p ){
        const char *start;
        size_t termlen;
        while( *p && (unsigned char)*p <= ' ' ) p++;
        if( !*p ) break;
        start = p;
        while( *p && (unsigned char)*p > ' ' ) p++;
        termlen = (size_t)(p - start);

        if( !first ){
            memcpy(out + pos, " OR ", 4);
            pos += 4;
        }
        first = 0;

        out[pos++] = '"';
        for( size_t i = 0; i < termlen; i++ ){
            if( start[i] == '"' ) out[pos++] = '"'; /* double embedded quotes */
            out[pos++] = start[i];
        }
        out[pos++] = '"';
        out[pos] = '\0';
    }

    if( first ){
        free(out);
        return NULL;
    }
    return out;
}

static viki_ask_result *find_or_add(viki_ask_result *pool, int *n, const char *hash, int chunk_ix){
    int i;
    for( i = 0; i < *n; i++ ){
        if( pool[i].chunk_ix == chunk_ix && strcmp(pool[i].hash, hash) == 0 ) return &pool[i];
    }
    if( *n >= VIKI_CANDIDATE_POOL ) return NULL;
    memset(&pool[*n], 0, sizeof(viki_ask_result));
    strncpy(pool[*n].hash, hash, 64);
    pool[*n].chunk_ix = chunk_ix;
    (*n)++;
    return &pool[*n - 1];
}

/* Records one hit from retrieval leg `leg` and advances *pRank to the rank
** this hit occupies within that leg.
**
** Ranks count DISTINCT (content_hash, chunk_ix) chunks, not rows: a chunk this
** leg has already scored neither scores again nor consumes a rank. That is the
** fix for a real double-counting bug. chunk_fts holds one row per
** (content_hash, model_id, chunk_ix), so re-indexing the same content under a
** second model_id -- the NORMAL steady state of D-11 cache sharing, where a
** no-model peer writes model_id='none' rows and a model-having peer writes its
** own model_id's rows into the same latest-wins uv blob -- puts two rows for
** one chunk in front of BM25. find_or_add() merges them into one candidate,
** correctly (chunk_text does not vary by model; only the embedding does), and
** the old code then added the BM25 leg's 1/(k+rank) contribution once per row.
** Measured on a two-epoch cache: rrf=0.0489 = 1/61 + 1/62 + 1/61 for a hit a
** single-epoch cache scores 0.0328 = 1/61 + 1/62 -- the keyword leg counted
** twice, and every score shifted by an amount that depends on how many epochs
** a peer happened to push rather than on the query.
**
** WHY dedupe here rather than adding "WHERE model_id = <asker's model>" to the
** FTS query: the asker's model_id is not a legal filter for the keyword leg.
** A chunk may exist ONLY under model_id='none' (indexed by a peer with no
** model), and `viki ask` with no model at all -- where there is no model_id to
** filter by in the first place -- must still search a cache that a model-having
** peer built, which is the entire point of `viki cache pull` (D-11/D-12).
** Filtering would drop exactly the rows the pull was for, converting a scoring
** bug into a silent recall bug. BM25 also does not depend on model_id at all:
** the only indexed column is chunk_text, identical across epochs, so the extra
** rows carry zero extra information and collapsing them loses nothing. The
** vector leg is the opposite case -- embeddings from two models are not
** comparable, so run_vector MUST filter by model_id, and does; viki_chunk's
** primary key then already makes its rows unique per (content_hash, chunk_ix).
** It still goes through this guard so "each leg contributes at most once" is
** enforced by the merge code itself rather than by trusting each leg's SQL.
**
** Both legs feed rows in their own best-first order (ORDER BY bm25() /
** cosine DESC), so the first row a leg reports for a chunk is also that
** chunk's best rank in that leg -- which is the one that gets scored. */
static void leg_hit(viki_ask_result *pool, int *n, unsigned leg, int *pRank,
                     const char *hash, int chunk_ix, const char *text){
    viki_ask_result *c = find_or_add(pool, n, hash, chunk_ix);
    if( c && (c->legs & leg) ) return;   /* duplicate row for an already-scored chunk */
    (*pRank)++;
    /* c==NULL means the chunk is new and the candidate pool is full. The rank
    ** is still consumed: rank is this leg's position in ITS result list, and
    ** must not depend on how much room the pool had left. */
    if( !c ) return;
    c->legs |= leg;
    c->rrf += 1.0 / (VIKI_RRF_K + *pRank);
    if( text && !c->snippet[0] ) strncpy(c->snippet, text, sizeof(c->snippet) - 1);
}

static int cmp_result(const void *a, const void *b){
    double da = ((const viki_ask_result*)a)->rrf, db = ((const viki_ask_result*)b)->rrf;
    if( da > db ) return -1;
    if( da < db ) return 1;
    return 0;
}

static void run_fts(sqlite3 *db, const char *ftsQuery, int poolSize, viki_ask_result *pool, int *n){
    sqlite3_stmt *st;
    int rank = 0;

    if( sqlite3_prepare_v2(db,
            "SELECT content_hash, chunk_ix, snippet(chunk_fts, 0, '[', ']', ' ... ', 24) "
            "FROM chunk_fts WHERE chunk_fts MATCH ?1 ORDER BY bm25(chunk_fts) LIMIT ?2",
            -1, &st, NULL) != SQLITE_OK ){
        return;
    }
    sqlite3_bind_text(st, 1, ftsQuery, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, poolSize * VIKI_FTS_EPOCH_SLACK);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *snippet = (const char*)sqlite3_column_text(st, 2);
        leg_hit(pool, n, VIKI_LEG_FTS, &rank, hash, chunk_ix, snippet);
        if( rank >= poolSize ) break; /* poolSize distinct chunks, not rows */
    }
    sqlite3_finalize(st);
}

static void run_vector(sqlite3 *db, const float *qvec, int dim, const char *modelId,
                        int poolSize, viki_ask_result *pool, int *n){
    sqlite3_stmt *st;
    int rank = 0;

    if( sqlite3_prepare_v2(db,
            "SELECT content_hash, chunk_ix, substr(chunk_text,1,140) "
            "FROM viki_chunk WHERE model_id=?1 AND embedding IS NOT NULL "
            "ORDER BY ndvss_cosine_similarity_f(?2, embedding, ?3) DESC LIMIT ?4",
            -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki ask: vector query prepare failed: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_text(st, 1, modelId, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 2, qvec, (int)(sizeof(float) * (size_t)dim), SQLITE_STATIC);
    sqlite3_bind_int(st, 3, dim);
    sqlite3_bind_int(st, 4, poolSize);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *excerpt = (const char*)sqlite3_column_text(st, 2);
        leg_hit(pool, n, VIKI_LEG_VEC, &rank, hash, chunk_ix, excerpt);
    }
    sqlite3_finalize(st);
}

static void fill_sources(sqlite3 *db, viki_ask_result *pool, int n){
    int i;
    for( i = 0; i < n; i++ ){
        sqlite3_stmt *stPath;
        strcpy(pool[i].source, "(source path unknown)");
        if( sqlite3_prepare_v2(db, "SELECT path FROM viki_source WHERE content_hash=?1 LIMIT 1",
                -1, &stPath, NULL) == SQLITE_OK ){
            sqlite3_bind_text(stPath, 1, pool[i].hash, -1, SQLITE_STATIC);
            if( sqlite3_step(stPath) == SQLITE_ROW ){
                const char *path = (const char*)sqlite3_column_text(stPath, 0);
                if( path ) strncpy(pool[i].source, path, sizeof(pool[i].source) - 1);
            }
            sqlite3_finalize(stPath);
        }
    }
}

int viki_ask_query(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb,
                    viki_ask_result *results, int maxResults){
    char *ftsQuery;
    viki_ask_result pool[VIKI_CANDIDATE_POOL];
    int n = 0;
    int poolSize = topK * 4 < VIKI_CANDIDATE_POOL ? topK * 4 : VIKI_CANDIDATE_POOL;
    int nOut;

    if( poolSize < 1 ) poolSize = 1;

    ftsQuery = build_or_query(zQuery);
    if( ftsQuery ){
        run_fts(db, ftsQuery, poolSize, pool, &n);
        free(ftsQuery);
    }

    if( emb ){
        float *qvec = malloc(sizeof(float) * (size_t)viki_embedder_dim(emb));
        if( viki_embed(emb, zQuery, qvec) == 0 ){
            run_vector(db, qvec, viki_embedder_dim(emb), viki_embedder_model_id(emb), poolSize, pool, &n);
        }
        free(qvec);
    }

    fill_sources(db, pool, n);
    qsort(pool, (size_t)n, sizeof(viki_ask_result), cmp_result);

    nOut = n < maxResults ? n : maxResults;
    nOut = nOut < topK ? nOut : topK;
    if( nOut > 0 ) memcpy(results, pool, sizeof(viki_ask_result) * (size_t)nOut);
    return nOut;
}

int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb){
    viki_ask_result results[VIKI_CANDIDATE_POOL];
    int n, i;

    if( emb ){
        fprintf(stderr, "viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=%s)\n\n",
                viki_embedder_model_id(emb));
    }else{
        fprintf(stderr,
            "viki ask: degraded mode (BM25 keyword search only -- no embedding "
            "model available; see FINDINGS.md / VIKI_DESIGN.md rung 1/2)\n\n");
    }

    n = viki_ask_query(db, zQuery, topK, emb, results, VIKI_CANDIDATE_POOL);

    for( i = 0; i < n; i++ ){
        /* content_hash#chunk_ix FIRST, source path last. KICKOFF.md
        ** deliverable 2 asks for "results with source content_hash, snippet,
        ** score", and VIKI_DESIGN.md's agent contract has agents citing
        ** content_hashes so answers link back to source artifacts -- until now
        ** the CLI printed neither, only the viki_source path, which is a
        ** best-effort join that prints "(source path unknown)" for any chunk
        ** whose path row is stale/missing. Same value and same semantics as
        ** /api/ask's "hash" field, and `<hash>#<ix>` are exactly the two
        ** parameters /api/chunk?hash=&ix= wants, so a hit named on the CLI can
        ** be fetched in full from `viki serve` without a translation step. The
        ** ragged, possibly-empty, possibly-space-containing source path goes
        ** last so it cannot shift any field a script reads by position. */
        printf("[%d] rrf=%.4f  %s#%d  %s\n    %s\n\n",
               i + 1, results[i].rrf, results[i].hash, results[i].chunk_ix,
               results[i].source, results[i].snippet);
    }
    if( n == 0 ) fprintf(stderr, "(no matches)\n");
    return 0;
}
