#include "viki_ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIKI_RRF_K 60.0      /* standard RRF damping constant */
#define VIKI_CANDIDATE_POOL 40

typedef struct {
    char hash[65];
    int chunk_ix;
    char snippet[512]; /* best snippet/excerpt we've seen for this hit */
    double rrf;
} Candidate;

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

static Candidate *find_or_add(Candidate *pool, int *n, const char *hash, int chunk_ix){
    int i;
    for( i = 0; i < *n; i++ ){
        if( pool[i].chunk_ix == chunk_ix && strcmp(pool[i].hash, hash) == 0 ) return &pool[i];
    }
    if( *n >= VIKI_CANDIDATE_POOL ) return NULL;
    memset(&pool[*n], 0, sizeof(Candidate));
    strncpy(pool[*n].hash, hash, 64);
    pool[*n].chunk_ix = chunk_ix;
    (*n)++;
    return &pool[*n - 1];
}

static int cmp_candidate(const void *a, const void *b){
    double da = ((const Candidate*)a)->rrf, db = ((const Candidate*)b)->rrf;
    if( da > db ) return -1;
    if( da < db ) return 1;
    return 0;
}

static void run_fts(sqlite3 *db, const char *ftsQuery, int poolSize, Candidate *pool, int *n){
    sqlite3_stmt *st;
    int rank = 0;

    if( sqlite3_prepare_v2(db,
            "SELECT content_hash, chunk_ix, snippet(chunk_fts, 0, '[', ']', ' ... ', 24) "
            "FROM chunk_fts WHERE chunk_fts MATCH ?1 ORDER BY bm25(chunk_fts) LIMIT ?2",
            -1, &st, NULL) != SQLITE_OK ){
        return;
    }
    sqlite3_bind_text(st, 1, ftsQuery, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, poolSize);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *snippet = (const char*)sqlite3_column_text(st, 2);
        Candidate *c;
        rank++;
        c = find_or_add(pool, n, hash, chunk_ix);
        if( !c ) continue;
        c->rrf += 1.0 / (VIKI_RRF_K + rank);
        if( snippet && !c->snippet[0] ) strncpy(c->snippet, snippet, sizeof(c->snippet) - 1);
    }
    sqlite3_finalize(st);
}

static void run_vector(sqlite3 *db, const float *qvec, int dim, const char *modelId,
                        int poolSize, Candidate *pool, int *n){
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
        Candidate *c;
        rank++;
        c = find_or_add(pool, n, hash, chunk_ix);
        if( !c ) continue;
        c->rrf += 1.0 / (VIKI_RRF_K + rank);
        if( excerpt && !c->snippet[0] ) strncpy(c->snippet, excerpt, sizeof(c->snippet) - 1);
    }
    sqlite3_finalize(st);
}

int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb){
    char *ftsQuery;
    Candidate pool[VIKI_CANDIDATE_POOL];
    int n = 0;
    int poolSize = topK * 4 < VIKI_CANDIDATE_POOL ? topK * 4 : VIKI_CANDIDATE_POOL;
    int i;

    if( emb ){
        fprintf(stderr, "viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=%s)\n\n",
                viki_embedder_model_id(emb));
    }else{
        fprintf(stderr,
            "viki ask: degraded mode (BM25 keyword search only -- no embedding "
            "model available; see FINDINGS.md / VIKI_DESIGN.md rung 1/2)\n\n");
    }

    ftsQuery = build_or_query(zQuery);
    if( !ftsQuery ){
        fprintf(stderr, "viki ask: empty query\n");
        return 1;
    }
    run_fts(db, ftsQuery, poolSize, pool, &n);
    free(ftsQuery);

    if( emb ){
        float *qvec = malloc(sizeof(float) * (size_t)viki_embedder_dim(emb));
        if( viki_embed(emb, zQuery, qvec) == 0 ){
            run_vector(db, qvec, viki_embedder_dim(emb), viki_embedder_model_id(emb), poolSize, pool, &n);
        }else{
            fprintf(stderr, "viki ask: could not embed the query; falling back to BM25-only for this call\n");
        }
        free(qvec);
    }

    qsort(pool, (size_t)n, sizeof(Candidate), cmp_candidate);

    for( i = 0; i < n && i < topK; i++ ){
        sqlite3_stmt *stPath;
        const char *path = NULL;
        if( sqlite3_prepare_v2(db, "SELECT path FROM viki_source WHERE content_hash=?1 LIMIT 1",
                -1, &stPath, NULL) == SQLITE_OK ){
            sqlite3_bind_text(stPath, 1, pool[i].hash, -1, SQLITE_STATIC);
            if( sqlite3_step(stPath) == SQLITE_ROW ) path = (const char*)sqlite3_column_text(stPath, 0);
            printf("[%d] rrf=%.4f  %s#%d\n    %s\n\n",
                   i + 1, pool[i].rrf, path ? path : "(source path unknown)",
                   pool[i].chunk_ix, pool[i].snippet);
            sqlite3_finalize(stPath);
        }
    }

    if( n == 0 ) fprintf(stderr, "(no matches)\n");
    return 0;
}
