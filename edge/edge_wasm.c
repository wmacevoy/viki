/* viki edge -- the READ-ONLY tier of the vikiverse, compiled to wasm.
**
** This is `caching = required` from VIKIVERSE.md made real: a peer that never
** indexes, never runs a model, never spawns fossil and never opens a socket.
** It receives a `.viki/cache.db` produced by someone else (D-11: an embedding
** is a deterministic function of (content_hash, model_id, chunk_params), so
** whoever saw the content first computed it) and answers queries against it.
**
** WHAT IT CAN AND CANNOT DO, and the boundary is not arbitrary:
**   CAN  -- FTS5 BM25, the literal leg, and therefore `ask` in degraded mode,
**           plus `grep`. Degraded mode is a REQUIRED path in this
**           architecture, not a failure (CLAUDE.md), which is the only reason
**           a no-model build is coherent at all.
**   CANNOT -- embed a QUERY. The stored chunk vectors are right there in the
**           cache, but turning the user's words into a comparable vector needs
**           the model, and ONNX Runtime is not linked here. So the cosine leg
**           cannot run from a text query.
**
** The three ONNX symbols the retrieval core references are supplied as
** aborting stubs in edge_noembed.c; they sit behind `if( emb )` and are never
** reached. That three-symbol coupling is the entire distance between viki's
** read path and a phone.
**
** The cache is DATA, not baked in: JS writes the bytes into the virtual FS and
** calls edge_open(). That mirrors `viki cache pull` rather than shipping a
** corpus inside the binary, and it keeps the wasm generic. */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viki_db.h"
#include "viki_ask.h"
#include "embed.h"

static sqlite3 *g_db = NULL;

/* Supplied by edge_embed_js.c: the JS-backed embedder, or NULL when no query
** vector has been set. Passing NULL is the degraded path, not an error. */
extern viki_embedder *edge_embedder(void);

/* Appends z to a growing buffer as a JSON string body, escaping what RFC 8259
** requires. Indexed content is untrusted text -- it is this project's own
** documents, but also whatever a peer put in the cache -- so anything that
** could terminate the string or inject structure has to go. */
static void json_escape(char **buf, size_t *len, size_t *cap, const char *z){
    const unsigned char *p = (const unsigned char*)z;
    for( ; p && *p; p++ ){
        char tmp[8]; const char *add; size_t n;
        switch( *p ){
            case '"':  add = "\\\""; n = 2; break;
            case '\\': add = "\\\\"; n = 2; break;
            case '\n': add = "\\n";  n = 2; break;
            case '\r': add = "\\r";  n = 2; break;
            case '\t': add = "\\t";  n = 2; break;
            default:
                if( *p < 0x20 ){ snprintf(tmp, sizeof(tmp), "\\u%04x", *p); add = tmp; n = 6; }
                else { tmp[0] = (char)*p; tmp[1] = 0; add = tmp; n = 1; }
        }
        if( *len + n + 1 > *cap ){
            *cap = (*cap ? *cap * 2 : 1024) + n;
            *buf = realloc(*buf, *cap);
            if( !*buf ){ *len = 0; *cap = 0; return; }
        }
        memcpy(*buf + *len, add, n);
        *len += n;
    }
}

static void json_raw(char **buf, size_t *len, size_t *cap, const char *z){
    size_t n = strlen(z);
    if( *len + n + 1 > *cap ){
        *cap = (*cap ? *cap * 2 : 1024) + n;
        *buf = realloc(*buf, *cap);
        if( !*buf ){ *len = 0; *cap = 0; return; }
    }
    memcpy(*buf + *len, z, n);
    *len += n;
}

EMSCRIPTEN_KEEPALIVE
int edge_open(const char *zPath){
    if( g_db ){ sqlite3_close(g_db); g_db = NULL; }
    viki_db_register_ndvss();
    if( viki_db_open(zPath, &g_db) != SQLITE_OK ){ g_db = NULL; return 0; }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int edge_chunk_count(void){
    sqlite3_stmt *st; int n = 0;
    if( !g_db ) return -1;
    if( sqlite3_prepare_v2(g_db, "SELECT count(*) FROM viki_chunk", -1, &st, NULL) == SQLITE_OK ){
        if( sqlite3_step(st) == SQLITE_ROW ) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

/* Returns a malloc'd JSON string; the caller must edge_free() it.
**
** The fragment facts are exposed as BOOLEANS and the snippet is left
** UNDECORATED, exactly as `/api/ask` does (viki_ask.h): a client already
** parsing `snippet` must not start receiving marker words it cannot tell
** apart from indexed content. The page does the marking. */
EMSCRIPTEN_KEEPALIVE
char *edge_ask(const char *zQuery, int k){
    viki_ask_result res[20];
    char *buf = NULL; size_t len = 0, cap = 0;
    char num[128];
    int n, i;

    if( k < 1 ) k = 5;
    if( k > 20 ) k = 20;
    if( !g_db ){
        buf = malloc(64); if( buf ) strcpy(buf, "{\"error\":\"no cache open\"}");
        return buf;
    }
    {
        viki_embedder *emb = edge_embedder();
        n = viki_ask_query(g_db, zQuery, k, emb, res, 20);
        json_raw(&buf, &len, &cap, emb
                 ? "{\"mode\":\"hybrid\",\"results\":["
                 : "{\"mode\":\"bm25+literal\",\"results\":[");
    }
    for( i = 0; i < n; i++ ){
        if( i ) json_raw(&buf, &len, &cap, ",");
        json_raw(&buf, &len, &cap, "{\"rank\":");
        snprintf(num, sizeof(num), "%d,\"rrf\":%.6f,\"chunk_ix\":%d,\"chunk_count\":%d,",
                 i + 1, res[i].rrf, res[i].chunk_ix, res[i].chunk_count);
        json_raw(&buf, &len, &cap, num);
        json_raw(&buf, &len, &cap, "\"hash\":\"");
        json_escape(&buf, &len, &cap, res[i].hash);
        json_raw(&buf, &len, &cap, "\",\"source\":\"");
        json_escape(&buf, &len, &cap, res[i].source);
        json_raw(&buf, &len, &cap, "\",\"snippet\":\"");
        json_escape(&buf, &len, &cap, res[i].snippet);
        snprintf(num, sizeof(num), "\",\"fragment_head\":%s,\"fragment_tail\":%s,\"snippet_truncated\":%s}",
                 (res[i].frag & VIKI_FRAG_HEAD) ? "true" : "false",
                 (res[i].frag & VIKI_FRAG_TAIL) ? "true" : "false",
                 (res[i].frag & VIKI_FRAG_CUT)  ? "true" : "false");
        json_raw(&buf, &len, &cap, num);
    }
    json_raw(&buf, &len, &cap, "]}");
    if( buf ) buf[len] = '\0';
    return buf;
}

EMSCRIPTEN_KEEPALIVE
void edge_free(char *p){ free(p); }
