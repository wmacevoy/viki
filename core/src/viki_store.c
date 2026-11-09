/*
** viki_store.c -- assertions: frame, write, resolve, merge, erase.
** Design and reasons: core/DESIGN_v3.md.
*/
#include "viki_store_int.h"
#include "sha256.h"
#include <string.h>

struct VkStore {
    sqlite3 *db;
    VkBytes principal;
    const VkGrant *grants;
    uint32_t nGrants;
    const char *zErr;
    /* Prepared once, because undoing a failed write must not need memory
    ** that may be exactly what ran out. */
    sqlite3_stmt *pBegin, *pCommit, *pUndo, *pEnd;
};

/* ---- failure plumbing ---- */

int vk_prepare(VkStore *p, const char *zSql, sqlite3_stmt **ppOut){
    *ppOut = 0;
#ifdef VK_TEST_FAULTS
    if( vk_test_fault() ) return SQLITE_IOERR;
#endif
    return sqlite3_prepare_v2(p->db, zSql, -1, ppOut, 0);
}

int vk_step(sqlite3_stmt *pSt){
#ifdef VK_TEST_FAULTS
    if( vk_test_fault() ) return SQLITE_IOERR;
#endif
    return sqlite3_step(pSt);
}

VkStatus vk_refuse(VkStore *p, VkStatus rc, const char *zWhy){
    p->zErr = zWhy;
    return rc;
}

VkStatus vk_sql_status(VkStore *p, int rc){
    p->zErr = "sqlite failure";
    return (rc & 0xff)==SQLITE_NOMEM ? VK_ENOMEM : VK_ESQL;
}

const char *vk_errmsg(const VkStore *p){ return p->zErr ? p->zErr : ""; }

void vk_bind_bytes(sqlite3_stmt *pSt, int i, VkBytes b){
    /* A NULL pointer would bind SQL NULL, not an empty value. */
    sqlite3_bind_blob(pSt, i, b.n ? (const void*)b.p : (const void*)"", (int)b.n, SQLITE_STATIC);
}

static VkBytes column_bytes(sqlite3_stmt *pSt, int i){
    VkBytes b;
    b.p = (const uint8_t*)sqlite3_column_blob(pSt, i);
    b.n = (uint32_t)sqlite3_column_bytes(pSt, i);
    if( !b.p ){ b.p = (const uint8_t*)""; b.n = 0; }
    return b;
}

static int bytes_eq(VkBytes a, VkBytes b){
    return a.n==b.n && (a.n==0 || memcmp(a.p, b.p, a.n)==0);
}

static int bytes_is(VkBytes a, const char *z){
    VkBytes b;
    b.p = (const uint8_t*)z; b.n = (uint32_t)strlen(z);
    return bytes_eq(a, b);
}

/* ---- the frame ---- */

static void hash_u32(sha256_ctx *c, uint32_t v){
    unsigned char b[4];
    b[0] = (unsigned char)(v>>24); b[1] = (unsigned char)(v>>16);
    b[2] = (unsigned char)(v>>8);  b[3] = (unsigned char)v;
    sha256_update(c, b, 4);
}

static void hash_field(sha256_ctx *c, VkBytes b){
    hash_u32(c, b.n);
    sha256_update(c, b.n ? b.p : (const unsigned char*)"", b.n);
}

/* Links hash in sorted order, so the same reconciliation written by two
** peers is one assertion (A-2). Selecting the next-larger each pass needs no
** copy, and n is at most VK_MAX_LINKS. */
static void hash_links(sha256_ctx *c, const VkId *a, uint32_t n){
    const uint8_t *pPrev = 0;
    uint32_t i, k;
    hash_u32(c, n);
    for(k=0; k<n; k++){
        const uint8_t *pNext = 0;
        for(i=0; i<n; i++){
            if( pPrev && memcmp(a[i], pPrev, VK_ID_BYTES)<=0 ) continue;
            if( !pNext || memcmp(a[i], pNext, VK_ID_BYTES)<0 ) pNext = a[i];
        }
        if( !pNext ) return;    /* a repeat; vk_put and vk_ingest refuse these */
        sha256_update(c, pNext, VK_ID_BYTES);
        pPrev = pNext;
    }
}

/* The body enters as its hash, so erasing the body leaves the id computable
** from what remains (SHAPE-3). The tag keeps this framing from colliding
** with any other hashed format. */
void vk_assertion_id(const VkAssertion *a, VkId out){
    static const char zTag[] = "viki.assertion.v3";
    sha256_ctx c;
    uint8_t aBody[VK_ID_BYTES];
    if( a->erased ){
        memcpy(aBody, a->bodyHash, VK_ID_BYTES);
    }else{
        sha256_init(&c);
        sha256_update(&c, a->body.n ? a->body.p : (const unsigned char*)"", a->body.n);
        sha256_final(&c, aBody);
    }
    sha256_init(&c);
    sha256_update(&c, (const unsigned char*)zTag, sizeof(zTag));
    hash_field(&c, a->author);
    hash_field(&c, a->kind);
    hash_field(&c, a->akey);
    hash_field(&c, a->ts);
    sha256_update(&c, a->rank, VK_RANK_BYTES);
    hash_field(&c, a->reference);
    hash_links(&c, a->supersedes, a->nSupersedes);
    hash_links(&c, a->derivedFrom, a->nDerivedFrom);
    sha256_update(&c, aBody, VK_ID_BYTES);
    sha256_final(&c, out);
}

static int links_ok(const VkId *a, uint32_t n){
    uint32_t i, k;
    if( n>VK_MAX_LINKS ) return 0;
    for(i=0; i<n; i++){
        for(k=i+1; k<n; k++){
            if( memcmp(a[i], a[k], VK_ID_BYTES)==0 ) return 0;
        }
    }
    return 1;
}

/* NULL if well formed, else the requirement it breaks. */
static const char *shape_error(const VkAssertion *a){
    if( a->author.n==0 || a->kind.n==0 || a->akey.n==0 ) return "A-2: author, kind and akey are required";
    if( a->author.n>VK_MAX_FIELD || a->kind.n>VK_MAX_FIELD || a->akey.n>VK_MAX_FIELD
     || a->ts.n>VK_MAX_FIELD || a->reference.n>VK_MAX_FIELD ) return "ENV-2: field too long";
    if( !links_ok(a->supersedes, a->nSupersedes) || !links_ok(a->derivedFrom, a->nDerivedFrom) )
        return "A-2: too many links, or a repeated link";
    if( bytes_is(a->kind, VK_TOMBSTONE) ){
        if( a->erased ) return "W-2: a tombstone cannot be erased";
        if( a->body.n!=VK_ID_BYTES || !bytes_eq(a->akey, a->body) || a->nSupersedes )
            return "W-1: malformed tombstone";
    }
    return 0;
}

/* ---- open and close ---- */

static int rights_of(const VkStore *p, VkBytes who){
    uint32_t i;
    for(i=0; i<p->nGrants; i++){
        if( bytes_eq(p->grants[i].principal, who) ) return p->grants[i].rights;
    }
    return 0;
}

int vk_has_right(const VkStore *p, int right){
    return (rights_of(p, p->principal) & right)!=0;
}

static void rights_func(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    const VkStore *p = (const VkStore*)sqlite3_user_data(ctx);
    VkBytes who;
    (void)argc;
    who.p = (const uint8_t*)sqlite3_value_blob(argv[0]);
    who.n = (uint32_t)sqlite3_value_bytes(argv[0]);
    if( !who.p ){ who.p = (const uint8_t*)""; who.n = 0; }
    sqlite3_result_int(ctx, rights_of(p, who));
}

static const char zSchema[] =
    "CREATE TABLE IF NOT EXISTS assertion("
    " seq INTEGER PRIMARY KEY,"       /* arrival order: local, never merged */
    " id BLOB NOT NULL UNIQUE,"
    " author BLOB NOT NULL, kind BLOB NOT NULL, akey BLOB NOT NULL,"
    " ts BLOB NOT NULL, rank BLOB NOT NULL, reference BLOB NOT NULL,"
    " body_hash BLOB NOT NULL,"
    " body BLOB);"                    /* NULL once erased */
    "CREATE INDEX IF NOT EXISTS assertion_akey ON assertion(akey);"
    /* rel 0 is supersedes, 1 is derived_from */
    "CREATE TABLE IF NOT EXISTS edge(parent BLOB NOT NULL, rel INTEGER NOT NULL,"
    " child BLOB NOT NULL, PRIMARY KEY(parent,rel,child)) WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS edge_child ON edge(child,rel,parent);"
    "CREATE TABLE IF NOT EXISTS quarantine(id BLOB NOT NULL, why TEXT NOT NULL,"
    " PRIMARY KEY(id,why)) WITHOUT ROWID;";

static int exec_schema(VkStore *p){
    const char *zSql = zSchema;
    while( *zSql ){
        sqlite3_stmt *pSt = 0;
        const char *zTail = 0;
        int rc;
#ifdef VK_TEST_FAULTS
        if( vk_test_fault() ) return SQLITE_IOERR;
#endif
        rc = sqlite3_prepare_v2(p->db, zSql, -1, &pSt, &zTail);
        if( rc!=SQLITE_OK ) return rc;
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        if( rc!=SQLITE_DONE ) return rc;
        zSql = zTail;
    }
    return SQLITE_OK;
}

int vk_close(VkStore *p){
    if( !p ) return SQLITE_OK;
    sqlite3_finalize(p->pBegin);
    sqlite3_finalize(p->pCommit);
    sqlite3_finalize(p->pUndo);
    sqlite3_finalize(p->pEnd);
    sqlite3_create_function(p->db, "vk_rights", 1, SQLITE_UTF8, 0, 0, 0, 0);
    sqlite3_free(p);
    return SQLITE_OK;
}

VkStatus vk_open(sqlite3 *db, VkBytes principal, const VkGrant *grants,
                 uint32_t nGrants, VkStore **ppOut){
    VkStore *p;
    int rc;
    VkStatus st;
    *ppOut = 0;
    p = (VkStore*)sqlite3_malloc(sizeof(*p));
    if( !p ) return VK_ENOMEM;
    memset(p, 0, sizeof(*p));
    p->db = db; p->principal = principal;
    p->grants = grants; p->nGrants = nGrants;
    rc = sqlite3_create_function(db, "vk_rights", 1, SQLITE_UTF8, p, rights_func, 0, 0);
    if( rc==SQLITE_OK ) rc = exec_schema(p);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "SAVEPOINT vk", &p->pBegin);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "RELEASE vk", &p->pCommit);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "ROLLBACK TO vk", &p->pUndo);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "RELEASE vk", &p->pEnd);
    if( rc!=SQLITE_OK ){
        st = (rc & 0xff)==SQLITE_NOMEM ? VK_ENOMEM : VK_ESQL;
        vk_close(p);
        return st;
    }
    *ppOut = p;
    return VK_OK;
}

/* ---- atomic writes ---- */

static int begin(VkStore *p){
    int rc = vk_step(p->pBegin);
    sqlite3_reset(p->pBegin);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

static int commit(VkStore *p){
    int rc = vk_step(p->pCommit);
    sqlite3_reset(p->pCommit);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

/* Deliberately not vk_step: the test build must not fail the undo itself. */
static void undo(VkStore *p){
    sqlite3_step(p->pUndo);
    sqlite3_reset(p->pUndo);
    sqlite3_step(p->pEnd);
    sqlite3_reset(p->pEnd);
}

static int run_id(VkStore *p, const char *zSql, const uint8_t *id, int *pnChanged){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, zSql, &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_blob(pSt, 1, id, VK_ID_BYTES, SQLITE_STATIC);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE ) return rc;
    if( pnChanged ) *pnChanged = sqlite3_changes(p->db);
    return SQLITE_OK;
}

static int insert_links(VkStore *p, const uint8_t *id, int rel, const VkId *a, uint32_t n){
    sqlite3_stmt *pSt = 0;
    uint32_t i;
    int rc;
    if( n==0 ) return SQLITE_OK;
    rc = vk_prepare(p, "INSERT OR IGNORE INTO edge(parent,rel,child) VALUES(?1,?2,?3)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    for(i=0; i<n; i++){
        sqlite3_bind_blob(pSt, 1, a[i], VK_ID_BYTES, SQLITE_STATIC);
        sqlite3_bind_int(pSt, 2, rel);
        sqlite3_bind_blob(pSt, 3, id, VK_ID_BYTES, SQLITE_STATIC);
        rc = vk_step(pSt);
        if( rc!=SQLITE_DONE ) break;
        rc = SQLITE_OK;
        sqlite3_reset(pSt);
    }
    sqlite3_finalize(pSt);
    return rc;
}

/* Stores the row and applies whatever erasure it triggers, in one savepoint. */
static int store_row(VkStore *p, const VkAssertion *a, const VkId id, int *pbAdded){
    static const char zInsert[] =
        "INSERT INTO assertion(id,author,kind,akey,ts,rank,reference,body_hash,body)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) ON CONFLICT(id) DO NOTHING";
    sqlite3_stmt *pSt = 0;
    sha256_ctx c;
    uint8_t aHash[VK_ID_BYTES];
    int bTomb = bytes_is(a->kind, VK_TOMBSTONE);
    int rc = begin(p);
    if( rc!=SQLITE_OK ) return rc;

    if( a->erased ){
        memcpy(aHash, a->bodyHash, VK_ID_BYTES);
    }else{
        sha256_init(&c);
        sha256_update(&c, a->body.n ? a->body.p : (const unsigned char*)"", a->body.n);
        sha256_final(&c, aHash);
    }
    rc = vk_prepare(p, zInsert, &pSt);
    if( rc==SQLITE_OK ){
        sqlite3_bind_blob(pSt, 1, id, VK_ID_BYTES, SQLITE_STATIC);
        vk_bind_bytes(pSt, 2, a->author);
        vk_bind_bytes(pSt, 3, a->kind);
        vk_bind_bytes(pSt, 4, a->akey);
        vk_bind_bytes(pSt, 5, a->ts);
        sqlite3_bind_blob(pSt, 6, a->rank, VK_RANK_BYTES, SQLITE_STATIC);
        vk_bind_bytes(pSt, 7, a->reference);
        sqlite3_bind_blob(pSt, 8, aHash, VK_ID_BYTES, SQLITE_STATIC);
        if( !a->erased ) vk_bind_bytes(pSt, 9, a->body);
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
    }
    *pbAdded = 0;
    if( rc==SQLITE_OK ) *pbAdded = sqlite3_changes(p->db)>0;
    if( rc==SQLITE_OK && *pbAdded ){
        rc = insert_links(p, id, 0, a->supersedes, a->nSupersedes);
        if( rc==SQLITE_OK ) rc = insert_links(p, id, 1, a->derivedFrom, a->nDerivedFrom);
    }
    if( rc==SQLITE_OK && *pbAdded ){
        if( bTomb && (rights_of(p, a->author) & VK_ERASE) ){
            /* W-2: a tombstone is immune to being erased. */
            rc = run_id(p, "UPDATE assertion SET body=NULL WHERE id=?1"
                           " AND kind<>CAST('" VK_TOMBSTONE "' AS BLOB)", a->body.p, 0);
        }else if( !bTomb ){
            /* W-6: the tombstone may have arrived first. */
            rc = run_id(p, "UPDATE assertion SET body=NULL WHERE id=?1 AND EXISTS("
                           "SELECT 1 FROM assertion t WHERE t.akey=?1"
                           " AND t.kind=CAST('" VK_TOMBSTONE "' AS BLOB) AND vk_rights(t.author)&1)", id, 0);
        }
    }
    if( rc==SQLITE_OK ) rc = commit(p);
    if( rc!=SQLITE_OK ) undo(p);
    return rc;
}

/* ---- write ---- */

static int needed_right(const VkAssertion *a){
    if( bytes_is(a->kind, VK_TOMBSTONE) ) return VK_ERASE;
    return a->nSupersedes ? VK_SUPERSEDE : VK_ORIGINATE;
}

VkStatus vk_put(VkStore *p, const VkAssertion *a, VkId idOut){
    const char *zBad = shape_error(a);
    int bAdded = 0, rc;
    if( zBad ) return vk_refuse(p, VK_EINVAL, zBad);
    if( a->erased ) return vk_refuse(p, VK_EINVAL, "A-6: a write cannot arrive erased");
    if( !bytes_eq(a->author, p->principal) )
        return vk_refuse(p, VK_EDENIED, "I-1: the author must be this store's principal");
    if( !vk_has_right(p, needed_right(a)) )
        return vk_refuse(p, VK_EDENIED, "D-3: the principal lacks the right this write needs");
    vk_assertion_id(a, idOut);
    rc = store_row(p, a, idOut, &bAdded);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    return bAdded ? VK_OK : VK_EXISTS;
}

#define VK_HEADS(k) \
    "SELECT a.id, a.rank, a.body FROM assertion a WHERE a.akey=" k " AND " VK_VALID("a") \
    " AND NOT EXISTS(SELECT 1 FROM edge e JOIN assertion c ON c.id=e.child" \
    " WHERE e.parent=a.id AND e.rel=0 AND " VK_VALID("c") ")"

VkStatus vk_put_chain(VkStore *p, const VkAssertion *a, VkId idOut){
    VkId aHead[VK_MAX_LINKS];
    VkAssertion b = *a;
    sqlite3_stmt *pSt = 0;
    uint32_t n = 0;
    int rc = vk_prepare(p, VK_HEADS("?1"), &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, a->akey);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        if( n==VK_MAX_LINKS ){
            sqlite3_finalize(pSt);
            return vk_refuse(p, VK_EINVAL, "D-3f: more arms than one assertion can supersede");
        }
        memcpy(aHead[n++], sqlite3_column_blob(pSt, 0), VK_ID_BYTES);
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE ) return vk_sql_status(p, rc);
    b.supersedes = (const VkId*)aHead;
    b.nSupersedes = n;
    return vk_put(p, &b, idOut);
}

VkStatus vk_erase(VkStore *p, const VkId target, VkBytes ts,
                  const uint8_t rank[VK_RANK_BYTES], VkId idOut){
    VkAssertion a;
    memset(&a, 0, sizeof(a));
    a.author = p->principal;
    a.kind.p = (const uint8_t*)VK_TOMBSTONE;
    a.kind.n = (uint32_t)strlen(VK_TOMBSTONE);
    a.akey.p = target;  a.akey.n = VK_ID_BYTES;
    a.body = a.akey;
    a.ts = ts;
    memcpy(a.rank, rank, VK_RANK_BYTES);
    return vk_put(p, &a, idOut);
}

/* ---- read ---- */

static VkStatus read_head(VkStore *p, VkBytes akey, VkRow *r, int bChoose){
    /* Ordering is on the arms of one key, so the sorter holds only those. */
    static const char zSql[] =
        "WITH h AS (" VK_HEADS("?1") ")"
        " SELECT id, body, (SELECT count(*) FROM h) FROM h"
        " ORDER BY rank DESC, id DESC LIMIT 1";
    int rc;
    memset(r, 0, sizeof(*r));
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p, zSql, &r->pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(r->pSt, 1, akey);
    rc = vk_step(r->pSt);
    if( rc==SQLITE_DONE ){ vk_row_close(r); return VK_NOTFOUND; }
    if( rc!=SQLITE_ROW ){ vk_row_close(r); return vk_sql_status(p, rc); }
    r->nArms = sqlite3_column_int(r->pSt, 2);
    if( r->nArms>1 && !bChoose ){ vk_row_close(r); return VK_FORKED; }
    memcpy(r->id, sqlite3_column_blob(r->pSt, 0), VK_ID_BYTES);
    r->erased = sqlite3_column_type(r->pSt, 1)==SQLITE_NULL;
    r->body = column_bytes(r->pSt, 1);
    return VK_OK;
}

VkStatus vk_current(VkStore *p, VkBytes akey, VkRow *r){ return read_head(p, akey, r, 0); }
VkStatus vk_resolve(VkStore *p, VkBytes akey, VkRow *r){ return read_head(p, akey, r, 1); }

void vk_row_close(VkRow *r){
    sqlite3_finalize(r->pSt);
    r->pSt = 0;
}

/* ---- cursor ---- */

VkStatus vk_cursor_open(VkStore *p, int64_t afterSeq, VkCursor *c){
    int rc;
    memset(c, 0, sizeof(*c));
    c->p = p;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p,
        "SELECT seq,id,author,kind,akey,ts,rank,reference,body_hash,body"
        " FROM assertion WHERE seq>?1 ORDER BY seq", &c->pRow);
    if( rc==SQLITE_OK ) rc = vk_prepare(p,
        "SELECT rel,parent FROM edge WHERE child=?1 ORDER BY rel,parent", &c->pEdge);
    if( rc!=SQLITE_OK ){
        VkStatus st = vk_sql_status(p, rc);
        vk_cursor_close(c);
        return st;
    }
    sqlite3_bind_int64(c->pRow, 1, afterSeq);
    return VK_OK;
}

VkStatus vk_cursor_next(VkCursor *c){
    uint32_t an[2] = {0, 0};
    VkBytes b;
    int rc = vk_step(c->pRow);
    if( rc==SQLITE_DONE ) return VK_DONE;
    if( rc!=SQLITE_ROW ) return vk_sql_status(c->p, rc);
    c->seq = sqlite3_column_int64(c->pRow, 0);
    memcpy(c->id, sqlite3_column_blob(c->pRow, 1), VK_ID_BYTES);
    c->a.author    = column_bytes(c->pRow, 2);
    c->a.kind      = column_bytes(c->pRow, 3);
    c->a.akey      = column_bytes(c->pRow, 4);
    c->a.ts        = column_bytes(c->pRow, 5);
    b = column_bytes(c->pRow, 6);
    memcpy(c->a.rank, b.p, VK_RANK_BYTES);
    c->a.reference = column_bytes(c->pRow, 7);
    b = column_bytes(c->pRow, 8);
    memcpy(c->a.bodyHash, b.p, VK_ID_BYTES);
    c->a.erased = sqlite3_column_type(c->pRow, 9)==SQLITE_NULL;
    c->a.body   = column_bytes(c->pRow, 9);

    sqlite3_reset(c->pEdge);
    sqlite3_bind_blob(c->pEdge, 1, c->id, VK_ID_BYTES, SQLITE_STATIC);
    while( (rc = vk_step(c->pEdge))==SQLITE_ROW ){
        int rel = sqlite3_column_int(c->pEdge, 0);
        if( rel<0 || rel>1 || an[rel]==VK_MAX_LINKS ) continue;
        memcpy(c->links[rel][an[rel]++], sqlite3_column_blob(c->pEdge, 1), VK_ID_BYTES);
    }
    if( rc!=SQLITE_DONE ) return vk_sql_status(c->p, rc);
    c->a.supersedes  = (const VkId*)c->links[0]; c->a.nSupersedes  = an[0];
    c->a.derivedFrom = (const VkId*)c->links[1]; c->a.nDerivedFrom = an[1];
    return VK_OK;
}

void vk_cursor_close(VkCursor *c){
    sqlite3_finalize(c->pRow);
    sqlite3_finalize(c->pEdge);
    c->pRow = c->pEdge = 0;
}

/* ---- merge ---- */

static int quarantine(VkStore *p, const VkId id, const char *zWhy){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "INSERT OR IGNORE INTO quarantine(id,why) VALUES(?1,?2)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_blob(pSt, 1, id, VK_ID_BYTES, SQLITE_STATIC);
    sqlite3_bind_text(pSt, 2, zWhy, -1, SQLITE_STATIC);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

VkStatus vk_ingest(VkStore *dst, const VkAssertion *a, const VkId id, VkMergeReport *rep){
    VkId aCheck;
    const char *zBad = shape_error(a);
    int bAdded = 0, rc;
    if( !zBad ){
        vk_assertion_id(a, aCheck);
        if( memcmp(aCheck, id, VK_ID_BYTES)!=0 ) zBad = "A-7: the id does not match the content";
    }
    if( zBad ){
        rc = quarantine(dst, id, zBad);
        if( rc!=SQLITE_OK ) return vk_sql_status(dst, rc);
        rep->quarantined++;
        return VK_OK;
    }
    rc = store_row(dst, a, id, &bAdded);
    if( rc!=SQLITE_OK ) return vk_sql_status(dst, rc);
    if( bAdded ) rep->added++; else rep->present++;
    return VK_OK;
}

VkStatus vk_merge(VkStore *dst, VkStore *src, VkMergeReport *rep){
    VkCursor c;
    VkStatus st = vk_cursor_open(src, 0, &c);
    if( st==VK_OK ){
        while( (st = vk_cursor_next(&c))==VK_OK ){
            st = vk_ingest(dst, &c.a, c.id, rep);
            if( st!=VK_OK ) break;
        }
        vk_cursor_close(&c);
    }
    if( st==VK_DONE ) return VK_OK;
    rep->incomplete = 1;
    return st;
}

/* ---- checks ---- */

VkStatus vk_verify(VkStore *p, int64_t *pnBad){
    VkCursor c;
    VkId aId;
    VkStatus st = vk_cursor_open(p, 0, &c);
    *pnBad = 0;
    if( st!=VK_OK ) return st;
    while( (st = vk_cursor_next(&c))==VK_OK ){
        vk_assertion_id(&c.a, aId);
        if( memcmp(aId, c.id, VK_ID_BYTES)!=0 ) (*pnBad)++;
    }
    vk_cursor_close(&c);
    return st==VK_DONE ? VK_OK : st;
}

VkStatus vk_digest(VkStore *p, VkId out){
    sqlite3_stmt *pSt = 0;
    sha256_ctx c;
    int rc;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p, "SELECT id, body IS NULL FROM assertion ORDER BY id", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    sha256_init(&c);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        unsigned char bErased = (unsigned char)sqlite3_column_int(pSt, 1);
        sha256_update(&c, (const unsigned char*)sqlite3_column_blob(pSt, 0), VK_ID_BYTES);
        sha256_update(&c, &bErased, 1);
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE ) return vk_sql_status(p, rc);
    sha256_final(&c, out);
    return VK_OK;
}
