#include "viki_ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        /* query was empty/whitespace-only */
        free(out);
        return NULL;
    }
    return out;
}

int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK){
    sqlite3_stmt *st;
    int rc;
    int nResults = 0;
    char *ftsQuery;

    fprintf(stderr,
        "viki ask: degraded mode (BM25 keyword search only -- no embedding "
        "model wired up yet; see FINDINGS.md / VIKI_DESIGN.md rung 1/2)\n\n");

    ftsQuery = build_or_query(zQuery);
    if( !ftsQuery ){
        fprintf(stderr, "viki ask: empty query\n");
        return 1;
    }

    rc = sqlite3_prepare_v2(db,
        "SELECT content_hash, chunk_ix, snippet(chunk_fts, 0, '[', ']', ' ... ', 24), bm25(chunk_fts) "
        "FROM chunk_fts WHERE chunk_fts MATCH ?1 ORDER BY bm25(chunk_fts) LIMIT ?2",
        -1, &st, NULL);
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki ask: query prepare failed: %s\n", sqlite3_errmsg(db));
        free(ftsQuery);
        return 1;
    }
    sqlite3_bind_text(st, 1, ftsQuery, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, topK);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const unsigned char *hash = sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const unsigned char *snippet = sqlite3_column_text(st, 2);
        double score = sqlite3_column_double(st, 3);
        const char *path = NULL;
        sqlite3_stmt *stPath;

        if( sqlite3_prepare_v2(db,
                "SELECT path FROM viki_source WHERE content_hash=?1 LIMIT 1",
                -1, &stPath, NULL) == SQLITE_OK ){
            sqlite3_bind_text(stPath, 1, (const char*)hash, -1, SQLITE_STATIC);
            if( sqlite3_step(stPath) == SQLITE_ROW ){
                path = (const char*)sqlite3_column_text(stPath, 0);
            }
            printf("[%d] score=%.3f  %s#%d\n    %s\n\n",
                   ++nResults, score,
                   path ? path : "(source path unknown)",
                   chunk_ix, snippet ? (const char*)snippet : "");
            sqlite3_finalize(stPath);
        }
    }
    sqlite3_finalize(st);
    free(ftsQuery);

    if( nResults == 0 ){
        fprintf(stderr, "(no matches)\n");
    }
    return 0;
}
