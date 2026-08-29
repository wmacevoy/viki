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

/* PER-THREAD, because the context it describes is. A shared buffer would let
** one thread's failure be read as another's -- and viki_errmsg() is the only
** way a caller learns WHY, so a crossed message is worse than none. */
#if defined(_MSC_VER)
# define VIKI_TLS __declspec(thread)
#else
# define VIKI_TLS _Thread_local
#endif
static VIKI_TLS char g_err[512] = "";
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
    const VikiStore *p;
    if( !RETAINED(VikiStore) ){
        snprintf(g_err, sizeof(g_err),
                 "no VikiStore retained -- RETAIN_BEGIN(VikiStore, &s, g)");
        return 0;
    }
    /* RETAINED() only says something was pushed, not that it was non-NULL or
    ** carried a connection. Both are reachable from a caller doing the
    ** obvious thing with an uninitialised struct. */
    p = RECALL(VikiStore);
    if( !p || !p->db ){
        snprintf(g_err, sizeof(g_err), "retained VikiStore has no connection");
        return 0;
    }
    return p->db;
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

/* TRANSACTIONS ON A CONNECTION WE DO NOT OWN.
**
** Core never issues BEGIN/COMMIT: the caller opened this connection and may
** be inside its own transaction, and a bare COMMIT from here would commit the
** caller's work -- durably, silently, and at a point of our choosing. That is
** the single worst thing a library can do to a host's database.
**
** SAVEPOINT nests, so it is correct whether or not the caller is in a
** transaction: RELEASE of an inner savepoint commits nothing on its own.
** Names are fixed rather than generated because core is single-context per
** thread and re-entrant use of these functions is not supported. */
static int tx_begin(sqlite3 *db, const char *zName){
    char *z = sqlite3_mprintf("SAVEPOINT %w", zName);
    int rc = z ? sqlite3_exec(db, z, 0, 0, 0) : SQLITE_NOMEM;
    sqlite3_free(z);
    return rc;
}
static void tx_end(sqlite3 *db, const char *zName, int bOk){
    char *z = sqlite3_mprintf(bOk ? "RELEASE %w"
                                  : "ROLLBACK TO %w; RELEASE %w", zName, zName);
    if( z ) sqlite3_exec(db, z, 0, 0, 0);
    sqlite3_free(z);
}

VikiStatus viki_put(VikiAssert *p){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0;
    const char *zCanon;
    int rc;
    if( !db ) return VIKI_ENOCTX;
    if( !p || !p->vftbl ) return fail(VIKI_EINVAL, "viki_put: null assertion%s", "");
    /* EVERY slot is checked, not just canon(). akey/arank/atext are NOT NULL
    ** in the schema and INSERT OR IGNORE suppresses a NOT NULL violation just
    ** as happily as a duplicate-key one -- so a subtype returning NULL from
    ** key() stored nothing and returned VIKI_OK. A memory that quietly drops
    ** what it was told is worse than one that is absent. */
    zCanon = p->vftbl->canon(p);
    if( !zCanon ) return fail(VIKI_EINVAL, "viki_put: canon() returned null%s", "");
    if( !p->vftbl->key(p)  ) return fail(VIKI_EINVAL, "viki_put: key() returned null%s", "");
    if( !p->vftbl->rank(p) ) return fail(VIKI_EINVAL, "viki_put: rank() returned null%s", "");
    if( !p->vftbl->text(p) ) return fail(VIKI_EINVAL, "viki_put: text() returned null%s", "");
    /* IDENTITY IS THE CONTENT IN ITS POSITION, not the content alone.
    **
    ** Hashing canon() by itself made two assertions with the same text but a
    ** different key, timestamp or supersedes-link collide into ONE row -- so
    ** the second silently vanished, and "grow-only" quietly lost a write.
    ** Framing the hash with the fields that distinguish an assertion fixes
    ** that without changing the vtable: canon() still means "the content
    ** bytes", and the type stays free to decide what those are.
    **
    ** \x1f (US) separates because it cannot occur in any of these fields and
    ** therefore cannot be used to make two different tuples hash alike. */
    {
        const char *zKey = p->vftbl->key(p);
        const char *zKind = p->vftbl->type->zName;
        char *zFramed = sqlite3_mprintf("%s\x1f%s\x1f%s\x1f%s\x1f%s",
                            zKind, zKey, p->zTs ? p->zTs : "",
                            p->zSupersedes ? p->zSupersedes : "", zCanon);
        if( !zFramed ) return fail(VIKI_ENOMEM, "viki_put: out of memory%s","");
        hex_sha256(zFramed, p->zId);
        sqlite3_free(zFramed);
    }
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
    /* OR IGNORE is here for ONE case -- this exact content is already stored,
    ** which is a no-op by construction. Any other suppressed constraint means
    ** the row is missing, so verify it is actually there rather than trusting
    ** SQLITE_DONE. */
    if( sqlite3_changes(db)==0 ){
        sqlite3_stmt *chk = 0; int bHave = 0;
        if( sqlite3_prepare_v2(db, "SELECT 1 FROM viki_assert WHERE id=?1",
                               -1, &chk, 0)==SQLITE_OK ){
            sqlite3_bind_text(chk, 1, p->zId, -1, SQLITE_STATIC);
            bHave = (sqlite3_step(chk)==SQLITE_ROW);
            sqlite3_finalize(chk);
        }
        if( !bHave ) return fail(VIKI_ESQL,
            "viki_put: row was rejected and no prior copy exists (%s)",
            sqlite3_errmsg(db));
    }
    return VIKI_OK;
}

VikiStatus viki_get(const char *zId, char **pzBody){
    sqlite3 *db = db_or_null(); sqlite3_stmt *st = 0; int rc;
    if( !db ) return VIKI_ENOCTX;
    /* pzBody may be NULL -- `viki_get(zId, 0)` is the natural spelling for
    ** "does this exist?", and viki_current() already accepts it. */
    if( pzBody ) *pzBody = 0;
    if( sqlite3_prepare_v2(db, "SELECT body FROM viki_assert WHERE id=?1",
                           -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_get: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if( rc==SQLITE_ROW && pzBody ){
        const char *z = (const char*)sqlite3_column_text(st, 0);
        if( z ){
            *pzBody = strdup(z);
            if( !*pzBody ){ sqlite3_finalize(st); return fail(VIKI_ENOMEM,"viki_get: out of memory%s",""); }
        }
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
    sqlite3_stmt *st = 0, *ins = 0;
    int n = 0, rc, rcEnd;
    if( !db ) return VIKI_ENOCTX;
    if( !pOther ) return fail(VIKI_EINVAL, "viki_merge: null source%s", "");
    if( pnAdded ) *pnAdded = 0;
    /* Read the other store through ITS OWN handle and insert through ours.
    ** No ATTACH: the two connections may have different keys, a different VFS,
    ** or belong to different processes -- none of which is core's business,
    ** and ATTACH would make all three core's problem. */
    if( sqlite3_prepare_v2(pOther,
        "SELECT id,kind,akey,arank,ts,supersedes,body,atext FROM viki_assert",
        -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_merge: source: %s", sqlite3_errmsg(pOther));
    /* PREPARED ONCE, before the loop and before the savepoint: a merge into a
    ** store that was never viki_attach()ed used to prepare-fail inside the
    ** loop, break, COMMIT, and return VIKI_OK with 0 added -- a truncated
    ** union reported as a complete one, which is the one thing "union IS
    ** merge" cannot survive. */
    if( sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO viki_assert(id,kind,akey,arank,ts,supersedes,body,atext)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &ins, 0)!=SQLITE_OK ){
        sqlite3_finalize(st);
        return fail(VIKI_ESQL, "viki_merge: destination: %s", sqlite3_errmsg(db));
    }
    if( tx_begin(db, "viki_merge")!=SQLITE_OK ){
        sqlite3_finalize(st); sqlite3_finalize(ins);
        return fail(VIKI_ESQL, "viki_merge: %s", sqlite3_errmsg(db));
    }
    rc = SQLITE_OK;
    while( (rcEnd = sqlite3_step(st))==SQLITE_ROW ){
        int i;
        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
        for(i=0;i<8;i++) sqlite3_bind_value(ins, i+1, sqlite3_column_value(st, i));
        if( sqlite3_step(ins)!=SQLITE_DONE ){ rc = SQLITE_ERROR; break; }
        n += sqlite3_changes(db);
    }
    sqlite3_finalize(ins);
    sqlite3_finalize(st);
    /* The SOURCE scan's terminating code matters as much as the inserts: a
    ** read that ends in SQLITE_CORRUPT or SQLITE_BUSY mid-scan leaves a
    ** partial union that must not be reported as a whole one. */
    if( rc!=SQLITE_OK || rcEnd!=SQLITE_DONE ){
        tx_end(db, "viki_merge", 0);
        return fail(VIKI_ESQL, "viki_merge: incomplete: %s",
                    rc!=SQLITE_OK ? sqlite3_errmsg(db) : sqlite3_errmsg(pOther));
    }
    tx_end(db, "viki_merge", 1);
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
    /* '' is the reserved "unembedded" epoch. A host that supplies it, or
    ** NULL, would collide with that sentinel -- and a NULL bound into
    ** `epoch TEXT NOT NULL` was silently swallowed by INSERT OR IGNORE, so
    ** reindex looped forever over the same rows reporting VIKI_OK and 0. */
    if( pe && (!pe->zEpoch || !pe->zEpoch[0]) )
        return fail(VIKI_EINVAL, "viki_reindex: VikiEmbed.zEpoch must be non-empty%s","");
    if( pe && pe->nDim<=0 )
        return fail(VIKI_EINVAL, "viki_reindex: VikiEmbed.nDim must be positive%s","");
    if( sqlite3_prepare_v2(db,
        "SELECT id, atext FROM viki_assert WHERE id NOT IN"
        " (SELECT id FROM viki_chunk WHERE epoch=?1)", -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_reindex: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zEpoch, -1, SQLITE_STATIC);
    if( tx_begin(db, "viki_reindex")!=SQLITE_OK ){
        sqlite3_finalize(st);
        return fail(VIKI_ESQL, "viki_reindex: %s", sqlite3_errmsg(db));
    }
    while( sqlite3_step(st)==SQLITE_ROW ){
        const char *zId = (const char*)sqlite3_column_text(st, 0);
        const char *zTx = (const char*)sqlite3_column_text(st, 1);
        char zIdBuf[VIKI_ID_HEX+1];
        const char *zC; size_t nC; int ix = 0;
        if( !zId ) continue;
        if( !zTx ) zTx = "";
        snprintf(zIdBuf, sizeof(zIdBuf), "%s", zId);
        /* An empty atext yields no chunks, and without this the selection
        ** "id NOT IN (SELECT id FROM viki_chunk WHERE epoch=?)" picks it
        ** again on every run, forever. One empty chunk marks it projected. */
        if( !zTx[0] ){
            sqlite3_stmt *e = 0;
            if( sqlite3_prepare_v2(db,
                "INSERT OR IGNORE INTO viki_chunk(id,ix,epoch,text,vec)"
                " VALUES(?1,0,?2,'',NULL)", -1, &e, 0)==SQLITE_OK ){
                sqlite3_bind_text(e, 1, zIdBuf, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(e, 2, zEpoch, -1, SQLITE_STATIC);
                sqlite3_step(e); sqlite3_finalize(e);
            }
            continue;
        }
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
    tx_end(db, "viki_reindex", 1);
    if( pnChunked ) *pnChunked = nTot;
    return VIKI_OK;
}

/* ---- withdrawal ------------------------------------------------------
**
** WHICH FTS5 DELETE IDIOM IS USED MATTERS MORE THAN THE ORDER, and the
** predecessor's rule ("delete FTS first, then the chunk") is about the OTHER
** idiom. Measured here rather than inherited:
**
**   DELETE FROM f WHERE rowid=?          re-reads the content table to learn
**                                        which tokens to drop. If the content
**                                        row is already gone it succeeds,
**                                        changes nothing, and the withdrawn
**                                        text STAYS SEARCHABLE. Order is
**                                        load-bearing.
**
**   INSERT INTO f(f,rowid,text)          takes the text EXPLICITLY, so it
**        VALUES('delete',?,?)            needs no content row at all. Order
**                                        does not matter.
**
** This code uses the second, so it is immune to the trap -- but only as long
** as it keeps using it. The probe therefore asserts the PROPERTY (a direct
** FTS MATCH finds nothing after a forget) rather than the mechanism, because
** that assertion survives someone changing the idiom.
**
** Phase 1 still reads viki_chunk before phase 2 deletes it, because it needs
** the text to pass to 'delete'. That is a data dependency, not a rule.
**
** Note also that viki_ask() JOINs viki_chunk, so an orphaned FTS row can
** never become a hit -- which means an ask-based assertion CANNOT detect this
** class of bug. Measured: it stays green through the failure. */
static int drop_chunks(sqlite3 *db, const char *zSql1, const char *zSql2,
                       const char *zBind){
    sqlite3_stmt *st = 0;
    int n = 0;
    /* Phase 1: tell FTS, while viki_chunk can still be read. The special
    ** 'delete' command needs the rowid AND the text that was indexed. */
    if( sqlite3_prepare_v2(db, zSql1, -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zBind, -1, SQLITE_STATIC);
        while( sqlite3_step(st)==SQLITE_ROW ){
            sqlite3_stmt *d = 0;
            if( sqlite3_prepare_v2(db,
                "INSERT INTO viki_fts(viki_fts, rowid, text) VALUES('delete',?1,?2)",
                -1, &d, 0)==SQLITE_OK ){
                sqlite3_bind_int64(d, 1, sqlite3_column_int64(st, 0));
                sqlite3_bind_text (d, 2, (const char*)sqlite3_column_text(st,1), -1, SQLITE_TRANSIENT);
                sqlite3_step(d);
                sqlite3_finalize(d);
            }
            n++;
        }
        sqlite3_finalize(st);
    }
    /* Phase 2: now the rows may go. */
    if( sqlite3_prepare_v2(db, zSql2, -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zBind, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    return n;
}

VikiStatus viki_forget(const char *zId){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0;
    int bHad = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zId || !*zId ) return fail(VIKI_EINVAL, "viki_forget: null id%s","");
    if( sqlite3_prepare_v2(db, "SELECT 1 FROM viki_assert WHERE id=?1",
                           -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
        bHad = (sqlite3_step(st)==SQLITE_ROW);
        sqlite3_finalize(st);
    }
    if( !bHad ) return VIKI_ENOTFOUND;
    if( tx_begin(db, "viki_forget")!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_forget: %s", sqlite3_errmsg(db));
    drop_chunks(db,
        "SELECT seq, text FROM viki_chunk WHERE id=?1",
        "DELETE FROM viki_chunk WHERE id=?1", zId);
    if( sqlite3_prepare_v2(db, "DELETE FROM viki_assert WHERE id=?1",
                           -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    tx_end(db, "viki_forget", 1);
    return VIKI_OK;
}

VikiStatus viki_prune_epoch(const char *zEpoch, int *pnDropped){
    sqlite3 *db = db_or_null();
    int n;
    if( pnDropped ) *pnDropped = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zEpoch ) return fail(VIKI_EINVAL, "viki_prune_epoch: null epoch%s","");
    if( tx_begin(db, "viki_prune")!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_prune_epoch: %s", sqlite3_errmsg(db));
    n = drop_chunks(db,
        "SELECT seq, text FROM viki_chunk WHERE epoch=?1",
        "DELETE FROM viki_chunk WHERE epoch=?1", zEpoch);
    tx_end(db, "viki_prune", 1);
    if( pnDropped ) *pnDropped = n;
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

    /* Cleared FIRST, before any early return: a caller that checks *ppOut
    ** after a failure would otherwise read its own uninitialised pointer and
    ** free it. */
    if( ppOut ) *ppOut = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !ppOut ) return fail(VIKI_EINVAL, "viki_ask: null out-parameter%s","");
    if( !zQuery || !*zQuery || k<=0 ) return fail(VIKI_EINVAL, "viki_ask: empty query%s","");
    nTerm = split_terms(zQuery, azTerm, VIKI_MAX_TERMS);

    /* --- leg 1: FTS5 BM25. THE MATCH MUST BE OR-OF-TERMS: FTS5's MATCH is
    ** implicit-AND, which is simply wrong for a natural-language query -- one
    ** absent word and the whole query returns nothing. */
    for(i=0;i<nTerm;i++){
        char *zNew = sqlite3_mprintf("%z%s\"%q\"", zMatch, zMatch?" OR ":"", azTerm[i]);
        zMatch = zNew;
    }
    if( zMatch && sqlite3_prepare_v2(db,
        /* NO EPOCH FILTER. The keyword leg needs no model, so restricting it
        ** to the retained epoch made degraded mode return ZERO hits over any
        ** corpus that had been indexed WITH an embedder -- the required
        ** working path answering "nothing is known" about a store full of
        ** text. Only the vector leg is epoch-sensitive, because only vectors
        ** are. Duplicate (id,ix) across epochs is collapsed at
        ** materialisation. */
        "SELECT c.seq FROM viki_fts f JOIN viki_chunk c ON c.seq=f.rowid"
        " WHERE viki_fts MATCH ?1"
        " ORDER BY bm25(viki_fts) LIMIT ?2", -1, &st, 0)==SQLITE_OK ){
        int r = 0;
        sqlite3_bind_text(st, 1, zMatch, -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 2, VIKI_LEG_BUDGET);
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
                "SELECT count(*) FROM viki_chunk"
                " WHERE instr(lower(text), lower(?1))>0", -1, &q, 0)!=SQLITE_OK ) continue;
            sqlite3_bind_text(q, 1, azTerm[i], -1, SQLITE_STATIC);
            if( sqlite3_step(q)==SQLITE_ROW ) cnt = sqlite3_column_int(q, 0);
            sqlite3_finalize(q);
            if( cnt<=0 ) continue;
            df = (double)cnt;
            if( sqlite3_prepare_v2(db,
                "SELECT seq FROM viki_chunk"
                " WHERE instr(lower(text), lower(?1))>0", -1, &q, 0)!=SQLITE_OK ) continue;
            sqlite3_bind_text(q, 1, azTerm[i], -1, SQLITE_STATIC);
            while( sqlite3_step(q)==SQLITE_ROW ){
                sqlite3_int64 sq = sqlite3_column_int64(q, 0);
                int f = -1;
                for(j=0;j<nLit;j++) if( aLit[j].seq==sq ){ f=j; break; }
                /* THE CAP ONLY STOPS NEW ENTRIES, never the scan. The guard
                ** used to sit in the while condition, so once the array
                ** filled, every REMAINING term was abandoned mid-scan -- and
                ** terms are processed in query order, so a rare identifier
                ** typed late lost to the common words typed first. That
                ** inverts the entire point of weighting by 1/df. */
                if( f>=0 ) aLit[f].w += 1.0/df;
                else if( nLit<VIKI_POOL*4 ){ aLit[nLit].seq=sq; aLit[nLit].w=1.0/df; nLit++; }
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
    if( pe && pe->nDim>0 && pe->zEpoch && pe->zEpoch[0] ){
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
    /* Walk the WHOLE fused pool, not just the first k: dropping the epoch
    ** filter means one chunk can appear at several epochs, and collapsing
    ** those after sorting is what keeps `k` a count of DISTINCT chunks. The
    ** first occurrence is the best because the pool is sorted descending. */
    for(i=0;i<nPool && pH->n<nOut;i++){
        sqlite3_stmt *q = 0;
        if( sqlite3_prepare_v2(db, "SELECT id, ix, text FROM viki_chunk WHERE seq=?1",
                               -1, &q, 0)!=SQLITE_OK ) break;
        sqlite3_bind_int64(q, 1, pool[i].seq);
        if( sqlite3_step(q)==SQLITE_ROW ){
            int d, bDup = 0;
            const char *zIdC = (const char*)sqlite3_column_text(q, 0);
            int ixC = sqlite3_column_int(q, 1);
            for(d=0; d<pH->n; d++)
                if( pH->a[d].ix==ixC && strcmp(pH->a[d].zId, zIdC?zIdC:"")==0 ){ bDup=1; break; }
            if( bDup ){ sqlite3_finalize(q); continue; }
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
    sqlite3 *db = db_or_null();
    const char *zTail = zSql;
    int rc, bStop = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zSql ) return fail(VIKI_EINVAL, "viki_sql: null sql%s", "");
    /* EVERY statement, and EVERY error.
    **
    ** The first version passed pzTail=0 and silently dropped everything after
    ** the first statement -- so `viki_sql("INSERT ...; SELECT ...")` ran half
    ** of what it was given and said VIKI_OK. It also assigned the step result
    ** and never read it, so an error raised at STEP time rather than PREPARE
    ** time (integer overflow, a failed write, SQLITE_BUSY) also came back
    ** VIKI_OK with an empty errmsg. This is the raw rung: an agent reaching
    ** it has no other way to learn that its query did not run. */
    while( !bStop && zTail && *zTail ){
        sqlite3_stmt *st = 0;
        const char *zNext = 0;
        if( sqlite3_prepare_v2(db, zTail, -1, &st, &zNext)!=SQLITE_OK )
            return fail(VIKI_ESQL, "viki_sql: %s", sqlite3_errmsg(db));
        if( !st ){ zTail = zNext; continue; }   /* whitespace or a comment */
        while( (rc=sqlite3_step(st))==SQLITE_ROW ){
            int n = sqlite3_column_count(st), i;
            const char **az = (const char**)calloc((size_t)(n>0?n:1), sizeof(char*));
            if( !az ){ sqlite3_finalize(st); return fail(VIKI_ENOMEM,"viki_sql: out of memory%s",""); }
            for(i=0;i<n;i++) az[i] = (const char*)sqlite3_column_text(st, i);
            if( x && x(pApp, n, az) ) bStop = 1;
            free(az);
            if( bStop ) break;
        }
        sqlite3_finalize(st);
        if( !bStop && rc!=SQLITE_DONE && rc!=SQLITE_ROW )
            return fail(VIKI_ESQL, "viki_sql: %s", sqlite3_errmsg(db));
        zTail = zNext;
    }
    return VIKI_OK;
}
