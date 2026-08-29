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
RETAIN_DEFINE(VikiIdentity);

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
  "  id         TEXT PRIMARY KEY,"   /* sha256 hex of the framed canon()     */
  "  kind       TEXT NOT NULL,"
  "  akey       TEXT NOT NULL,"      /* what it competes on                  */
  "  arank      TEXT NOT NULL,"      /* LEXICAL sort key; max wins           */
  "  ts         TEXT NOT NULL,"
  "  supersedes TEXT,"
  "  body       TEXT NOT NULL,"
  "  atext      TEXT NOT NULL"       /* what gets chunked -- THE ONLY COPY   */
  ") WITHOUT ROWID;"
  "CREATE INDEX IF NOT EXISTS viki_assert_key ON viki_assert(akey, arank);"
  "CREATE INDEX IF NOT EXISTS viki_assert_sup ON viki_assert(supersedes);"

  /* A CHUNK IS A RANGE OVER AN ASSERTION, AND STORES NO TEXT.
  **
  ** The predecessor keyed chunks on (content_hash, model_id, chunk_ix) and
  ** stored the text per chunk. chunk_ix is an ORDINAL whose meaning depends
  ** on the parameters that produced it, so two peers with different chunk
  ** sizes wrote rows that AGREED ON THE KEY and DISAGREED ON THE TEXT, and
  ** INSERT OR IGNORE silently double-indexed a document. The fix there was to
  ** fold the chunking into the model id.
  **
  ** Keyed on (id, lo, hi, model) that collision is UNREPRESENTABLE: different
  ** boundaries produce different keys by construction, because the extent is
  ** IN the key rather than implied by it. Three things follow:
  **
  **   - `model` means the MODEL again, as D-11 originally said. `chunking` is
  **     provenance -- which policy drew these lines -- not identity.
  **   - Chunk rows become IMMUTABLE ON A CONTENT KEY, so they are grow-only
  **     and union-mergeable, the same class as assertions.
  **   - OVERLAP IS FREE. Overlapping ranges are just ranges; the predecessor
  **     duplicated the overlapped text into two rows.
  **
  ** And the reason that matters most: a device can store an assertion with NO
  ** chunking decision at all, and a device with a model can add ranges over it
  ** later -- a SECOND set with different boundaries without disturbing the
  ** first. Compute-once-share-many extends from embedding to chunking. */
  "CREATE TABLE IF NOT EXISTS viki_chunk("
  "  seq      INTEGER PRIMARY KEY,"
  "  id       TEXT NOT NULL,"        /* the assertion this ranges over       */
  "  lo       INTEGER NOT NULL,"     /* byte offset, inclusive               */
  "  hi       INTEGER NOT NULL,"     /* byte offset, exclusive               */
  "  model    TEXT NOT NULL,"        /* '' when unembedded                   */
  "  chunking TEXT NOT NULL,"        /* provenance, NOT identity             */
  "  vec      BLOB,"
  "  UNIQUE(id, lo, hi, model)"
  ");"
  "CREATE INDEX IF NOT EXISTS viki_chunk_id ON viki_chunk(id);"

  /* SIGNATURES ARE ROWS, NOT A COLUMN. Several identities may sign one
  ** assertion, so countersigning is union-merge like everything else -- and a
  ** signed copy can never be shadowed by an unsigned one, which it would be if
  ** the signature rode on the assertion and INSERT OR IGNORE kept whichever
  ** arrived first. Grow-only on (id, signer), the same class as assertions. */
  "CREATE TABLE IF NOT EXISTS viki_sig("
  "  id     TEXT NOT NULL,"        /* the assertion signed                  */
  "  signer TEXT NOT NULL,"        /* the signing identity's assertion id   */
  "  sig    BLOB NOT NULL,"
  "  PRIMARY KEY(id, signer)"
  ") WITHOUT ROWID;"

  /* The payload of a blob assertion, kept OUT of viki_assert so that a 23 MB
  ** model does not sit in the row every resolve, count and merge scans. The
  ** assertion carries the description; this carries the bytes. */
  "CREATE TABLE IF NOT EXISTS viki_blob("
  "  id    TEXT PRIMARY KEY REFERENCES viki_assert(id),"
  "  bytes BLOB NOT NULL"
  ") WITHOUT ROWID;"

  /* The text of a chunk is COMPUTED, never stored. FTS5 accepts a view as its
  ** external content (verified), so the index is built over ranges and
  ** `INSERT INTO viki_fts(viki_fts) VALUES('rebuild')` regenerates it from
  ** them. substr() is 1-based; lo is 0-based. */
  "CREATE VIEW IF NOT EXISTS viki_chunk_text(seq, text) AS"
  "  SELECT c.seq, substr(a.atext, c.lo+1, c.hi-c.lo)"
  "    FROM viki_chunk c JOIN viki_assert a ON a.id = c.id;"
  "CREATE VIRTUAL TABLE IF NOT EXISTS viki_fts"
  " USING fts5(text, content='viki_chunk_text', content_rowid='seq');";

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

/* ---- identity and signing --------------------------------------------
** Core links no crypto. Everything here is bookkeeping around two host
** callbacks, which is what makes the mechanism pluggable: a thumbprint on a
** laptop and an agent key written into your own diary reach this code the
** same way. */
static const VikiType vikiIdentityType = { "identity", 0, 0 };
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;
    const char *zName;
    const char *zCanon;
} IdAssert;
static const char *idKey  (const VikiAssert *p){ return ((const IdAssert*)p)->zCanon; }
static const char *idRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *idText (const VikiAssert *p){ return ((const IdAssert*)p)->zName; }
static const char *idCanon(const VikiAssert *p){ return ((const IdAssert*)p)->zCanon; }
static const struct VikiAssertVftbl vikiIdentityVftbl = {
    &vikiIdentityType, idKey, idRank, idText, idCanon
};

VikiStatus viki_identity_put(const char *zName, const char *zPubKey, char *zIdOut){
    IdAssert a;
    char *zCanon;
    VikiStatus rc;
    if( !db_or_null() ) return VIKI_ENOCTX;
    if( !zName || !zPubKey || !*zPubKey )
        return fail(VIKI_EINVAL, "viki_identity_put: name and public key required%s","");
    memset(&a, 0, sizeof a);
    a.vftbl = &vikiIdentityVftbl;
    a.zName = zName;
    /* Identity is (name, public key). The SAME key under a different name is a
    ** different assertion, because the name is part of the claim -- and a
    ** rename must be visible rather than silent. */
    zCanon = sqlite3_mprintf("%s\x1f%s", zPubKey, zName);
    if( !zCanon ) return fail(VIKI_ENOMEM, "viki_identity_put: out of memory%s","");
    a.zCanon = zCanon;
    rc = viki_put((VikiAssert*)&a);
    sqlite3_free(zCanon);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, a.zId, VIKI_ID_HEX+1);
    return rc;
}

/* The public key of a recorded identity, from its canon (pubkey \x1f name). */
static char *identity_pubkey(sqlite3 *db, const char *zSigner){
    sqlite3_stmt *st = 0; char *z = 0;
    if( sqlite3_prepare_v2(db,
        "SELECT body FROM viki_assert WHERE id=?1 AND kind='identity'",
        -1, &st, 0)!=SQLITE_OK ) return 0;
    sqlite3_bind_text(st, 1, zSigner, -1, SQLITE_STATIC);
    if( sqlite3_step(st)==SQLITE_ROW ){
        const char *zBody = (const char*)sqlite3_column_text(st, 0);
        const char *zSep = zBody ? strchr(zBody, '\x1f') : 0;
        if( zSep ){
            size_t n = (size_t)(zSep - zBody);
            z = (char*)malloc(n+1);
            if( z ){ memcpy(z, zBody, n); z[n] = 0; }
        }
    }
    sqlite3_finalize(st);
    return z;
}

/* Adds the retained identity's signature for zId. Declining is not an error:
** a cancelled thumbprint or a locked keychain stores the assertion UNSIGNED
** rather than losing it. */
static void sign_if_able(sqlite3 *db, const char *zId){
    const VikiIdentity *pI;
    unsigned char aSig[VIKI_SIG_MAX];
    int nSig = (int)sizeof aSig;
    sqlite3_stmt *st = 0;
    if( !RETAINED(VikiIdentity) ) return;
    pI = RECALL(VikiIdentity);
    if( !pI || !pI->xSign || !pI->zSigner ) return;
    if( pI->xSign(pI->pApp, zId, aSig, &nSig)!=0 ) return;
    if( nSig<=0 || nSig>(int)sizeof aSig ) return;
    if( sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO viki_sig(id,signer,sig) VALUES(?1,?2,?3)",
        -1, &st, 0)!=SQLITE_OK ) return;
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pI->zSigner, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 3, aSig, nSig, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

VikiStatus viki_countersign(const char *zId){
    sqlite3 *db = db_or_null();
    if( !db ) return VIKI_ENOCTX;
    if( !RETAINED(VikiIdentity) )
        return fail(VIKI_EINVAL, "viki_countersign: no identity retained%s","");
    if( viki_get(zId, 0)!=VIKI_OK ) return VIKI_ENOTFOUND;
    sign_if_able(db, zId);
    return VIKI_OK;
}

VikiStatus viki_signed(const char *zId, VikiSigState *pState, char *zSignerOut){
    sqlite3 *db = db_or_null();
    const VikiIdentity *pI = RETAINED(VikiIdentity) ? RECALL(VikiIdentity) : 0;
    sqlite3_stmt *st = 0;
    VikiSigState best = VIKI_SIG_NONE;
    char zBest[VIKI_ID_HEX+1];
    zBest[0] = 0;
    if( pState ) *pState = VIKI_SIG_NONE;
    if( zSignerOut ) zSignerOut[0] = 0;
    if( !db ) return VIKI_ENOCTX;
    if( sqlite3_prepare_v2(db, "SELECT signer, sig FROM viki_sig WHERE id=?1",
                           -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_signed: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    while( sqlite3_step(st)==SQLITE_ROW ){
        const char *zSigner = (const char*)sqlite3_column_text(st, 0);
        const unsigned char *aSig = (const unsigned char*)sqlite3_column_blob(st, 1);
        int nSig = sqlite3_column_bytes(st, 1);
        char *zPub = identity_pubkey(db, zSigner);
        VikiSigState thisOne;
        if( !zPub ){
            /* Signed by somebody this diary has never heard of. That is not a
            ** failure -- it is a fact about coverage, and it is exactly what a
            ** peer sees before the signer's identity assertion reaches it. */
            thisOne = VIKI_SIG_UNKNOWN;
        }else if( pI && pI->xVerify
               && pI->xVerify(pI->pApp, zPub, zId, aSig, nSig)==0 ){
            thisOne = VIKI_SIG_OK;
        }else{
            thisOne = VIKI_SIG_BAD;
        }
        free(zPub);
        /* strongest wins: OK > BAD > UNKNOWN > NONE */
        if( thisOne==VIKI_SIG_OK
         || (thisOne==VIKI_SIG_BAD && best!=VIKI_SIG_OK)
         || (thisOne==VIKI_SIG_UNKNOWN && best==VIKI_SIG_NONE) ){
            best = thisOne;
            if( zSigner ) snprintf(zBest, sizeof zBest, "%s", zSigner);
        }
    }
    sqlite3_finalize(st);
    if( pState ) *pState = best;
    if( zSignerOut ) snprintf(zSignerOut, VIKI_ID_HEX+1, "%s", zBest);
    return VIKI_OK;
}

/* ---- blobs ------------------------------------------------------------ */
static const VikiType vikiBlobType = { "blob", 0, 0 };
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;
    const char *zDesc;
    const char *zCanon;
} BlobAssert;
static const char *blKey  (const VikiAssert *p){ return ((const BlobAssert*)p)->zCanon; }
static const char *blRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
/* THE DESCRIPTION IS WHAT GETS EMBEDDED. Chunking the coefficients would
** produce ranges of noise; chunking the description produces the one range
** that can answer "which model do I have?". */
static const char *blText (const VikiAssert *p){ return ((const BlobAssert*)p)->zDesc; }
static const char *blCanon(const VikiAssert *p){ return ((const BlobAssert*)p)->zCanon; }
static const struct VikiAssertVftbl vikiBlobVftbl = {
    &vikiBlobType, blKey, blRank, blText, blCanon
};

VikiStatus viki_blob_put(const char *zDesc, const char *zContentHash,
                         const void *pBytes, sqlite3_int64 nBytes,
                         char *zIdOut){
    sqlite3 *db = db_or_null();
    BlobAssert b;
    sqlite3_stmt *st = 0;
    char *zCanon;
    VikiStatus rc;
    if( !db ) return VIKI_ENOCTX;
    if( !zDesc || !zContentHash || !*zContentHash )
        return fail(VIKI_EINVAL, "viki_blob_put: description and content hash are required%s","");
    if( nBytes<0 || (nBytes>0 && !pBytes) )
        return fail(VIKI_EINVAL, "viki_blob_put: bad payload%s","");
    memset(&b, 0, sizeof b);
    b.vftbl = &vikiBlobVftbl;
    b.zDesc = zDesc;
    /* Identity is the CALLER'S hash plus the description: two peers holding
    ** the same model under different descriptions are two assertions about
    ** one payload, which is correct -- the description is the claim. */
    zCanon = sqlite3_mprintf("%s\x1f%s", zContentHash, zDesc);
    if( !zCanon ) return fail(VIKI_ENOMEM, "viki_blob_put: out of memory%s","");
    b.zCanon = zCanon;
    rc = viki_put((VikiAssert*)&b);
    sqlite3_free(zCanon);
    if( rc!=VIKI_OK ) return rc;
    if( zIdOut ) memcpy(zIdOut, b.zId, VIKI_ID_HEX+1);
    if( sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO viki_blob(id,bytes) VALUES(?1,?2)",
                           -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_blob_put: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, b.zId, -1, SQLITE_STATIC);
    sqlite3_bind_blob64(st, 2, pBytes, (sqlite3_uint64)nBytes, SQLITE_STATIC);
    if( sqlite3_step(st)!=SQLITE_DONE ){
        sqlite3_finalize(st);
        return fail(VIKI_ESQL, "viki_blob_put: %s", sqlite3_errmsg(db));
    }
    sqlite3_finalize(st);
    return VIKI_OK;
}

/* The statement is held so the pointer stays valid: sqlite3_column_blob()
** owns the memory until the statement is stepped or finalized, so handing it
** back requires keeping exactly one alive. That is why the contract says
** "until the next core call on this store". */
static VIKI_TLS sqlite3_stmt *g_pBlobHold = 0;

VikiStatus viki_blob_get(const char *zId, const void **ppBytes, sqlite3_int64 *pnBytes){
    sqlite3 *db = db_or_null();
    int rc;
    if( ppBytes ) *ppBytes = 0;
    if( pnBytes ) *pnBytes = 0;
    if( !db ) return VIKI_ENOCTX;
    if( g_pBlobHold ){ sqlite3_finalize(g_pBlobHold); g_pBlobHold = 0; }
    if( sqlite3_prepare_v2(db, "SELECT bytes FROM viki_blob WHERE id=?1",
                           -1, &g_pBlobHold, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_blob_get: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(g_pBlobHold, 1, zId, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(g_pBlobHold);
    if( rc!=SQLITE_ROW ){
        sqlite3_finalize(g_pBlobHold); g_pBlobHold = 0;
        return VIKI_ENOTFOUND;
    }
    if( ppBytes ) *ppBytes = sqlite3_column_blob(g_pBlobHold, 0);
    if( pnBytes ) *pnBytes = sqlite3_column_bytes(g_pBlobHold, 0);
    return VIKI_OK;
}

/* ---- observability ---------------------------------------------------
**
** Events are BUFFERED and flushed on COMMIT. Core runs inside SAVEPOINTs on a
** connection it does not own, so the host may roll back after us; announcing
** a change during the operation would tell a listener about something that
** never happened, and a listener that wrote to a UI or told a peer cannot
** take that back. SQLite's commit/rollback hooks are what make "never an
** uncommitted change, never a missed committed one" true rather than hoped.
**
** State is per-CONNECTION, held in a small registry rather than a global, so
** two stores in one process notify independently. */
#define VIKI_MAX_WATCH 8
#define VIKI_MAX_QUEUE 256

typedef struct {
    VikiEventKind kind;
    char zId[VIKI_ID_HEX+1];
    char zKey[256];
    char zOther[VIKI_ID_HEX+1];
    int  n;
} QEvent;

typedef struct WatchSet WatchSet;
struct WatchSet {
    sqlite3      *db;
    WatchSet     *pNext;
    viki_watch_fn axFn[VIKI_MAX_WATCH];
    void         *apApp[VIKI_MAX_WATCH];
    int           anTok[VIKI_MAX_WATCH];
    int           nNextTok;
    QEvent        aQ[VIKI_MAX_QUEUE];
    int           nQ;
    int           bOverflow;   /* more events than the queue holds          */
    int           bDispatching;/* reentrancy guard -- see viki_watch()      */
};
static VIKI_TLS WatchSet *g_pWatch = 0;

static WatchSet *watch_find(sqlite3 *db, int bCreate){
    WatchSet *p;
    for(p=g_pWatch; p; p=p->pNext) if( p->db==db ) return p;
    if( !bCreate ) return 0;
    p = (WatchSet*)calloc(1, sizeof(*p));
    if( !p ) return 0;
    p->db = db; p->nNextTok = 1;
    p->pNext = g_pWatch; g_pWatch = p;
    return p;
}

static void watch_flush(void *pArg){
    WatchSet *p = (WatchSet*)pArg;
    int i, w;
    if( !p || p->bDispatching ) return;
    p->bDispatching = 1;
    for(i=0;i<p->nQ;i++){
        VikiEvent2 ev;
        ev.kind   = p->aQ[i].kind;
        ev.zId    = p->aQ[i].zId[0]    ? p->aQ[i].zId    : 0;
        ev.zKey   = p->aQ[i].zKey[0]   ? p->aQ[i].zKey   : 0;
        ev.zOther = p->aQ[i].zOther[0] ? p->aQ[i].zOther : 0;
        ev.n      = p->aQ[i].n;
        for(w=0;w<VIKI_MAX_WATCH;w++)
            if( p->axFn[w] ) p->axFn[w](p->apApp[w], &ev);
    }
    p->nQ = 0; p->bOverflow = 0;
    p->bDispatching = 0;
}
static int on_commit(void *pArg){ watch_flush(pArg); return 0; }
/* DISCARDED, not flushed: the host rolled back, so none of it happened. */
static void on_rollback(void *pArg){
    WatchSet *p = (WatchSet*)pArg;
    if( p ){ p->nQ = 0; p->bOverflow = 0; }
}

static void emit(sqlite3 *db, VikiEventKind k, const char *zId,
                 const char *zKey, const char *zOther, int n){
    WatchSet *p = watch_find(db, 0);
    QEvent *e;
    if( !p || p->nQ>=VIKI_MAX_QUEUE ){ if( p ) p->bOverflow = 1; return; }
    e = &p->aQ[p->nQ++];
    memset(e, 0, sizeof(*e));
    e->kind = k; e->n = n;
    if( zId )    snprintf(e->zId,    sizeof(e->zId),    "%s", zId);
    if( zKey )   snprintf(e->zKey,   sizeof(e->zKey),   "%s", zKey);
    if( zOther ) snprintf(e->zOther, sizeof(e->zOther), "%s", zOther);
    /* NOT INSIDE A HOST TRANSACTION: our savepoint already released and
    ** SQLite auto-committed, so the commit hook will not fire again. Flush
    ** now rather than holding events until some unrelated later commit. */
    if( sqlite3_get_autocommit(db) ) watch_flush(p);
}

VikiStatus viki_watch(sqlite3 *db, viki_watch_fn x, void *pApp, int *pnToken){
    WatchSet *p;
    int i;
    if( !db || !x ) return VIKI_EINVAL;
    p = watch_find(db, 1);
    if( !p ) return VIKI_ENOMEM;
    for(i=0;i<VIKI_MAX_WATCH;i++) if( !p->axFn[i] ) break;
    if( i==VIKI_MAX_WATCH ) return VIKI_EBUSY;
    p->axFn[i] = x; p->apApp[i] = pApp; p->anTok[i] = p->nNextTok++;
    if( pnToken ) *pnToken = p->anTok[i];
    sqlite3_commit_hook(db, on_commit, p);
    sqlite3_rollback_hook(db, on_rollback, p);
    return VIKI_OK;
}

VikiStatus viki_unwatch(sqlite3 *db, int nToken){
    WatchSet *p = watch_find(db, 0);
    int i;
    if( !p ) return VIKI_ENOTFOUND;
    for(i=0;i<VIKI_MAX_WATCH;i++)
        if( p->axFn[i] && p->anTok[i]==nToken ){ p->axFn[i]=0; p->apApp[i]=0; return VIKI_OK; }
    return VIKI_ENOTFOUND;
}

/* Refuses a mutation attempted from inside a listener. Without this a
** listener that noted something would append to the queue it is being
** dispatched from. */
static int watch_busy(sqlite3 *db){
    WatchSet *p = watch_find(db, 0);
    return p && p->bDispatching;
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
    if( watch_busy(db) ) return fail(VIKI_EBUSY, "viki_put: called from a listener%s","");
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
        return VIKI_OK;                 /* already stored: not a new event */
    }
    sign_if_able(db, p->zId);
    emit(db, p->zSupersedes ? VIKI_EV_SUPERSEDED : VIKI_EV_PUT,
         p->zId, p->vftbl->key(p), p->zSupersedes, 1);
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
    /* SIGNATURES MERGE TOO. They are grow-only rows on (id, signer), so union
    ** is merge for them exactly as for assertions -- and without this a peer
    ** would receive a statement while losing the evidence of who stood behind
    ** it, which is the half signing exists to supply. */
    {   sqlite3_stmt *q = 0, *w = 0;
        if( sqlite3_prepare_v2(pOther, "SELECT id,signer,sig FROM viki_sig",
                               -1, &q, 0)==SQLITE_OK ){
            if( sqlite3_prepare_v2(db,
                "INSERT OR IGNORE INTO viki_sig(id,signer,sig) VALUES(?1,?2,?3)",
                -1, &w, 0)==SQLITE_OK ){
                while( sqlite3_step(q)==SQLITE_ROW ){
                    int c;
                    sqlite3_reset(w); sqlite3_clear_bindings(w);
                    for(c=0;c<3;c++) sqlite3_bind_value(w, c+1, sqlite3_column_value(q, c));
                    sqlite3_step(w);
                }
                sqlite3_finalize(w);
            }
            sqlite3_finalize(q);
        }
    }
    tx_end(db, "viki_merge", 1);
    emit(db, VIKI_EV_MERGED, 0, 0, 0, n);
    if( pnAdded ) *pnAdded = n;
    return VIKI_OK;
}

/* ---- projection: ranges, FTS, vectors --------------------------------
** Chunk geometry is PORTED, not re-chosen: 40 lines with 10 overlapping was
** measured in (recall@1 0.256 -> 0.349, MRR 0.381 -> 0.424 at +26% chunks),
** and 20 cost +77% chunks for nothing. */
const VikiChunking vikiChunkDefault = { "l40o10", 40, 10 };

/* Byte range of chunk ix, or 0 when ix is past the end. Ranges are computed
** over the assertion's text and only the OFFSETS are stored. */
static int range_at(const char *zText, const VikiChunking *pCh, int ix,
                    int *pLo, int *pHi){
    int stride = pCh->nLines - pCh->nOverlap;
    const char *p = zText;
    int line = 0, want, n = 0;
    const char *zStart;
    if( stride < 1 ) stride = 1;          /* clamp: an override must not hang */
    want = ix * stride;
    if( ix < 0 ) return 0;
    while( line<want && *p ){ if( *p=='\n' ) line++; p++; }
    if( !*p && want>0 ) return 0;
    zStart = p;
    while( *p && n<pCh->nLines ){ if( *p=='\n' ) n++; p++; }
    if( p==zStart ) return 0;
    *pLo = (int)(zStart - zText);
    *pHi = (int)(p - zText);
    return 1;
}

VikiStatus viki_reindex(const VikiChunking *pCh, int *pnChunked){
    sqlite3 *db = db_or_null();
    const VikiEmbed *pe = RETAINED(VikiEmbed) ? RECALL(VikiEmbed) : 0;
    const char *zModel = pe ? pe->zModel : "";
    sqlite3_stmt *st = 0; int nTot = 0;
    if( !db ) return VIKI_ENOCTX;
    if( pnChunked ) *pnChunked = 0;
    if( !pCh ) pCh = &vikiChunkDefault;
    if( pCh->nLines<1 || pCh->nOverlap<0 || pCh->nOverlap>=pCh->nLines || !pCh->zName )
        return fail(VIKI_EINVAL, "viki_reindex: bad chunking policy%s","");
    /* '' is the reserved "unembedded" model. A host supplying it, or NULL,
    ** would collide with that sentinel -- and NULL bound into a NOT NULL
    ** column was swallowed by INSERT OR IGNORE, so reindex looped forever
    ** over the same rows reporting success. */
    if( pe && (!pe->zModel || !pe->zModel[0]) )
        return fail(VIKI_EINVAL, "viki_reindex: VikiEmbed.zModel must be non-empty%s","");
    if( pe && pe->nDim<=0 )
        return fail(VIKI_EINVAL, "viki_reindex: VikiEmbed.nDim must be positive%s","");

    /* Selects assertions with no ranges AT THIS (model, chunking). A second
    ** policy therefore adds to the first rather than replacing it. */
    if( sqlite3_prepare_v2(db,
        "SELECT a.id, a.atext FROM viki_assert a WHERE NOT EXISTS("
        "  SELECT 1 FROM viki_chunk c WHERE c.id=a.id AND c.model=?1 AND c.chunking=?2)",
        -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_reindex: %s", sqlite3_errmsg(db));
    sqlite3_bind_text(st, 1, zModel,     -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pCh->zName, -1, SQLITE_STATIC);
    if( tx_begin(db, "viki_reindex")!=SQLITE_OK ){
        sqlite3_finalize(st);
        return fail(VIKI_ESQL, "viki_reindex: %s", sqlite3_errmsg(db));
    }
    while( sqlite3_step(st)==SQLITE_ROW ){
        const char *zId = (const char*)sqlite3_column_text(st, 0);
        const char *zTx = (const char*)sqlite3_column_text(st, 1);
        char zIdBuf[VIKI_ID_HEX+1];
        int ix = 0, lo = 0, hi = 0;
        if( !zId ) continue;
        if( !zTx ) zTx = "";
        snprintf(zIdBuf, sizeof(zIdBuf), "%s", zId);
        /* An empty atext yields no ranges; a zero-length one marks it done so
        ** the selection above does not pick it again on every run, forever. */
        if( !zTx[0] ){
            sqlite3_stmt *e = 0;
            if( sqlite3_prepare_v2(db,
                "INSERT OR IGNORE INTO viki_chunk(id,lo,hi,model,chunking,vec)"
                " VALUES(?1,0,0,?2,?3,NULL)", -1, &e, 0)==SQLITE_OK ){
                sqlite3_bind_text(e, 1, zIdBuf, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(e, 2, zModel, -1, SQLITE_STATIC);
                sqlite3_bind_text(e, 3, pCh->zName, -1, SQLITE_STATIC);
                sqlite3_step(e); sqlite3_finalize(e);
            }
            continue;
        }
        while( range_at(zTx, pCh, ix, &lo, &hi) ){
            sqlite3_stmt *ins = 0;
            float *aVec = 0;
            if( pe ){
                char *zTmp = (char*)malloc((size_t)(hi-lo)+1);
                if( zTmp ){
                    memcpy(zTmp, zTx+lo, (size_t)(hi-lo)); zTmp[hi-lo] = 0;
                    aVec = (float*)calloc((size_t)pe->nDim, sizeof(float));
                    if( aVec && pe->xEmbed(pe->pApp, zTmp, aVec, pe->nDim)!=0 ){
                        free(aVec); aVec = 0;   /* declined: keyword only */
                    }
                    free(zTmp);
                }
            }
            if( sqlite3_prepare_v2(db,
                "INSERT OR IGNORE INTO viki_chunk(id,lo,hi,model,chunking,vec)"
                " VALUES(?1,?2,?3,?4,?5,?6)", -1, &ins, 0)==SQLITE_OK ){
                sqlite3_bind_text(ins, 1, zIdBuf, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (ins, 2, lo);
                sqlite3_bind_int (ins, 3, hi);
                sqlite3_bind_text(ins, 4, zModel, -1, SQLITE_STATIC);
                sqlite3_bind_text(ins, 5, pCh->zName, -1, SQLITE_STATIC);
                if( aVec ) sqlite3_bind_blob(ins, 6, aVec,
                             (int)((size_t)pe->nDim*sizeof(float)), SQLITE_TRANSIENT);
                else       sqlite3_bind_null(ins, 6);
                if( sqlite3_step(ins)==SQLITE_DONE && sqlite3_changes(db)>0 ){
                    sqlite3_stmt *f = 0;
                    sqlite3_int64 seq = sqlite3_last_insert_rowid(db);
                    /* External content over a VIEW: FTS is told the text
                    ** explicitly at insert, and can regenerate the whole index
                    ** from the ranges with ('rebuild') if it ever drifts. */
                    if( sqlite3_prepare_v2(db,
                        "INSERT INTO viki_fts(rowid, text) VALUES(?1,?2)",
                        -1, &f, 0)==SQLITE_OK ){
                        sqlite3_bind_int64(f, 1, seq);
                        sqlite3_bind_text (f, 2, zTx+lo, hi-lo, SQLITE_TRANSIENT);
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
    emit(db, VIKI_EV_PROJECTED, 0, 0, 0, nTot);
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
        "SELECT seq, text FROM viki_chunk_text WHERE seq IN"
        " (SELECT seq FROM viki_chunk WHERE id=?1)",
        "DELETE FROM viki_chunk WHERE id=?1", zId);
    if( sqlite3_prepare_v2(db, "DELETE FROM viki_sig WHERE id=?1",
                           -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
        sqlite3_step(st); sqlite3_finalize(st); st = 0;
    }
    if( sqlite3_prepare_v2(db, "DELETE FROM viki_assert WHERE id=?1",
                           -1, &st, 0)==SQLITE_OK ){
        sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    tx_end(db, "viki_forget", 1);
    emit(db, VIKI_EV_FORGOTTEN, zId, 0, 0, 1);
    return VIKI_OK;
}

VikiStatus viki_prune_model(const char *zModel, int *pnDropped){
    sqlite3 *db = db_or_null();
    int n;
    if( pnDropped ) *pnDropped = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zModel ) return fail(VIKI_EINVAL, "viki_prune_model: null model%s","");
    if( tx_begin(db, "viki_prune")!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_prune_model: %s", sqlite3_errmsg(db));
    n = drop_chunks(db,
        "SELECT seq, text FROM viki_chunk_text WHERE seq IN"
        " (SELECT seq FROM viki_chunk WHERE model=?1)",
        "DELETE FROM viki_chunk WHERE model=?1", zModel);
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
    const char *zModel = pe ? pe->zModel : "";
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
        ** to one model made degraded mode return ZERO hits over any corpus
        ** indexed WITH an embedder -- the required working path answering
        ** "nothing is known" about a store full of text. Only the vector leg
        ** is model-sensitive, because only vectors are. */
        "SELECT f.rowid FROM viki_fts f"
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
                "SELECT count(*) FROM viki_chunk_text"
                " WHERE instr(lower(text), lower(?1))>0", -1, &q, 0)!=SQLITE_OK ) continue;
            sqlite3_bind_text(q, 1, azTerm[i], -1, SQLITE_STATIC);
            if( sqlite3_step(q)==SQLITE_ROW ) cnt = sqlite3_column_int(q, 0);
            sqlite3_finalize(q);
            if( cnt<=0 ) continue;
            df = (double)cnt;
            if( sqlite3_prepare_v2(db,
                "SELECT seq FROM viki_chunk_text"
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
    if( pe && pe->nDim>0 && pe->zModel && pe->zModel[0] ){
        float *aQ = (float*)calloc((size_t)pe->nDim, sizeof(float));
        if( aQ && pe->xEmbed(pe->pApp, zQuery, aQ, pe->nDim)==0 ){
            if( sqlite3_prepare_v2(db,
                /* ACROSS EVERY CHUNKING AT THIS MODEL, which is the point of
                ** ranges: a fine policy finds the precise sentence, a coarse
                ** one keeps enough context to be judged, and RRF decides. The
                ** budget is per-CHUNKING so one policy cannot crowd out the
                ** others -- without that partition, the finest chunking wins
                ** every slot simply by having the most rows. */
                "SELECT seq FROM ("
                "  SELECT seq, chunking, viki_cos(vec, ?2) AS cs,"
                "         row_number() OVER (PARTITION BY chunking"
                "                            ORDER BY viki_cos(vec, ?2) DESC) AS rn"
                "    FROM viki_chunk WHERE model=?1 AND vec IS NOT NULL"
                ") WHERE rn <= ?3 ORDER BY cs DESC", -1, &st, 0)==SQLITE_OK ){
                int r = 0;
                sqlite3_bind_text(st, 1, zModel, -1, SQLITE_STATIC);
                sqlite3_bind_blob(st, 2, aQ, (int)((size_t)pe->nDim*sizeof(float)), SQLITE_STATIC);
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
        if( sqlite3_prepare_v2(db,
              "SELECT c.id, c.lo, c.hi, t.text, c.chunking FROM viki_chunk c"
              " JOIN viki_chunk_text t ON t.seq=c.seq WHERE c.seq=?1",
                               -1, &q, 0)!=SQLITE_OK ) break;
        sqlite3_bind_int64(q, 1, pool[i].seq);
        if( sqlite3_step(q)==SQLITE_ROW ){
            /* Collapse on the RANGE, not an ordinal: the same bytes reached
            ** through two chunkings are one answer, and two overlapping
            ** ranges that merely intersect are not. */
            int d, bDup = 0;
            const char *zIdC = (const char*)sqlite3_column_text(q, 0);
            int loC = sqlite3_column_int(q, 1), hiC = sqlite3_column_int(q, 2);
            for(d=0; d<pH->n; d++)
                if( pH->a[d].lo==loC && pH->a[d].hi==hiC
                 && strcmp(pH->a[d].zId, zIdC?zIdC:"")==0 ){ bDup=1; break; }
            if( bDup ){ sqlite3_finalize(q); continue; }
            const char *zT = (const char*)sqlite3_column_text(q, 3);
            const char *zCh = (const char*)sqlite3_column_text(q, 4);
            snprintf(pH->a[pH->n].zId, VIKI_ID_HEX+1, "%s", sqlite3_column_text(q,0));
            pH->a[pH->n].lo    = loC;
            pH->a[pH->n].hi    = hiC;
            pH->a[pH->n].score = pool[i].score;
            pH->a[pH->n].zText = zT ? strdup(zT) : 0;
            snprintf(pH->a[pH->n].zChunking, sizeof(pH->a[pH->n].zChunking), "%s", zCh?zCh:"");
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

/* ---- reading without knowing the schema ------------------------------
** These exist so a host never needs to name a table. core's own probe is
** written against them, which is what keeps that claim honest: if the probe
** cannot be expressed here, the API has a gap rather than the probe having
** an excuse. */
VikiStatus viki_count(VikiCountWhat what, const char *zFilter, int *pn){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0;
    const char *zSql = 0;
    int rc;
    if( pn ) *pn = 0;
    if( !db ) return VIKI_ENOCTX;
    switch( what ){
      case VIKI_N_ASSERT:
        zSql = "SELECT count(*) FROM viki_assert WHERE (?1 IS NULL OR kind=?1)"; break;
      case VIKI_N_CURRENT:
        /* THE RESOLVED WINNERS -- one per key -- which is what viki_current()
        ** returns. An earlier version counted "assertions nothing explicitly
        ** supersedes", and those are NOT the same thing: an update that wins
        ** by RANK (a higher RFC 5546 sequence, say) sets no supersedes link,
        ** so the older assertion is still unsuperseded while no longer being
        ** current. Two notions with one name is exactly the kind of drift
        ** having a single resolver was supposed to prevent. */
        zSql = "SELECT count(DISTINCT akey) FROM viki_assert a"
               " WHERE (?1 IS NULL OR a.kind=?1)"
               "   AND NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"; break;
      case VIKI_N_RANGE:
        zSql = "SELECT count(*) FROM viki_chunk WHERE (?1 IS NULL OR model=?1)"; break;
      case VIKI_N_VECTOR:
        zSql = "SELECT count(*) FROM viki_chunk WHERE vec IS NOT NULL"
               " AND (?1 IS NULL OR model=?1)"; break;
      case VIKI_N_CHUNKING:
        zSql = "SELECT count(DISTINCT chunking) FROM viki_chunk"
               " WHERE (?1 IS NULL OR model=?1)"; break;
      case VIKI_N_MODEL:
        zSql = "SELECT count(DISTINCT model) FROM viki_chunk WHERE model<>''"
               " AND (?1 IS NULL OR model=?1)"; break;
      default: return fail(VIKI_EINVAL, "viki_count: unknown counter%s","");
    }
    if( sqlite3_prepare_v2(db, zSql, -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_count: %s", sqlite3_errmsg(db));
    if( zFilter ) sqlite3_bind_text(st, 1, zFilter, -1, SQLITE_STATIC);
    else          sqlite3_bind_null(st, 1);
    rc = sqlite3_step(st);
    if( rc==SQLITE_ROW && pn ) *pn = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return rc==SQLITE_ROW ? VIKI_OK : VIKI_ESQL;
}

VikiStatus viki_each(const char *zKind, const char *zKey,
                     viki_assert_row x, void *pApp){
    sqlite3 *db = db_or_null();
    sqlite3_stmt *st = 0;
    if( !db ) return VIKI_ENOCTX;
    if( sqlite3_prepare_v2(db,
        "SELECT a.id, a.kind, a.akey, a.ts, a.body,"
        "       NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
        "  FROM viki_assert a"
        " WHERE (?1 IS NULL OR a.kind=?1) AND (?2 IS NULL OR a.akey=?2)"
        " ORDER BY a.arank DESC", -1, &st, 0)!=SQLITE_OK )
        return fail(VIKI_ESQL, "viki_each: %s", sqlite3_errmsg(db));
    if( zKind ) sqlite3_bind_text(st, 1, zKind, -1, SQLITE_STATIC); else sqlite3_bind_null(st,1);
    if( zKey  ) sqlite3_bind_text(st, 2, zKey,  -1, SQLITE_STATIC); else sqlite3_bind_null(st,2);
    while( sqlite3_step(st)==SQLITE_ROW ){
        if( x && x(pApp,
              (const char*)sqlite3_column_text(st,0),
              (const char*)sqlite3_column_text(st,1),
              (const char*)sqlite3_column_text(st,2),
              (const char*)sqlite3_column_text(st,3),
              (const char*)sqlite3_column_text(st,4),
              sqlite3_column_int(st,5)) ) break;
    }
    sqlite3_finalize(st);
    return VIKI_OK;
}
