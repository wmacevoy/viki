/*
** viki_core.c -- assertions, merge, projection, retrieval.
**
** No fopen, no socket, no fork, no fossil. Everything happens on the
** sqlite3* the caller retained. Grep this file for those and the absence is
** the design (core/test/core-probe.sh C1 asserts it).
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "viki_core.h"
#include "sha256.h"

RETAIN_DEFINE(VikiStore);
RETAIN_DEFINE(VikiEmbed);

static char g_err[512] = "";
const char *viki_errmsg(void){ return g_err; }
static VikiStatus fail(VikiStatus rc, const char *zFmt, const char *zArg){
    snprintf(g_err, sizeof(g_err), zFmt, zArg ? zArg : "");
    return rc;
}

int viki_isa(const VikiType *pMe, const VikiType *pOf){
    while( pMe ){ if( pMe==pOf ) return 1; pMe = pMe->pParent; }
    return 0;
}

/* ---- the retained store, or a clear refusal ------------------------- */
static sqlite3 *db_or_null(void){
    if( !RETAINED(VikiStore) ){
        snprintf(g_err, sizeof(g_err),
                 "no VikiStore retained -- RETAIN_BEGIN(VikiStore, &s, g)");
        return 0;
    }
    return RECALL(VikiStore)->db;
}

/* ---- viki_cos(): cosine over two float32 BLOBs ----------------------
** Core owns its own vector function rather than depending on a vendored
** extension: it is twenty lines, it removes a build dependency, and it means
** `viki_sql` callers reach the same function the ask path uses. */
static void cosFunc(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    const float *a, *b; int na, nb, i, n; double dot=0, ma=0, mb=0;
    if( argc!=2 ) return;
    if( sqlite3_value_type(argv[0])!=SQLITE_BLOB
     || sqlite3_value_type(argv[1])!=SQLITE_BLOB ) return;
    a = (const float*)sqlite3_value_blob(argv[0]); na = sqlite3_value_bytes(argv[0]);
    b = (const float*)sqlite3_value_blob(argv[1]); nb = sqlite3_value_bytes(argv[1]);
    if( !a || !b || na!=nb || na<(int)sizeof(float) ) return;
    n = na/(int)sizeof(float);
    for(i=0;i<n;i++){ dot += (double)a[i]*b[i]; ma += (double)a[i]*a[i]; mb += (double)b[i]*b[i]; }
    if( ma<=0 || mb<=0 ){ sqlite3_result_double(ctx, 0.0); return; }
    sqlite3_result_double(ctx, dot/(sqrt(ma)*sqrt(mb)));
}

/* ---- schema ---------------------------------------------------------
** viki_assert is GROW-ONLY on a content key, which is what makes union the
** whole of merge. viki_chunk and viki_fts are a PROJECTION: deletable,
** rebuildable, never merged. */
static const char zSchema[] =
  "CREATE TABLE IF NOT EXISTS viki_assert("
  "  id         TEXT PRIMARY KEY,"   /* sha256 hex of canon()               */
  "  kind       TEXT NOT NULL,"
  "  akey       TEXT NOT NULL,"      /* what it competes on                 */
  "  arank      TEXT NOT NULL,"      /* LEXICAL sort key; max wins          */
  "  ts         TEXT NOT NULL,"
  "  supersedes TEXT,"
  "  body       TEXT NOT NULL,"
  "  atext      TEXT NOT NULL"
  ") WITHOUT ROWID;"
  "CREATE INDEX IF NOT EXISTS viki_assert_key ON viki_assert(akey, arank);"
  "CREATE INDEX IF NOT EXISTS viki_assert_sup ON viki_assert(supersedes);"
  "CREATE TABLE IF NOT EXISTS viki_chunk("
  "  seq   INTEGER PRIMARY KEY,"
  "  id    TEXT NOT NULL,"
  "  ix    INTEGER NOT NULL,"
  "  epoch TEXT NOT NULL,"           /* '' when unembedded                  */
  "  text  TEXT NOT NULL,"
  "  vec   BLOB,"
  "  UNIQUE(id, ix, epoch)"
  ");"
  "CREATE VIRTUAL TABLE IF NOT EXISTS viki_fts"
  " USING fts5(text, content='viki_chunk', content_rowid='seq');";

VikiStatus viki_attach(sqlite3 *db){
    char *zErr = 0;
    if( !db ) return fail(VIKI_EINVAL, "viki_attach: null connection%s", "");
    if( sqlite3_create_function(db, "viki_cos", 2,
            SQLITE_UTF8|SQLITE_DETERMINISTIC, 0, cosFunc, 0, 0)!=SQLITE_OK ){
        return fail(VIKI_ESQL, "viki_attach: %s", sqlite3_errmsg(db));
    }
    if( sqlite3_exec(db, zSchema, 0, 0, &zErr)!=SQLITE_OK ){
        VikiStatus rc = fail(VIKI_ESQL, "viki_attach: %s", zErr);
        sqlite3_free(zErr); return rc;
    }
    return VIKI_OK;
}

/* ---- assertions ------------------------------------------------------ */
static void hex_sha256(const char *z, char *zOut){
    viki_sha256_hex(z, strlen(z), zOut);
}

VikiStatus viki_put(VikiAssert *p){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0;
    const char *zCanon;
    int rc;
    if( !db ) return VIKI_ENOCTX;
    if( !p || !p->vftbl ) return fail(VIKI_EINVAL, "viki_put: null assertion%s", "");
    zCanon = p->vftbl->canon(p);
    if( !zCanon ) return fail(VIKI_EINVAL, "viki_put: canon() returned null%s", "");
    hex_sha256(zCanon, p->zId);
    /* OR IGNORE, not OR REPLACE: rows are immutable and identity is the hash
    ** of the content, so a second put of the same bytes is a no-op by
    ** construction. This is the same property that makes merge trivial. */
    rc = sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO viki_assert(id,kind,akey,arank,ts,supersedes,body,atext)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &st, 0);
    if( rc!=SQLITE_OK ) return fail(VIKI_ESQL, "viki_put: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, p->zId, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, p->vftbl->type->zName, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, p->vftbl->key(p), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, p->vftbl->rank(p), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, p->zTs ? p->zTs : "", -1, SQLITE_TRANSIENT);
    if( p->zSupersedes ) sqlite3_bind_text(st, 6, p->zSupersedes, -1, SQLITE_TRANSIENT);
    else                 sqlite3_bind_null(st, 6);
    sqlite3_bind_text(st, 7, zCanon, -1, SQLITE_TRANSIENT);
    /* text() is stored, not recomputed: reindex then needs only SQL and
    ** the embed callback, never a live object of the right subtype. */
    sqlite3_bind_text(st, 8, p->vftbl->text(p), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if( rc!=SQLITE_DONE ) return fail(VIKI_ESQL, "viki_put: %s", sqlite3_errmsg(db));
    return VIKI_OK;
}

VikiStatus viki_get(const char *zId, char **pzBody){
    sqlite3 *db = db_or_null(); sqlite3_stmt *st = 0; int rc;
    if( !db ) return VIKI_ENOCTX;
    *pzBody = 0;
    if( sqlite3_prepare_v2(db, "SELECT body FROM viki_assert WHERE id=?1",
                           -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_get: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if( rc==SQLITE_ROW ){
        const char *z = (const char*)sqlite3_column_text(st, 0);
        *pzBody = z ? strdup(z) : 0;
    }
    sqlite3_finalize(st);
    return rc==SQLITE_ROW ? VIKI_OK : VIKI_ENOTFOUND;
}

/* RESOLUTION IS ONE STATEMENT FOR EVERY TYPE, which is the payoff of making
** rank() a vtable slot: the type produced a sortable string at write time,
** so read time is `max(arank) among those nothing supersedes`. */
VikiStatus viki_current(const char *zKey, char *zIdOut, char **pzBody){
    sqlite3 *db = db_or_null(); sqlite3_stmt *st = 0; int rc;
    if( !db ) return VIKI_ENOCTX;
    if( pzBody ) *pzBody = 0;
    if( sqlite3_prepare_v2(db,
        "SELECT a.id, a.body FROM viki_assert a"
        " WHERE a.akey=?1"
        "   AND NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
        " ORDER BY a.arank DESC LIMIT 1", -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_current: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zKey, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if( rc==SQLITE_ROW ){
        if( zIdOut ) snprintf(zIdOut, VIKI_ID_HEX+1, "%s", sqlite3_column_text(st,0));
        if( pzBody ){ const char *z=(const char*)sqlite3_column_text(st,1); *pzBody = z?strdup(z):0; }
    }
    sqlite3_finalize(st);
    return rc==SQLITE_ROW ? VIKI_OK : VIKI_ENOTFOUND;
}

/* ---- merge ----------------------------------------------------------- */
VikiStatus viki_merge(sqlite3 *pOther, int *pnAdded){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0; int n = 0, rc;
    if( !db ) return VIKI_ENOCTX;
    if( !pOther ) return fail(VIKI_EINVAL, "viki_merge: null source%s", "");
    if( pnAdded ) *pnAdded = 0;
    /* Read the other store through ITS OWN handle and insert through ours.
    ** No ATTACH: the two connections may have different keys, different VFS,
    ** or live in different processes' memory -- none of which is core's
    ** business, and ATTACH would make all three core's problem. */
    if( sqlite3_prepare_v2(pOther,
        "SELECT id,kind,akey,arank,ts,supersedes,body,atext FROM viki_assert",
        -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_merge: source: %s", sqlite3_errmsg(pOther));
    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    while( sqlite3_step(st)==SQLITE_ROW ){
        sqlite3_stmt *ins = 0; int i;
        if( sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO viki_assert(id,kind,akey,arank,ts,supersedes,body,atext)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &ins, 0)!=SQLITE_OK ) break;
        for(i=0;i<8;i++) sqlite3_bind_value(ins, i+1, sqlite3_column_value(st, i));
        if( sqlite3_step(ins)==SQLITE_DONE ) n += sqlite3_changes(db);
        sqlite3_finalize(ins);
    }
    sqlite3_finalize(st);
    rc = sqlite3_exec(db, "COMMIT", 0, 0, 0);
    if( rc!=SQLITE_OK ) return fail(VIKI_ESQL, "viki_merge: %s", sqlite3_errmsg(db));
    if( pnAdded ) *pnAdded = n;
    return VIKI_OK;
}

/* ---- projection: chunk, FTS, vectors --------------------------------
** Chunk geometry is PORTED, not re-chosen: 40 lines with 10 overlapping was
** measured in (recall@1 0.256 -> 0.349, MRR 0.381 -> 0.424 at +26% chunks),
** and 20 cost +77% chunks for nothing. Do not adjust without re-measuring. */
#define VIKI_CHUNK_LINES   40
#define VIKI_CHUNK_OVERLAP 10
#define VIKI_CHUNK_STRIDE  (VIKI_CHUNK_LINES-VIKI_CHUNK_OVERLAP)

/* Returns chunk ix of zText into pz and pn, or 0 when ix is past the end. */
static int chunk_at(const char *zText, int ix, const char **pz, size_t *pn){
    const char *p = zText, *zStart; int line = 0, want = ix*VIKI_CHUNK_STRIDE, n = 0;
    if( ix<0 ) return 0;
    while( line<want && *p ){ if( *p=='\n' ) line++; p++; }
    if( !*p && want>0 ) return 0;
    zStart = p;
    while( *p && n<VIKI_CHUNK_LINES ){ if( *p=='\n' ) n++; p++; }
    if( p==zStart ) return 0;
    *pz = zStart; *pn = (size_t)(p-zStart);
    return 1;
}

VikiStatus viki_reindex(int *pnChunked){
    sqlite3 *db = db_or_null();
    const VikiEmbed *pe = RETAINED(VikiEmbed) ? RECALL(VikiEmbed) : 0;
    const char *zEpoch = pe ? pe->zEpoch : "";
    sqlite3_stmt *st = 0; int nTot = 0;
    if( !db ) return VIKI_ENOCTX;
    if( pnChunked ) *pnChunked = 0;
    if( sqlite3_prepare_v2(db,
        "SELECT id, atext FROM viki_assert WHERE id NOT IN"
        " (SELECT id FROM viki_chunk WHERE epoch=?1)", -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_reindex: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zEpoch, -1, SQLITE_STATIC);
    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    while( sqlite3_step(st)==SQLITE_ROW ){
        const char *zId = (const char*)sqlite3_column_text(st, 0);
        const char *zTx = (const char*)sqlite3_column_text(st, 1);
        char zIdBuf[VIKI_ID_HEX+1];
        const char *zC; size_t nC; int ix = 0;
        if( !zId || !zTx ) continue;
        snprintf(zIdBuf, sizeof(zIdBuf), "%s", zId);
        while( chunk_at(zTx, ix, &zC, &nC) ){
            sqlite3_stmt *ins = 0;
            float *aVec = 0;
            if( pe ){
                char *zTmp = (char*)malloc(nC+1);
                if( zTmp ){
                    memcpy(zTmp, zC, nC); zTmp[nC] = 0;
                    aVec = (float*)calloc((size_t)pe->nDim, sizeof(float));
                    if( aVec && pe->xEmbed(pe->pApp, zTmp, aVec, pe->nDim)!=0 ){
                        free(aVec); aVec = 0;     /* embedder declined: keyword only */
                    }
                    free(zTmp);
                }
            }
            if( sqlite3_prepare_v2(db,
                "INSERT OR IGNORE INTO viki_chunk(id,ix,epoch,text,vec)"
                " VALUES(?1,?2,?3,?4,?5)", -1, &ins, 0)==SQLITE_OK ){
                sqlite3_bind_text(ins, 1, zIdBuf, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (ins, 2, ix);
                sqlite3_bind_text(ins, 3, zEpoch, -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 4, zC, (int)nC, SQLITE_TRANSIENT);
                if( aVec ) sqlite3_bind_blob(ins, 5, aVec,
                                             (int)(pe->nDim*sizeof(float)), SQLITE_TRANSIENT);
                else       sqlite3_bind_null(ins, 5);
                if( sqlite3_step(ins)==SQLITE_DONE && sqlite3_changes(db)>0 ){
                    sqlite3_stmt *f = 0;
                    sqlite3_int64 seq = sqlite3_last_insert_rowid(db);
                    /* External-content FTS: the row must be told about the
                    ** text explicitly, and it re-reads viki_chunk to know
                    ** what tokens to drop on delete -- which is why any
                    ** future delete path must remove FTS FIRST. */
                    if( sqlite3_prepare_v2(db,
                        "INSERT INTO viki_fts(rowid, text) VALUES(?1,?2)",
                        -1, &f, 0)==SQLITE_OK ){
                        sqlite3_bind_int64(f, 1, seq);
                        sqlite3_bind_text (f, 2, zC, (int)nC, SQLITE_TRANSIENT);
                        sqlite3_step(f); sqlite3_finalize(f);
                    }
                    nTot++;
                }
                sqlite3_finalize(ins);
            }
            free(aVec);
            ix++;
        }
    }
    sqlite3_finalize(st);
    sqlite3_exec(db, "COMMIT", 0, 0, 0);
    if( pnChunked ) *pnChunked = nTot;
    return VIKI_OK;
}

/* ---- retrieval -------------------------------------------------------
** Three legs into one pool, fused by reciprocal rank (RRF, k=60). All three
** constants are PORTED and were measured, not chosen: the pool at 150
** (recall@5 0.512 -> 0.558 going 40 -> 80, with 160 giving recall@k back),
** and the LITERAL leg's rarity weighting, without which a unique identifier
** ties with the common words around it and volume wins.
**
** WHY THREE. bm25() rewards term frequency and cosine rewards topical
** concentration, so both are DENSITY-BIASED: a document treating a subject at
** length takes every slot, and a document stating the same fact once, in
** passing, is buried. Passing mentions are where a partially-applied update
** hides. */
#define VIKI_RRF_K         60.0
#define VIKI_POOL          150
#define VIKI_LEG_BUDGET    40
#define VIKI_MAX_TERMS     32

typedef struct { sqlite3_int64 seq; double score; } Cand;

static int cand_find(Cand *a, int n, sqlite3_int64 seq){
    int i; for(i=0;i<n;i++) if( a[i].seq==seq ) return i; return -1;
}
/* Adds rank-r (0-based) evidence for seq. Returns the (possibly new) count.
** A leg may only ADD to the pool while there is room -- and when the pool is
** full it may still REINFORCE a candidate already in it. Letting a full pool
** silently drop a leg's evidence is how the vector and literal legs were once
** reduced to re-ranking whatever BM25 had already chosen. */
static int cand_add(Cand *a, int n, sqlite3_int64 seq, int r){
    int i = cand_find(a, n, seq);
    double w = 1.0/(VIKI_RRF_K + (double)r + 1.0);
    if( i>=0 ){ a[i].score += w; return n; }
    if( n>=VIKI_POOL ) return n;
    a[n].seq = seq; a[n].score = w; return n+1;
}

/* ASCII word split. Known-naive by choice, same as the tokenizer this ports
** from; non-ASCII corpora are a separate, measured decision. */
static int split_terms(const char *zQ, char **az, int nMax){
    int n = 0; const char *p = zQ;
    while( *p && n<nMax ){
        const char *s;
        while( *p && !((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')) ) p++;
        s = p;
        while( *p && ((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')) ) p++;
        if( p>s ){
            size_t k = (size_t)(p-s); char *z = (char*)malloc(k+1);
            if( !z ) break;
            memcpy(z, s, k); z[k]=0; az[n++] = z;
        }
    }
    return n;
}

VikiStatus viki_ask(const char *zQuery, int k, VikiHits **ppOut){
    sqlite3 *db = db_or_null();
    const VikiEmbed *pe = RETAINED(VikiEmbed) ? RECALL(VikiEmbed) : 0;
    const char *zEpoch = pe ? pe->zEpoch : "";
    Cand pool[VIKI_POOL];
    char *azTerm[VIKI_MAX_TERMS];
    int nTerm, nPool = 0, i, j, nOut;
    VikiHits *pH;
    sqlite3_stmt *st = 0;
    char *zMatch = 0;

    if( !db ) return VIKI_ENOCTX;
    if( !zQuery || !*zQuery || k<=0 ) return fail(VIKI_EINVAL, "viki_ask: empty query%s","");
    *ppOut = 0;
    nTerm = split_terms(zQuery, azTerm, VIKI_MAX_TERMS);

    /* --- leg 1: FTS5 BM25. THE MATCH MUST BE OR-OF-TERMS: FTS5's MATCH is
    ** implicit-AND, which is simply wrong for a natural-language query -- one
    ** absent word and the whole query returns nothing. */
    for(i=0;i<nTerm;i++){
        char *zNew = sqlite3_mprintf("%z%s\"%q\"", zMatch, zMatch?" OR ":"", azTerm[i]);
        zMatch = zNew;
    }
    if( zMatch && sqlite3_prepare_v2(db,
        "SELECT c.seq FROM viki_fts f JOIN viki_chunk c ON c.seq=f.rowid"
        " WHERE viki_fts MATCH ?1 AND c.epoch=?2"
        " ORDER BY bm25(viki_fts) LIMIT ?3", -1, &st, 0)==SQLITE_OK ){
        int r = 0;
        sqlite3_bind_text(st, 1, zMatch, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, zEpoch, -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 3, VIKI_LEG_BUDGET);
        while( sqlite3_step(st)==SQLITE_ROW )
            nPool = cand_add(pool, nPool, sqlite3_column_int64(st,0), r++);
        sqlite3_finalize(st); st = 0;
    }

    /* --- leg 2: LITERAL, rarity-weighted. Exact substring, scored by the sum
    ** of 1/df over the query terms a chunk contains. Needs no model, and it
    ** is what stops the two density-biased legs burying a passing mention. */
    if( nTerm>0 ){
        typedef struct { sqlite3_int64 seq; double w; } Lit;
        Lit *aLit = (Lit*)calloc(VIKI_POOL*4, sizeof(Lit));
        int nLit = 0;
        for(i=0;i<nTerm;i++){
            double df = 1.0; int cnt = 0;
            sqlite3_stmt *q = 0;
            if( sqlite3_prepare_v2(db,
                "SELECT count(*) FROM viki_chunk WHERE epoch=?1"
                " AND instr(lower(text), lower(?2))>0", -1, &q, 0)!=SQLITE_OK ) continue;
            sqlite3_bind_text(q, 1, zEpoch, -1, SQLITE_STATIC);
            sqlite3_bind_text(q, 2, azTerm[i], -1, SQLITE_STATIC);
            if( sqlite3_step(q)==SQLITE_ROW ) cnt = sqlite3_column_int(q, 0);
            sqlite3_finalize(q);
            if( cnt<=0 ) continue;
            df = (double)cnt;
            if( sqlite3_prepare_v2(db,
                "SELECT seq FROM viki_chunk WHERE epoch=?1"
                " AND instr(lower(text), lower(?2))>0", -1, &q, 0)!=SQLITE_OK ) continue;
            sqlite3_bind_text(q, 1, zEpoch, -1, SQLITE_STATIC);
            sqlite3_bind_text(q, 2, azTerm[i], -1, SQLITE_STATIC);
            while( sqlite3_step(q)==SQLITE_ROW && nLit<VIKI_POOL*4 ){
                sqlite3_int64 s = sqlite3_column_int64(q, 0);
                int f = -1;
                for(j=0;j<nLit;j++) if( aLit[j].seq==s ){ f=j; break; }
                if( f<0 ){ aLit[nLit].seq=s; aLit[nLit].w=1.0/df; nLit++; }
                else aLit[f].w += 1.0/df;
            }
            sqlite3_finalize(q);
        }
        /* rank by weight, then feed the ranks into RRF */
        for(i=0;i<nLit && i<VIKI_LEG_BUDGET;i++){
            int best = i;
            for(j=i+1;j<nLit;j++) if( aLit[j].w > aLit[best].w ) best = j;
            { Lit t = aLit[i]; aLit[i] = aLit[best]; aLit[best] = t; }
            nPool = cand_add(pool, nPool, aLit[i].seq, i);
        }
        free(aLit);
    }

    /* --- leg 3: VECTOR, only when an embedder was retained. Its absence is
    ** the degraded mode, reported rather than hidden. */
    if( pe ){
        float *aQ = (float*)calloc((size_t)pe->nDim, sizeof(float));
        if( aQ && pe->xEmbed(pe->pApp, zQuery, aQ, pe->nDim)==0 ){
            if( sqlite3_prepare_v2(db,
                "SELECT seq FROM viki_chunk WHERE epoch=?1 AND vec IS NOT NULL"
                " ORDER BY viki_cos(vec, ?2) DESC LIMIT ?3", -1, &st, 0)==SQLITE_OK ){
                int r = 0;
                sqlite3_bind_text(st, 1, zEpoch, -1, SQLITE_STATIC);
                sqlite3_bind_blob(st, 2, aQ, (int)(pe->nDim*sizeof(float)), SQLITE_STATIC);
                sqlite3_bind_int (st, 3, VIKI_LEG_BUDGET);
                while( sqlite3_step(st)==SQLITE_ROW )
                    nPool = cand_add(pool, nPool, sqlite3_column_int64(st,0), r++);
                sqlite3_finalize(st); st = 0;
            }
        }
        free(aQ);
    }

    for(i=0;i<nTerm;i++) free(azTerm[i]);
    sqlite3_free(zMatch);

    /* --- fuse and materialise */
    for(i=0;i<nPool;i++){
        int best = i;
        for(j=i+1;j<nPool;j++) if( pool[j].score > pool[best].score ) best = j;
        { Cand t = pool[i]; pool[i] = pool[best]; pool[best] = t; }
    }
    nOut = nPool < k ? nPool : k;
    pH = (VikiHits*)calloc(1, sizeof(VikiHits));
    if( !pH ) return fail(VIKI_ENOMEM, "viki_ask: out of memory%s","");
    pH->bDegraded = pe ? 0 : 1;
    pH->a = (VikiHit*)calloc((size_t)(nOut>0?nOut:1), sizeof(VikiHit));
    if( !pH->a ){ free(pH); return fail(VIKI_ENOMEM, "viki_ask: out of memory%s",""); }
    for(i=0;i<nOut;i++){
        sqlite3_stmt *q = 0;
        if( sqlite3_prepare_v2(db, "SELECT id, ix, text FROM viki_chunk WHERE seq=?1",
                               -1, &q, 0)!=SQLITE_OK ) break;
        sqlite3_bind_int64(q, 1, pool[i].seq);
        if( sqlite3_step(q)==SQLITE_ROW ){
            const char *zT = (const char*)sqlite3_column_text(q, 2);
            snprintf(pH->a[pH->n].zId, VIKI_ID_HEX+1, "%s", sqlite3_column_text(q,0));
            pH->a[pH->n].ix    = sqlite3_column_int(q, 1);
            pH->a[pH->n].score = pool[i].score;
            pH->a[pH->n].zText = zT ? strdup(zT) : 0;
            pH->n++;
        }
        sqlite3_finalize(q);
    }
    *ppOut = pH;
    return VIKI_OK;
}

void viki_hits_free(VikiHits *p){
    int i;
    if( !p ) return;
    for(i=0;i<p->n;i++) free(p->a[i].zText);
    free(p->a); free(p);
}

/* ---- the note type --------------------------------------------------- */
static const VikiType vikiNoteType = { "note", 0, sizeof(VikiNote) };
static const char *noteKey (const VikiAssert *p){
    const VikiNote *n = (const VikiNote*)p;
    return n->zKey ? n->zKey : n->zId;   /* unkeyed notes compete only with themselves */
}
static const char *noteRank(const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *noteText(const VikiAssert *p){ return ((const VikiNote*)p)->zText; }
static const char *noteCanon(const VikiAssert *p){ return ((const VikiNote*)p)->zText; }
const struct VikiAssertVftbl vikiNoteVftbl = {
    &vikiNoteType, noteKey, noteRank, noteText, noteCanon
};

VikiStatus viki_noteid(const char *zText, char *zIdOut){
    VikiNote n;
    VikiStatus rc;
    if( !zText ) return fail(VIKI_EINVAL, "viki_note: null text%s","");
    memset(&n, 0, sizeof(n));
    n.vftbl = &vikiNoteVftbl;
    n.zText = zText;
    n.zTs   = 0;
    rc = viki_put((VikiAssert*)&n);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, n.zId, VIKI_ID_HEX+1);
    return rc;
}
VikiStatus viki_note(const char *zText){ return viki_noteid(zText, 0); }

/* ---- the raw rung ---------------------------------------------------- */
VikiStatus viki_sql(const char *zSql, viki_row x, void *pApp){
    sqlite3 *db = db_or_null(); sqlite3_stmt *st = 0; int rc;
    if( !db ) return VIKI_ENOCTX;
    if( sqlite3_prepare_v2(db, zSql, -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_sql: %s", sqlite3_errmsg(db));
    while( (rc=sqlite3_step(st))==SQLITE_ROW ){
        int n = sqlite3_column_count(st), i;
        const char **az = (const char**)calloc((size_t)n, sizeof(char*));
        if( !az ) break;
        for(i=0;i<n;i++) az[i] = (const char*)sqlite3_column_text(st, i);
        if( x && x(pApp, n, az) ){ free(az); break; }
        free(az);
    }
    sqlite3_finalize(st);
    return VIKI_OK;
}
