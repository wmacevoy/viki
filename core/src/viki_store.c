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
    int rcSql;      /* kept because undoing a failed write clears SQLite's own */
    /* Prepared once, because undoing a failed write must not need memory
    ** that may be exactly what ran out. */
    sqlite3_stmt *pBegin, *pCommit, *pUndo, *pEnd;
    sqlite3_stmt *pBody;   /* one stored row's body and delta base, for vk_body */
    VkBytes outlines;      /* OL-4: whose outlines searches use; empty is none */
};

/* DS: when a superseded body is stored as a delta. invented, until measured. */
#define VK_DELTA_MIN   256   /* smaller bodies stay whole: a delta would not pay for itself */
#define VK_DELTA_CHAIN 16    /* DS-2: the most deltas walked to rebuild a version */

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
    p->rcSql = rc;
    return (rc & 0xff)==SQLITE_NOMEM ? VK_ENOMEM : VK_ESQL;
}

const char *vk_errmsg(const VkStore *p){ return p->zErr ? p->zErr : ""; }
int vk_sqlite_code(const VkStore *p){ return p->rcSql; }
sqlite3 *vk_store_db(VkStore *p){ return p->db; }
VkBytes vk_outline_author(const VkStore *p){ return p->outlines; }
void vk_use_outlines(VkStore *p, VkBytes author){ p->outlines = author; }

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

static void hash_u64(sha256_ctx *c, uint64_t v){
    hash_u32(c, (uint32_t)(v>>32));
    hash_u32(c, (uint32_t)v);
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
    hash_field(&c, a->log);
    hash_u64(&c, a->pos);
    hash_u32(&c, a->nSkip);
    if( a->nSkip ) sha256_update(&c, (const unsigned char*)a->skip, a->nSkip*VK_ID_BYTES);
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

/* ---- logs: pure position arithmetic ---- */

/* LOG-2: how many skip links the row at pos has. */
static uint32_t skip_count(uint64_t pos){
    uint32_t j, n = 0;
    for(j=0; j<VK_MAX_SKIP && (pos & ((uint64_t)1<<j))==0; j++){
        if( pos>((uint64_t)1<<j) ) n++;
    }
    if( j<VK_MAX_SKIP && pos>((uint64_t)1<<j) ) n++;
    return n;
}

static uint32_t popcount(uint64_t v){
    uint32_t n = 0;
    for(; v; v&=v-1) n++;
    return n;
}

static uint64_t get_be64(const uint8_t *p){
    uint64_t v = 0;
    int i;
    for(i=0; i<8; i++) v = (v<<8) | p[i];
    return v;
}

/* What a range tombstone's akey must be, so it can chain with no other key. */
static void log_key(VkBytes author, VkBytes log, VkId out){
    static const char zTag[] = "viki.log.v3";
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const unsigned char*)zTag, sizeof(zTag));
    hash_field(&c, author);
    hash_field(&c, log);
    sha256_final(&c, out);
}

/* OL-1: one author's outlines of one target share this key (OL-2). */
void vk_outline_key(const VkId target, VkBytes author, VkId out){
    static const char zTag[] = "viki.outline.v3";
    VkBytes t;
    sha256_ctx c;
    t.p = target; t.n = VK_ID_BYTES;
    sha256_init(&c);
    sha256_update(&c, (const unsigned char*)zTag, sizeof(zTag));
    hash_field(&c, t);
    hash_field(&c, author);
    sha256_final(&c, out);
}

/* OL-1, the frame: what can be checked without a store. */
static const char *outline_frame_error(const VkAssertion *a){
    VkId key;
    uint32_t i;
    int bLinked = 0;
    if( a->log.n ) return "OL-1: an outline is not logged";
    if( a->reference.n!=VK_ID_BYTES ) return "OL-1: an outline's reference is its target's id";
    vk_outline_key(a->reference.p, a->author, key);
    if( a->akey.n!=VK_ID_BYTES || memcmp(a->akey.p, key, VK_ID_BYTES)!=0 )
        return "OL-1: an outline's akey is vk_outline_key(target, author)";
    for(i=0; i<a->nDerivedFrom; i++) bLinked |= memcmp(a->derivedFrom[i], a->reference.p, VK_ID_BYTES)==0;
    if( !bLinked ) return "OL-1: an outline names its target in derived_from";
    return 0;
}

/* NULL if well formed, else the requirement it breaks. */
static const char *shape_error(const VkAssertion *a){
    if( a->author.n==0 || a->kind.n==0 || a->akey.n==0 ) return "A-2: author, kind and akey are required";
    if( a->author.n>VK_MAX_FIELD || a->kind.n>VK_MAX_FIELD || a->akey.n>VK_MAX_FIELD
     || a->ts.n>VK_MAX_FIELD || a->reference.n>VK_MAX_FIELD ) return "ENV-2: field too long";
    if( !links_ok(a->supersedes, a->nSupersedes) || !links_ok(a->derivedFrom, a->nDerivedFrom) )
        return "A-2: too many links, or a repeated link";
    if( a->log.n>VK_MAX_FIELD ) return "ENV-2: field too long";
    if( bytes_is(a->kind, VK_RANGE) ){
        VkId aKey;
        if( a->erased ) return "W-2: a tombstone cannot be erased";
        if( a->log.n==0 || a->reference.n==0 || a->nSupersedes || a->nSkip
         || a->pos<1 || a->pos>VK_MAX_POS
         || a->body.n!=8+popcount(a->pos)*VK_ID_BYTES
         || get_be64(a->body.p)<1 || get_be64(a->body.p)>a->pos ) return "W-15: malformed range tombstone";
        log_key(a->reference, a->log, aKey);
        if( a->akey.n!=VK_ID_BYTES || memcmp(a->akey.p, aKey, VK_ID_BYTES)!=0 )
            return "W-15: malformed range tombstone";
        return 0;
    }
    if( a->log.n==0 ){
        if( a->pos || a->nSkip ) return "LOG-1: a position needs a log";
    }else if( a->pos<1 || a->pos>VK_MAX_POS || a->nSkip!=skip_count(a->pos) ){
        return "LOG-2: the position and its skip links disagree";
    }
    if( bytes_is(a->kind, VK_OUTLINE) && !a->erased ){
        const char *z = outline_frame_error(a);
        if( z ) return z;
    }
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
    " id BLOB NOT NULL,"
    " author BLOB NOT NULL, kind BLOB NOT NULL, akey BLOB NOT NULL,"
    " ts BLOB NOT NULL, rank BLOB NOT NULL, reference BLOB NOT NULL,"
    " body_hash BLOB NOT NULL,"
    " body BLOB,"                     /* NULL once erased */
    " log BLOB NOT NULL, pos INTEGER NOT NULL, skip BLOB NOT NULL,"
    /* DS-1: NULL, the body is whole; else body is a reverse delta from row dbase. */
    " dbase INTEGER);"
    /* LOG-4: a logged row is found by position. An id is random, so an index on
    ** it would dirty a scattered page for every row written. */
    "CREATE UNIQUE INDEX IF NOT EXISTS assertion_id ON assertion(id) WHERE log=x'';"
    "CREATE UNIQUE INDEX IF NOT EXISTS assertion_log ON assertion(log,author,pos,id) WHERE log<>x'';"
    /* Projections, rebuildable from the rows (SHAPE-5). arm: the rows no valid
    ** assertion supersedes, so finding a head need not walk its chain (LOG-3). */
    "CREATE TABLE IF NOT EXISTS arm(akey BLOB NOT NULL, id BLOB NOT NULL, seq INTEGER NOT NULL,"
    " PRIMARY KEY(akey,id)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS range_tomb(log BLOB NOT NULL, target BLOB NOT NULL,"
    " pto INTEGER NOT NULL, pfrom INTEGER NOT NULL, author BLOB NOT NULL, id BLOB NOT NULL, seq INTEGER NOT NULL,"
    " PRIMARY KEY(log,target,pto,pfrom,id)) WITHOUT ROWID;"
    "CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v BLOB NOT NULL) WITHOUT ROWID;"
    /* rel 0 is supersedes, 1 is derived_from */
    "CREATE TABLE IF NOT EXISTS edge(parent BLOB NOT NULL, rel INTEGER NOT NULL,"
    " child BLOB NOT NULL, child_seq INTEGER NOT NULL, PRIMARY KEY(parent,rel,child)) WITHOUT ROWID;"
    "CREATE INDEX IF NOT EXISTS edge_child ON edge(child,rel,parent);"
    "CREATE TABLE IF NOT EXISTS quarantine(id BLOB NOT NULL, why TEXT NOT NULL,"
    " PRIMARY KEY(id,why)) WITHOUT ROWID;"
    /* OL-4, a projection: the nodes of every current outline. depth 0 is the text
    ** outside every top-level node. A node's own text is its range less its
    ** children's, so each byte of a target is in exactly one node (vk_oseg). */
    "CREATE TABLE IF NOT EXISTS vk_onode(rowid INTEGER PRIMARY KEY, outline_seq INTEGER NOT NULL,"
    " akey BLOB NOT NULL, target BLOB NOT NULL, author BLOB NOT NULL, depth INTEGER NOT NULL,"
    " lo INTEGER NOT NULL, hi INTEGER NOT NULL, path TEXT NOT NULL);"
    "CREATE INDEX IF NOT EXISTS vk_onode_target ON vk_onode(target, author, depth);"
    "CREATE INDEX IF NOT EXISTS vk_onode_akey ON vk_onode(akey);"
    "CREATE TABLE IF NOT EXISTS vk_oseg(node INTEGER NOT NULL, lo INTEGER NOT NULL, hi INTEGER NOT NULL);"
    "CREATE INDEX IF NOT EXISTS vk_oseg_node ON vk_oseg(node, lo);";

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
    sqlite3_finalize(p->pBody);
    sqlite3_create_function(p->db, "vk_body", 2, SQLITE_UTF8, 0, 0, 0, 0);
    sqlite3_finalize(p->pBegin);
    sqlite3_finalize(p->pCommit);
    sqlite3_finalize(p->pUndo);
    sqlite3_finalize(p->pEnd);
    sqlite3_create_function(p->db, "vk_rights", 1, SQLITE_UTF8, 0, 0, 0, 0);
    sqlite3_free(p);
    return SQLITE_OK;
}

static int begin(VkStore *p);
static int commit(VkStore *p);
static void undo(VkStore *p);

/* R-1a: only a row on the same akey supersedes. */
#define VK_NO_VALID_CHILD(idExpr, akeyExpr) \
    "NOT EXISTS(SELECT 1 FROM edge e JOIN assertion c ON c.seq=e.child_seq" \
    " WHERE e.parent=" idExpr " AND e.rel=0 AND c.akey=" akeyExpr " AND " VK_VALID("c") ")"

/* Which rows are arms depends on who holds which right, so the projection is
** rebuilt only when the grants differ from those it was built under. */
/* ---- OL-4: the node projection ---- */

static int refold_outline(VkStore *p, VkBytes akey);

/* The nodes of one outline: a row per node, and its own-text segments. */
static int build_nodes(VkStore *p, sqlite3_int64 seq, VkBytes akey, VkBytes target, VkBytes author,
                       VkBytes body, sqlite3_int64 nTarget){
    static const char zNodes[] =
        "WITH RECURSIVE t AS (SELECT id, parent, type, value FROM json_tree(?1)),"
        " n AS (SELECT o.id, (SELECT a.parent FROM t a WHERE a.id=o.parent) AS up,"
        "   json_extract(o.value,'$.title') AS title, json_extract(o.value,'$.lo') AS lo,"
        "   json_extract(o.value,'$.hi') AS hi FROM t o WHERE o.type='object'),"
        " anc(id, a, d) AS (SELECT id, id, 0 FROM n UNION ALL"
        "   SELECT anc.id, n.up, anc.d+1 FROM anc JOIN n ON n.id=anc.a WHERE n.up IS NOT NULL)"
        " SELECT n.id, n.up, n.lo, n.hi, (SELECT count(*) FROM anc WHERE anc.id=n.id),"
        "  (SELECT json_group_array(title) FROM (SELECT x.title FROM anc JOIN n x ON x.id=anc.a"
        "   WHERE anc.id=n.id ORDER BY anc.d DESC))"
        " FROM n ORDER BY n.id";
    sqlite3_stmt *pSt = 0, *pIns = 0, *pSeg = 0;
    sqlite3_int64 *aJid = 0, *aRow = 0, *aCur = 0, *aHi = 0;
    int nNode = 0, nAlloc = 16, rc, i;
    rc = vk_prepare(p, "INSERT INTO vk_onode(outline_seq,akey,target,author,depth,lo,hi,path)"
                       " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", &pIns);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "INSERT INTO vk_oseg(node,lo,hi) VALUES(?1,?2,?3)", &pSeg);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, zNodes, &pSt);
    if( rc!=SQLITE_OK ) goto done;
    aJid = sqlite3_malloc64(sizeof(sqlite3_int64)*4*nAlloc);
    if( !aJid ){ rc = SQLITE_NOMEM; goto done; }
    aRow = aJid + nAlloc; aCur = aRow + nAlloc; aHi = aCur + nAlloc;
    /* the root: the text outside every top-level node */
    sqlite3_bind_int64(pIns, 1, seq); vk_bind_bytes(pIns, 2, akey); vk_bind_bytes(pIns, 3, target);
    vk_bind_bytes(pIns, 4, author);
    sqlite3_bind_int(pIns, 5, 0); sqlite3_bind_int64(pIns, 6, 0);
    sqlite3_bind_int64(pIns, 7, nTarget>=0 ? nTarget : 0x7fffffff);
    sqlite3_bind_text(pIns, 8, "[]", 2, SQLITE_STATIC);
    rc = vk_step(pIns);
    if( rc!=SQLITE_DONE ) goto done;
    sqlite3_reset(pIns);
    aJid[0] = -1; aRow[0] = sqlite3_last_insert_rowid(p->db); aCur[0] = 0;
    aHi[0] = nTarget>=0 ? nTarget : 0x7fffffff;
    nNode = 1;
    sqlite3_bind_text(pSt, 1, body.n ? (const char*)body.p : "", (int)body.n, SQLITE_STATIC);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        sqlite3_int64 up = sqlite3_column_type(pSt, 1)==SQLITE_NULL ? -1 : sqlite3_column_int64(pSt, 1);
        sqlite3_int64 lo = sqlite3_column_int64(pSt, 2), hi = sqlite3_column_int64(pSt, 3);
        int par = 0, k;
        if( nNode==nAlloc ){
            sqlite3_int64 *aNew = sqlite3_malloc64(sizeof(sqlite3_int64)*8*nAlloc);
            if( !aNew ){ rc = SQLITE_NOMEM; goto done; }
            for(k=0; k<4; k++) memcpy(aNew + k*2*nAlloc, aJid + k*nAlloc, sizeof(sqlite3_int64)*nAlloc);
            sqlite3_free(aJid);
            aJid = aNew; nAlloc *= 2;
            aRow = aJid + nAlloc; aCur = aRow + nAlloc; aHi = aCur + nAlloc;
        }
        for(k=nNode-1; k>0; k--) if( aJid[k]==up ){ par = k; break; }   /* parents come first */
        sqlite3_bind_int(pIns, 5, sqlite3_column_int(pSt, 4));
        sqlite3_bind_int64(pIns, 6, lo);
        sqlite3_bind_int64(pIns, 7, hi);
        sqlite3_bind_text(pIns, 8, (const char*)sqlite3_column_text(pSt, 5), -1, SQLITE_TRANSIENT);
        rc = vk_step(pIns);
        if( rc!=SQLITE_DONE ) goto done;
        sqlite3_reset(pIns);
        aRow[nNode] = sqlite3_last_insert_rowid(p->db);   /* now: a segment insert below moves it */
        if( lo>aCur[par] ){              /* the parent's own text before this child */
            sqlite3_bind_int64(pSeg, 1, aRow[par]); sqlite3_bind_int64(pSeg, 2, aCur[par]);
            sqlite3_bind_int64(pSeg, 3, lo);
            rc = vk_step(pSeg);
            if( rc!=SQLITE_DONE ) goto done;
            sqlite3_reset(pSeg);
        }
        if( hi>aCur[par] ) aCur[par] = hi;
        aJid[nNode] = sqlite3_column_int64(pSt, 0);
        aCur[nNode] = lo; aHi[nNode] = hi;
        nNode++;
    }
    if( rc!=SQLITE_DONE ) goto done;
    for(i=0; i<nNode; i++){              /* each node's own text after its last child */
        if( aHi[i]<=aCur[i] ) continue;
        sqlite3_bind_int64(pSeg, 1, aRow[i]); sqlite3_bind_int64(pSeg, 2, aCur[i]);
        sqlite3_bind_int64(pSeg, 3, aHi[i]);
        rc = vk_step(pSeg);
        if( rc!=SQLITE_DONE ) goto done;
        sqlite3_reset(pSeg);
    }
    rc = SQLITE_OK;
done:
    sqlite3_free(aJid);
    sqlite3_finalize(pSt);
    sqlite3_finalize(pIns);
    sqlite3_finalize(pSeg);
    return rc;
}

/* The nodes for one outline akey: those of its current heads, valid and not
** erased. The bm25 index, when there is one, follows. */
static int refold_outline(VkStore *p, VkBytes akey){
    sqlite3_stmt *pSt = 0;
    int rc, bIndex = 0;
    rc = vk_bm25_on(p, &bIndex);
    if( rc==SQLITE_OK && bIndex ){
        rc = vk_prepare(p, "INSERT INTO vk_bm25(vk_bm25,rowid,body) SELECT 'delete', -n.rowid, "
                           VK_NODE_TEXT("n.rowid", VK_TARGET_BODY("n.target")) " FROM vk_onode n WHERE n.akey=?1", &pSt);
        if( rc==SQLITE_OK ){ vk_bind_bytes(pSt, 1, akey); rc = vk_step(pSt); rc = rc==SQLITE_DONE ? SQLITE_OK : rc; }
        sqlite3_finalize(pSt);
    }
    if( rc==SQLITE_OK ){
        rc = vk_prepare(p, "DELETE FROM vk_oseg WHERE node IN (SELECT rowid FROM vk_onode WHERE akey=?1)", &pSt);
        if( rc==SQLITE_OK ){ vk_bind_bytes(pSt, 1, akey); rc = vk_step(pSt); rc = rc==SQLITE_DONE ? SQLITE_OK : rc; }
        sqlite3_finalize(pSt);
    }
    if( rc==SQLITE_OK ){
        rc = vk_prepare(p, "DELETE FROM vk_onode WHERE akey=?1", &pSt);
        if( rc==SQLITE_OK ){ vk_bind_bytes(pSt, 1, akey); rc = vk_step(pSt); rc = rc==SQLITE_DONE ? SQLITE_OK : rc; }
        sqlite3_finalize(pSt);
    }
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_prepare(p,
        "SELECT a.seq, a.reference, a.author, a.body,"
        " (SELECT length(" VK_BODY("t.") ") FROM assertion t WHERE t.id=a.reference AND t.log=x'' AND t.body IS NOT NULL)"
        " FROM arm x JOIN assertion a ON a.seq=x.seq WHERE x.akey=?1"
        " AND a.kind=CAST('" VK_OUTLINE "' AS BLOB) AND a.body IS NOT NULL", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    vk_bind_bytes(pSt, 1, akey);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        VkBytes target = column_bytes(pSt, 1), author = column_bytes(pSt, 2), body = column_bytes(pSt, 3);
        sqlite3_int64 n = sqlite3_column_type(pSt, 4)==SQLITE_NULL ? -1 : sqlite3_column_int64(pSt, 4);
        rc = build_nodes(p, sqlite3_column_int64(pSt, 0), akey, target, author, body, n);
        if( rc!=SQLITE_OK ) break;
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE && rc!=SQLITE_OK ) return rc;
    rc = SQLITE_OK;
    if( bIndex ){
        rc = vk_prepare(p, "INSERT INTO vk_bm25(rowid,body) SELECT -n.rowid, "
                           VK_NODE_TEXT("n.rowid", VK_TARGET_BODY("n.target")) " FROM vk_onode n WHERE n.akey=?1", &pSt);
        if( rc==SQLITE_OK ){ vk_bind_bytes(pSt, 1, akey); rc = vk_step(pSt); rc = rc==SQLITE_DONE ? SQLITE_OK : rc; }
        sqlite3_finalize(pSt);
    }
    return rc;
}

/* Every outline akey, after the grants changed which heads count. */
static int refold_all(VkStore *p){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT akey FROM vk_onode UNION"
                           " SELECT akey FROM assertion WHERE kind=CAST('" VK_OUTLINE "' AS BLOB)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        VkBytes k = column_bytes(pSt, 0);
        uint8_t aKey[VK_MAX_FIELD];
        VkBytes kc;
        if( k.n>VK_MAX_FIELD ) continue;
        memcpy(aKey, k.p, k.n);          /* the cursor moves under it */
        kc.p = aKey; kc.n = k.n;
        rc = refold_outline(p, kc);
        if( rc!=SQLITE_OK ) break;
    }
    sqlite3_finalize(pSt);
    return (rc==SQLITE_DONE || rc==SQLITE_OK) ? SQLITE_OK : rc;
}

static int sync_arms(VkStore *p){
    sqlite3_stmt *pSt = 0;
    sha256_ctx c;
    VkId aPrint;
    uint32_t i;
    int bSame = 0;
    int rc = vk_prepare(p, "SELECT v FROM meta WHERE k='grants'", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sha256_init(&c);
    for(i=0; i<p->nGrants; i++){
        hash_field(&c, p->grants[i].principal);
        hash_u32(&c, (uint32_t)p->grants[i].rights);
    }
    sha256_final(&c, aPrint);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        bSame = sqlite3_column_bytes(pSt,0)==VK_ID_BYTES
             && memcmp(sqlite3_column_blob(pSt,0), aPrint, VK_ID_BYTES)==0;
        rc = SQLITE_DONE;
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE ) return rc;
    if( bSame ) return SQLITE_OK;
    rc = begin(p);
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_prepare(p, "DELETE FROM arm", &pSt);
    if( rc==SQLITE_OK ){
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
    }
    if( rc==SQLITE_OK ) rc = vk_prepare(p,
        "INSERT INTO arm(akey,id,seq) SELECT a.akey,a.id,a.seq FROM assertion a WHERE "
        VK_VALID("a") " AND " VK_NO_VALID_CHILD("a.id","a.akey"), &pSt);
    if( rc==SQLITE_OK ){
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
    }
    if( rc==SQLITE_OK ) rc = refold_all(p);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "INSERT OR REPLACE INTO meta(k,v) VALUES('grants',?1)", &pSt);
    if( rc==SQLITE_OK ){
        sqlite3_bind_blob(pSt, 1, aPrint, VK_ID_BYTES, SQLITE_STATIC);
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
    }
    if( rc==SQLITE_OK ) rc = commit(p);
    if( rc!=SQLITE_OK ) undo(p);
    return rc;
}

/* ---- DS: bodies stored as reverse deltas ---- */

/* vk_body(body, dbase): the body as written. A delta is rebuilt from the chain
** of rows it rests on, at most VK_DELTA_CHAIN long (DS-2). */
static void body_func(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    VkStore *p = (VkStore*)sqlite3_user_data(ctx);
    uint8_t *aD[VK_DELTA_CHAIN+1];
    uint32_t anD[VK_DELTA_CHAIN+1];
    int n = 0, rc = SQLITE_OK, i;
    uint8_t *cur = 0;
    uint32_t nCur = 0;
    sqlite3_int64 seq;
    (void)argc;
    if( sqlite3_value_type(argv[0])==SQLITE_NULL ){ sqlite3_result_null(ctx); return; }
    if( sqlite3_value_type(argv[1])==SQLITE_NULL ){ sqlite3_result_value(ctx, argv[0]); return; }
    seq = sqlite3_value_int64(argv[1]);
    anD[0] = (uint32_t)sqlite3_value_bytes(argv[0]);
    aD[0] = (uint8_t*)sqlite3_malloc64(anD[0] ? anD[0] : 1);
    if( !aD[0] ){ sqlite3_result_error_nomem(ctx); return; }
    memcpy(aD[0], sqlite3_value_blob(argv[0]), anD[0]);
    n = 1;
    /* Walk to the whole body the chain rests on, keeping each delta on the way. */
    for(;;){
        const uint8_t *z;
        uint32_t nz;
        int step;
        sqlite3_bind_int64(p->pBody, 1, seq);
        step = vk_step(p->pBody);
        if( step!=SQLITE_ROW ){ rc = step==SQLITE_DONE ? SQLITE_CORRUPT : step; break; }
        if( sqlite3_column_type(p->pBody, 0)==SQLITE_NULL ){ rc = SQLITE_CORRUPT; break; }
        z = (const uint8_t*)sqlite3_column_blob(p->pBody, 0);
        nz = (uint32_t)sqlite3_column_bytes(p->pBody, 0);
        if( sqlite3_column_type(p->pBody, 1)==SQLITE_NULL ){
            cur = (uint8_t*)sqlite3_malloc64(nz ? nz : 1);
            if( !cur ) rc = SQLITE_NOMEM;
            else{ memcpy(cur, z, nz); nCur = nz; }
            break;
        }
        if( n==VK_DELTA_CHAIN+1 ){ rc = SQLITE_CORRUPT; break; }   /* DS-2 broken: refuse */
        aD[n] = (uint8_t*)sqlite3_malloc64(nz ? nz : 1);
        if( !aD[n] ){ rc = SQLITE_NOMEM; break; }
        memcpy(aD[n], z, nz);
        anD[n++] = nz;
        seq = sqlite3_column_int64(p->pBody, 1);
        sqlite3_reset(p->pBody);
    }
    sqlite3_reset(p->pBody);
    for(i=n-1; rc==SQLITE_OK && i>=0; i--){
        uint8_t *next = 0;
        uint32_t nNext = 0;
        rc = vk_delta_apply(cur, nCur, aD[i], anD[i], &next, &nNext);
        sqlite3_free(cur);
        cur = next; nCur = nNext;
    }
    for(i=0; i<n; i++) sqlite3_free(aD[i]);
    if( rc!=SQLITE_OK ){
        sqlite3_free(cur);
        sqlite3_result_error_code(ctx, rc);
        return;
    }
    sqlite3_result_blob(ctx, cur, (int)nCur, sqlite3_free);
}

/* A store made before DS has no dbase column. */
static int ensure_dbase(VkStore *p){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT count(*) FROM pragma_table_info('assertion') WHERE name='dbase'", &pSt);
    int bHas = 0;
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ) bHas = sqlite3_column_int(pSt, 0)>0;
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_ROW ) return rc;
    if( !bHas ){
        rc = vk_prepare(p, "ALTER TABLE assertion ADD COLUMN dbase INTEGER", &pSt);
        if( rc!=SQLITE_OK ) return rc;
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        if( rc!=SQLITE_DONE ) return rc;
    }
    rc = vk_prepare(p, "CREATE INDEX IF NOT EXISTS assertion_dbase ON assertion(dbase) WHERE dbase IS NOT NULL", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
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
    if( rc==SQLITE_OK ) rc = sqlite3_create_function(db, "vk_body", 2, SQLITE_UTF8, p, body_func, 0, 0);
    if( rc==SQLITE_OK ) rc = exec_schema(p);
    if( rc==SQLITE_OK ) rc = ensure_dbase(p);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "SELECT body, dbase FROM assertion WHERE seq=?1", &p->pBody);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "SAVEPOINT vk", &p->pBegin);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "RELEASE vk", &p->pCommit);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "ROLLBACK TO vk", &p->pUndo);
    if( rc==SQLITE_OK ) rc = vk_prepare(p, "RELEASE vk", &p->pEnd);
    if( rc==SQLITE_OK ) rc = sync_arms(p);
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

static int insert_links(VkStore *p, const uint8_t *id, sqlite3_int64 seq, int rel, const VkId *a, uint32_t n){
    sqlite3_stmt *pSt = 0;
    uint32_t i;
    int rc;
    if( n==0 ) return SQLITE_OK;
    rc = vk_prepare(p, "INSERT OR IGNORE INTO edge(parent,rel,child,child_seq) VALUES(?1,?2,?3,?4)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    for(i=0; i<n; i++){
        sqlite3_bind_blob(pSt, 1, a[i], VK_ID_BYTES, SQLITE_STATIC);
        sqlite3_bind_int(pSt, 2, rel);
        sqlite3_bind_blob(pSt, 3, id, VK_ID_BYTES, SQLITE_STATIC);
        sqlite3_bind_int64(pSt, 4, seq);
        rc = vk_step(pSt);
        if( rc!=SQLITE_DONE ) break;
        rc = SQLITE_OK;
        sqlite3_reset(pSt);
    }
    sqlite3_finalize(pSt);
    return rc;
}

/* ---- ranges ---- */

#define VK_IN_RANGE(t) \
    t "log=?1 AND " t "log<>x'' AND " t "author=?2 AND " t "pos BETWEEN ?3 AND ?4" \
    " AND " t "kind NOT IN " VK_TOMB_KINDS

static void bind_range(sqlite3_stmt *pSt, VkBytes log, VkBytes target, uint64_t from, uint64_t to){
    vk_bind_bytes(pSt, 1, log);
    vk_bind_bytes(pSt, 2, target);
    sqlite3_bind_int64(pSt, 3, (sqlite3_int64)from);
    sqlite3_bind_int64(pSt, 4, (sqlite3_int64)to);
}

static int run_range(VkStore *p, const char *zSql, VkBytes log, VkBytes target, uint64_t from, uint64_t to){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, zSql, &pSt);
    if( rc!=SQLITE_OK ) return rc;
    bind_range(pSt, log, target, from, to);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

/* The row goes entirely. Edges from a surviving child to a gone parent stay:
** that child's frame names them and its id must still recompute. */
static int delete_range(VkStore *p, const VkAssertion *a){
    uint64_t from = get_be64(a->body.p);
    /* DS-4 needs nothing here: deltas pair unlogged rows only (store_deltas), so
    ** no stored body rests on a logged row, and no logged row is a delta. */
    int rc = run_range(p, "DELETE FROM arm WHERE (akey,id) IN (SELECT akey,id FROM assertion WHERE " VK_IN_RANGE("") ")",
                   a->log, a->reference, from, a->pos);
    if( rc==SQLITE_OK ) rc = run_range(p, "DELETE FROM edge WHERE child IN (SELECT id FROM assertion WHERE " VK_IN_RANGE("") ")",
                   a->log, a->reference, from, a->pos);
    if( rc==SQLITE_OK ) rc = run_range(p, "DELETE FROM assertion WHERE " VK_IN_RANGE(""),
                   a->log, a->reference, from, a->pos);
    return rc;
}

static int record_range(VkStore *p, const VkAssertion *a, const VkId id, sqlite3_int64 seq){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "INSERT OR IGNORE INTO range_tomb(log,target,pto,pfrom,author,id,seq)"
                           " VALUES(?1,?2,?3,?4,?5,?6,?7)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    bind_range(pSt, a->log, a->reference, a->pos, get_be64(a->body.p));
    vk_bind_bytes(pSt, 5, a->author);
    sqlite3_bind_blob(pSt, 6, id, VK_ID_BYTES, SQLITE_STATIC);
    sqlite3_bind_int64(pSt, 7, seq);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

/* W-16. Rights are checked here, at use, like every other fold (R-4a). */
static int is_covered(VkStore *p, const VkAssertion *a, int *pbCovered){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT 1 FROM range_tomb WHERE log=?1 AND target=?2 AND pto>=?3"
                           " AND pfrom<=?3 AND vk_rights(author)&1 LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    vk_bind_bytes(pSt, 1, a->log);
    vk_bind_bytes(pSt, 2, a->author);
    sqlite3_bind_int64(pSt, 3, (sqlite3_int64)a->pos);
    rc = vk_step(pSt);
    *pbCovered = rc==SQLITE_ROW;
    sqlite3_finalize(pSt);
    return (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
}

static int update_arms(VkStore *p, const VkAssertion *a, const VkId id, sqlite3_int64 seq){
    sqlite3_stmt *pSt = 0;
    uint32_t i;
    int right = (bytes_is(a->kind,VK_TOMBSTONE) || bytes_is(a->kind,VK_RANGE)) ? VK_ERASE
              : a->nSupersedes ? VK_SUPERSEDE : VK_ORIGINATE;
    int rc;
    if( !(rights_of(p, a->author) & right) ) return SQLITE_OK;   /* stored and ignored (R-4a) */
    if( a->nSupersedes ){
        rc = vk_prepare(p, "DELETE FROM arm WHERE akey=?1 AND id=?2", &pSt);
        if( rc!=SQLITE_OK ) return rc;
        vk_bind_bytes(pSt, 1, a->akey);
        for(i=0; i<a->nSupersedes; i++){
            sqlite3_bind_blob(pSt, 2, a->supersedes[i], VK_ID_BYTES, SQLITE_STATIC);
            rc = vk_step(pSt);
            if( rc!=SQLITE_DONE ) break;
            rc = SQLITE_OK;
            sqlite3_reset(pSt);
        }
        sqlite3_finalize(pSt);
        if( rc!=SQLITE_OK ) return rc;
    }
    /* A child may have arrived first. */
    rc = vk_prepare(p, "INSERT OR IGNORE INTO arm(akey,id,seq) SELECT ?1,?2,?3 WHERE " VK_NO_VALID_CHILD("?2","?1"), &pSt);
    if( rc!=SQLITE_OK ) return rc;
    vk_bind_bytes(pSt, 1, a->akey);
    sqlite3_bind_blob(pSt, 2, id, VK_ID_BYTES, SQLITE_STATIC);
    sqlite3_bind_int64(pSt, 3, seq);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

static int run_seq(VkStore *p, const char *zSql, sqlite3_int64 seq){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, zSql, &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_int64(pSt, 1, seq);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

/* W-3 on one row. DS-4: a body stored as a delta from this one is made whole
** first, so erasing loses nothing it was not asked to. */
static int forget_body(VkStore *p, sqlite3_int64 seq){
    sqlite3_stmt *pSt = 0;
    int rc = run_seq(p, "UPDATE assertion SET body=" VK_BODY("") ", dbase=NULL WHERE dbase=?1", seq);
    if( rc==SQLITE_OK ) rc = run_seq(p, "UPDATE assertion SET body=NULL, dbase=NULL WHERE seq=?1", seq);
    if( rc!=SQLITE_OK ) return rc;
    /* An erased outline has no nodes (OL-4). */
    rc = vk_prepare(p, "SELECT akey FROM assertion WHERE seq=?1 AND kind=CAST('" VK_OUTLINE "' AS BLOB)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_int64(pSt, 1, seq);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        uint8_t aKey[VK_MAX_FIELD];
        VkBytes k = column_bytes(pSt, 0), kc;
        if( k.n<=VK_MAX_FIELD ){
            memcpy(aKey, k.p, k.n);
            kc.p = aKey; kc.n = k.n;
            sqlite3_finalize(pSt);
            return refold_outline(p, kc);
        }
    }
    sqlite3_finalize(pSt);
    return (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
}

/* W-3 for a target known only by id. */
static int erase_by_id(VkStore *p, const uint8_t *target){
    static const char *azFind[2] = {
        "SELECT seq FROM assertion WHERE id=?1 AND log=x'' AND kind NOT IN " VK_TOMB_KINDS,
        /* LOG-4's price: a logged row by id is a scan of the position index. */
        "SELECT seq FROM assertion INDEXED BY assertion_log WHERE log<>x'' AND id=?1"
        " AND kind NOT IN " VK_TOMB_KINDS
    };
    int i, rc = SQLITE_OK;
    for(i=0; i<2; i++){
        sqlite3_stmt *pSt = 0;
        sqlite3_int64 seq = 0;
        rc = vk_prepare(p, azFind[i], &pSt);
        if( rc!=SQLITE_OK ) return rc;
        sqlite3_bind_blob(pSt, 1, target, VK_ID_BYTES, SQLITE_STATIC);
        rc = vk_step(pSt);
        if( rc==SQLITE_ROW ) seq = sqlite3_column_int64(pSt, 0);
        sqlite3_finalize(pSt);
        if( rc==SQLITE_ROW ) return forget_body(p, seq);
        if( rc!=SQLITE_DONE ) return rc;
    }
    return SQLITE_OK;
}

/* The chain of deltas under row seq, walked up to the bound: how many rows
** must be rebuilt to read it. 0 when it is whole. */
static int chain_depth(VkStore *p, sqlite3_int64 seq, int *pn){
    int rc = SQLITE_OK;
    *pn = 0;
    while( *pn<=VK_DELTA_CHAIN ){
        sqlite3_int64 next;
        sqlite3_bind_int64(p->pBody, 1, seq);
        rc = vk_step(p->pBody);
        if( rc!=SQLITE_ROW ){ sqlite3_reset(p->pBody); return rc==SQLITE_DONE ? SQLITE_CORRUPT : rc; }
        if( sqlite3_column_type(p->pBody, 1)==SQLITE_NULL ){ sqlite3_reset(p->pBody); return SQLITE_OK; }
        next = sqlite3_column_int64(p->pBody, 1);
        sqlite3_reset(p->pBody);
        seq = next;
        (*pn)++;
    }
    return SQLITE_OK;
}

/* DS-1: store row x as a reverse delta from row b, which supersedes it, when
** that is worth doing: x is whole, not small, the delta is at most 3/4 of it,
** and no chain through x grows past VK_DELTA_CHAIN (DS-2). Otherwise x stays
** whole, and that is a correct store too. */
static int try_delta(VkStore *p, sqlite3_int64 x, sqlite3_int64 b){
    sqlite3_stmt *pSt = 0;
    uint8_t *aX = 0, *aB = 0, *aD = 0;
    uint32_t nX = 0, nB = 0, nD = 0;
    int rc, depthB = 0, height = 0;

    rc = vk_prepare(p, "SELECT body FROM assertion WHERE seq=?1 AND dbase IS NULL AND body IS NOT NULL"
                       " AND length(body)>=?2 AND kind NOT IN " VK_TOMB_KINDS, &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_int64(pSt, 1, x);
    sqlite3_bind_int(pSt, 2, VK_DELTA_MIN);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        nX = (uint32_t)sqlite3_column_bytes(pSt, 0);
        aX = (uint8_t*)sqlite3_malloc64(nX);
        if( aX ) memcpy(aX, sqlite3_column_blob(pSt, 0), nX);
        else rc = SQLITE_NOMEM;
    }
    sqlite3_finalize(pSt);
    if( rc==SQLITE_DONE ) return SQLITE_OK;          /* nothing worth a delta */
    if( rc!=SQLITE_ROW ) goto done;

    rc = chain_depth(p, b, &depthB);
    if( rc!=SQLITE_OK ) goto done;
    rc = vk_prepare(p,
        "WITH RECURSIVE d(seq, n) AS (SELECT seq, 1 FROM assertion WHERE dbase=?1"
        " UNION ALL SELECT a.seq, d.n+1 FROM assertion a JOIN d ON a.dbase=d.seq WHERE d.n<?2)"
        " SELECT coalesce(max(n),0) FROM d", &pSt);
    if( rc!=SQLITE_OK ) goto done;
    sqlite3_bind_int64(pSt, 1, x);
    sqlite3_bind_int(pSt, 2, VK_DELTA_CHAIN+1);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ) height = sqlite3_column_int(pSt, 0);
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_ROW ) goto done;
    rc = SQLITE_OK;
    if( height + 1 + depthB > VK_DELTA_CHAIN ) goto done;   /* x stays whole: a keyframe */

    rc = vk_prepare(p, "SELECT " VK_BODY("") " FROM assertion WHERE seq=?1 AND body IS NOT NULL AND log=x''", &pSt);
    if( rc!=SQLITE_OK ) goto done;
    sqlite3_bind_int64(pSt, 1, b);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        nB = (uint32_t)sqlite3_column_bytes(pSt, 0);
        aB = (uint8_t*)sqlite3_malloc64(nB ? nB : 1);
        if( aB ) memcpy(aB, sqlite3_column_blob(pSt, 0), nB);
        else rc = SQLITE_NOMEM;
    }
    sqlite3_finalize(pSt);
    if( rc==SQLITE_DONE ){ rc = SQLITE_OK; goto done; }    /* b is erased or logged: x stays whole */
    if( rc!=SQLITE_ROW ) goto done;

    rc = vk_delta_make(aB, nB, aX, nX, &aD, &nD);
    if( rc!=SQLITE_OK ) goto done;
    if( (uint64_t)nD*4 > (uint64_t)nX*3 ) goto done;       /* not worth it */
    rc = vk_prepare(p, "UPDATE assertion SET body=?1, dbase=?2 WHERE seq=?3", &pSt);
    if( rc!=SQLITE_OK ) goto done;
    sqlite3_bind_blob(pSt, 1, aD, (int)nD, SQLITE_STATIC);
    sqlite3_bind_int64(pSt, 2, b);
    sqlite3_bind_int64(pSt, 3, x);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
done:
    sqlite3_free(aX);
    sqlite3_free(aB);
    sqlite3_free(aD);
    return rc;
}

/* DS-1 and DS-3 for a row just stored at seq: each row it supersedes may become
** a delta from it, and it may become a delta from a held row that supersedes it
** (one that arrived first). Deltas pair unlogged rows only: a logged row is
** found by position, a record is rarely superseded, and a range tombstone can
** then delete logged rows with no body resting on them. */
static int store_deltas(VkStore *p, const VkAssertion *a, const VkId id, sqlite3_int64 seq){
    sqlite3_stmt *pSt = 0;
    uint32_t i;
    int rc = SQLITE_OK;
    if( a->log.n || a->erased ) return SQLITE_OK;
    for(i=0; rc==SQLITE_OK && i<a->nSupersedes; i++){
        sqlite3_int64 x = 0;
        rc = vk_prepare(p, "SELECT seq FROM assertion WHERE id=?1 AND log=x''", &pSt);
        if( rc!=SQLITE_OK ) return rc;
        sqlite3_bind_blob(pSt, 1, a->supersedes[i], VK_ID_BYTES, SQLITE_STATIC);
        rc = vk_step(pSt);
        if( rc==SQLITE_ROW ) x = sqlite3_column_int64(pSt, 0);
        sqlite3_finalize(pSt);
        if( rc==SQLITE_ROW ) rc = try_delta(p, x, seq);
        else if( rc==SQLITE_DONE ) rc = SQLITE_OK;
    }
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_prepare(p, "SELECT child_seq FROM edge WHERE parent=?1 AND rel=0 ORDER BY child_seq LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_blob(pSt, 1, id, VK_ID_BYTES, SQLITE_STATIC);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        sqlite3_int64 b = sqlite3_column_int64(pSt, 0);
        sqlite3_finalize(pSt);
        return b==seq ? SQLITE_OK : try_delta(p, seq, b);
    }
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? SQLITE_OK : rc;
}

/* Stores the row and applies whatever erasure it triggers, in one savepoint.
** *pbAdded is 1 if stored, 0 if already held, -1 if a range tombstone covers it. */
static int store_row(VkStore *p, const VkAssertion *a, const VkId id, int *pbAdded){
    static const char zInsert[] =
        "INSERT OR IGNORE INTO assertion(id,author,kind,akey,ts,rank,reference,body_hash,body,log,pos,skip)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)";
    sqlite3_stmt *pSt = 0;
    sha256_ctx c;
    uint8_t aHash[VK_ID_BYTES];
    int bTomb = bytes_is(a->kind, VK_TOMBSTONE);
    int bRange = bytes_is(a->kind, VK_RANGE);
    sqlite3_int64 seq = 0;
    int rc;
    if( a->log.n && !bRange ){
        int bCovered = 0;
        rc = is_covered(p, a, &bCovered);
        if( rc!=SQLITE_OK ) return rc;
        if( bCovered ){ *pbAdded = -1; return SQLITE_OK; }
    }
    rc = begin(p);
    if( rc!=SQLITE_OK ) return rc;

    /* Deleting first, because on a full database the deleted rows' pages are
    ** the only room there is for the tombstone (W-15). */
    if( bRange && (rights_of(p, a->author) & VK_ERASE) ){
        rc = delete_range(p, a);
        if( rc!=SQLITE_OK ){ undo(p); return rc; }
    }

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
        vk_bind_bytes(pSt, 10, a->log);
        sqlite3_bind_int64(pSt, 11, (sqlite3_int64)a->pos);
        sqlite3_bind_blob(pSt, 12, a->nSkip ? (const void*)a->skip : (const void*)"",
                          (int)(a->nSkip*VK_ID_BYTES), SQLITE_STATIC);
        rc = vk_step(pSt);
        sqlite3_finalize(pSt);
        rc = rc==SQLITE_DONE ? SQLITE_OK : rc;
    }
    *pbAdded = 0;
    if( rc==SQLITE_OK ) *pbAdded = sqlite3_changes(p->db)>0;
    if( rc==SQLITE_OK && *pbAdded ) seq = sqlite3_last_insert_rowid(p->db);
    if( rc==SQLITE_OK && *pbAdded ){
        rc = insert_links(p, id, seq, 0, a->supersedes, a->nSupersedes);
        if( rc==SQLITE_OK ) rc = insert_links(p, id, seq, 1, a->derivedFrom, a->nDerivedFrom);
        if( rc==SQLITE_OK ) rc = update_arms(p, a, id, seq);
    }
    if( rc==SQLITE_OK && *pbAdded ){
        if( bRange ){
            if( rights_of(p, a->author) & VK_ERASE ) rc = record_range(p, a, id, seq);
        }else if( bTomb && (rights_of(p, a->author) & VK_ERASE) ){
            rc = erase_by_id(p, a->body.p);    /* W-2: never a tombstone */
        }else if( !bTomb ){
            /* W-6: the tombstone may have arrived first. */
            sqlite3_stmt *pUp = 0;
            int bTombed = 0;
            rc = vk_prepare(p, "SELECT 1 FROM arm x JOIN assertion t ON t.seq=x.seq WHERE x.akey=?1"
                               " AND t.kind=CAST('" VK_TOMBSTONE "' AS BLOB)", &pUp);
            if( rc==SQLITE_OK ){
                sqlite3_bind_blob(pUp, 1, id, VK_ID_BYTES, SQLITE_STATIC);
                rc = vk_step(pUp);
                bTombed = rc==SQLITE_ROW;
                sqlite3_finalize(pUp);
                rc = (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
            }
            if( rc==SQLITE_OK && bTombed ) rc = forget_body(p, seq);
            else if( rc==SQLITE_OK ) rc = store_deltas(p, a, id, seq);
            if( rc==SQLITE_OK && !bTombed && bytes_is(a->kind, VK_OUTLINE) ) rc = refold_outline(p, a->akey);
        }
    }
    if( rc==SQLITE_OK ) rc = commit(p);
    if( rc!=SQLITE_OK ) undo(p);
    return rc;
}

/* ---- OL-1: an outline's body ---- */

static const char *const azOutlineBad[] = {
    0,
    "OL-1: an outline is a JSON list of nodes",
    "OL-1: every node is an object",
    "OL-1: a node has a text title, integer lo and hi, optional children, and nothing else",
    "OL-1: a node's range is lo < hi, starting from 0",
    "OL-1: a child lies inside its parent",
    "OL-1: siblings are in order and do not overlap",
    "OL-1: a range runs past the end of the target's body",
};

/* SQLite reads the JSON, so there is no parser of ours. The body is bound as
** text: a BLOB given to the JSON functions may be read as JSONB, and an outline
** is JSON text. A list anywhere but a node's children is caught as a node that
** is not an object, or as a field a node may not have. nTarget < 0 skips the
** range check, for a target this store does not hold. */
static int outline_body_error(VkStore *p, VkBytes body, sqlite3_int64 nTarget, const char **pzBad){
    static const char zSql[] =
        "WITH t AS (SELECT * FROM json_tree(?1)),"
        " nodes AS (SELECT o.id, o.parent AS arr, CAST(o.key AS INTEGER) AS ix,"
        "   json_type(o.value,'$.title') AS tt, json_type(o.value,'$.lo') AS tl,"
        "   json_type(o.value,'$.hi') AS th, json_type(o.value,'$.children') AS tc,"
        "   json_extract(o.value,'$.lo') AS lo, json_extract(o.value,'$.hi') AS hi,"
        "   (SELECT count(*) FROM json_each(o.value)) AS nk FROM t o WHERE o.type='object'),"
        " arrs AS (SELECT id, parent FROM t WHERE type='array')"
        " SELECT CASE"
        "  WHEN json_type(?1)<>'array' THEN 1"
        "  WHEN EXISTS(SELECT 1 FROM t WHERE parent IN (SELECT id FROM arrs) AND type<>'object') THEN 2"
        "  WHEN EXISTS(SELECT 1 FROM nodes WHERE tt IS NOT 'text' OR tl IS NOT 'integer' OR th IS NOT 'integer'"
        "    OR (tc IS NOT NULL AND tc<>'array') OR nk<>3+(tc IS NOT NULL)) THEN 3"
        "  WHEN EXISTS(SELECT 1 FROM nodes WHERE lo<0 OR hi<=lo) THEN 4"
        "  WHEN EXISTS(SELECT 1 FROM nodes c JOIN arrs a ON a.id=c.arr JOIN nodes q ON q.id=a.parent"
        "    WHERE c.lo<q.lo OR c.hi>q.hi) THEN 5"
        "  WHEN EXISTS(SELECT 1 FROM nodes x JOIN nodes y ON y.arr=x.arr AND y.ix=x.ix+1 WHERE y.lo<x.hi) THEN 6"
        "  WHEN ?2>=0 AND (SELECT max(hi) FROM nodes)>?2 THEN 7"
        "  ELSE 0 END";
    sqlite3_stmt *pSt = 0;
    int rc, code = 0;
    *pzBad = 0;
    rc = vk_prepare(p, "SELECT json_valid(?1)", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_text(pSt, 1, body.n ? (const char*)body.p : "", (int)body.n, SQLITE_STATIC);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW && !sqlite3_column_int(pSt, 0) ) code = 1;
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_ROW ) return rc;
    if( !code ){
        rc = vk_prepare(p, zSql, &pSt);
        if( rc!=SQLITE_OK ) return rc;
        sqlite3_bind_text(pSt, 1, body.n ? (const char*)body.p : "", (int)body.n, SQLITE_STATIC);
        sqlite3_bind_int64(pSt, 2, nTarget);
        rc = vk_step(pSt);
        if( rc==SQLITE_ROW ) code = sqlite3_column_int(pSt, 0);
        sqlite3_finalize(pSt);
        if( rc!=SQLITE_ROW ) return rc;
    }
    if( code>0 && code<(int)(sizeof(azOutlineBad)/sizeof(azOutlineBad[0])) ) *pzBad = azOutlineBad[code];
    return SQLITE_OK;
}

/* The length of the target's body, -1 if not held, -2 if erased. */
static int target_length(VkStore *p, const uint8_t *target, sqlite3_int64 *pn){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p,
        "SELECT body IS NULL, length(" VK_BODY("") ") FROM assertion WHERE id=?1 AND log=x''"
        " UNION ALL SELECT body IS NULL, length(" VK_BODY("") ") FROM assertion INDEXED BY assertion_log"
        " WHERE log<>x'' AND id=?1 LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    sqlite3_bind_blob(pSt, 1, target, VK_ID_BYTES, SQLITE_STATIC);
    rc = vk_step(pSt);
    *pn = -1;
    if( rc==SQLITE_ROW ) *pn = sqlite3_column_int(pSt, 0) ? -2 : sqlite3_column_int64(pSt, 1);
    sqlite3_finalize(pSt);
    return (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
}

/* OL-1 on a body. bWrite: the writer must hold a readable target, and the
** ranges are checked against it. On arrival the target may come later, so
** only a held target's length is checked. */
static int outline_error(VkStore *p, const VkAssertion *a, int bWrite, const char **pzBad){
    sqlite3_int64 n = -1;
    int rc;
    *pzBad = 0;
    if( !bytes_is(a->kind, VK_OUTLINE) || a->erased ) return SQLITE_OK;
    rc = target_length(p, a->reference.p, &n);
    if( rc!=SQLITE_OK ) return rc;
    if( bWrite && n==-1 ){ *pzBad = "OL-1: the outline's target is not held"; return SQLITE_OK; }
    if( bWrite && n==-2 ){ *pzBad = "OL-1: the outline's target is erased"; return SQLITE_OK; }
    return outline_body_error(p, a->body, n<0 ? -1 : n, pzBad);
}

/* ---- write ---- */

static int needed_right(const VkAssertion *a){
    if( bytes_is(a->kind, VK_TOMBSTONE) || bytes_is(a->kind, VK_RANGE) ) return VK_ERASE;
    return a->nSupersedes ? VK_SUPERSEDE : VK_ORIGINATE;
}

VkStatus vk_put(VkStore *p, const VkAssertion *a, VkId idOut){
    const char *zBad = shape_error(a);
    int bAdded = 0, rc;
    if( zBad ) return vk_refuse(p, VK_EINVAL, zBad);
    if( a->erased ) return vk_refuse(p, VK_EINVAL, "A-6: a write cannot arrive erased");
    if( a->log.n && !bytes_is(a->kind, VK_RANGE) )
        return vk_refuse(p, VK_EINVAL, "LOG-1: a logged row is written by vk_append");
    if( !bytes_eq(a->author, p->principal) )
        return vk_refuse(p, VK_EDENIED, "I-1: the author must be this store's principal");
    if( !vk_has_right(p, needed_right(a)) )
        return vk_refuse(p, VK_EDENIED, "D-3: the principal lacks the right this write needs");
    rc = outline_error(p, a, 1, &zBad);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( zBad ) return vk_refuse(p, VK_EINVAL, zBad);
    vk_assertion_id(a, idOut);
    rc = store_row(p, a, idOut, &bAdded);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    return bAdded==1 ? VK_OK : VK_EXISTS;
}

#define VK_HEADS(k) \
    "SELECT a.id, a.rank, " VK_BODY("a.") " AS body FROM arm x JOIN assertion a ON a.seq=x.seq WHERE x.akey=" k

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

/* The id at (author, log, t): a held row, else one a range tombstone carries.
** VK_OK, VK_NOTFOUND, or a failure. */
static VkStatus log_id(VkStore *p, VkBytes author, VkBytes log, uint64_t t, VkId out){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT id FROM assertion WHERE log=?1 AND log<>x'' AND author=?2 AND pos=?3"
                           " AND kind NOT IN " VK_TOMB_KINDS " ORDER BY id DESC LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, log);
    vk_bind_bytes(pSt, 2, author);
    sqlite3_bind_int64(pSt, 3, (sqlite3_int64)t);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ) memcpy(out, sqlite3_column_blob(pSt, 0), VK_ID_BYTES);
    sqlite3_finalize(pSt);
    if( rc==SQLITE_ROW ) return VK_OK;
    if( rc!=SQLITE_DONE ) return vk_sql_status(p, rc);

    rc = vk_prepare(p, "SELECT r.pto, a.body FROM range_tomb r JOIN assertion a ON a.seq=r.seq"
                       " WHERE r.log=?1 AND r.target=?2 AND r.pto>=?3 AND vk_rights(r.author)&1"
                       " ORDER BY r.pto DESC", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, log);
    vk_bind_bytes(pSt, 2, author);
    sqlite3_bind_int64(pSt, 3, (sqlite3_int64)t);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        uint64_t q = (uint64_t)sqlite3_column_int64(pSt, 0);
        const uint8_t *pIds = (const uint8_t*)sqlite3_column_blob(pSt, 1) + 8;
        for(; q; q&=q-1, pIds+=VK_ID_BYTES){
            if( q==t ){
                memcpy(out, pIds, VK_ID_BYTES);
                sqlite3_finalize(pSt);
                return VK_OK;
            }
        }
    }
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? VK_NOTFOUND : vk_sql_status(p, rc);
}

/* The highest position used, by a held row or inside a range tombstone. */
static VkStatus log_head(VkStore *p, VkBytes author, VkBytes log, uint64_t *pPos){
    static const char *azSql[2] = {
        "SELECT pos FROM assertion WHERE log=?1 AND log<>x'' AND author=?2"
        " AND kind NOT IN " VK_TOMB_KINDS " ORDER BY pos DESC LIMIT 1",
        "SELECT pto FROM range_tomb WHERE log=?1 AND target=?2 AND vk_rights(author)&1"
        " ORDER BY pto DESC LIMIT 1"
    };
    int i;
    *pPos = 0;
    for(i=0; i<2; i++){
        sqlite3_stmt *pSt = 0;
        int rc = vk_prepare(p, azSql[i], &pSt);
        if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
        vk_bind_bytes(pSt, 1, log);
        vk_bind_bytes(pSt, 2, author);
        rc = vk_step(pSt);
        if( rc==SQLITE_ROW && (uint64_t)sqlite3_column_int64(pSt, 0)>*pPos ){
            *pPos = (uint64_t)sqlite3_column_int64(pSt, 0);
        }
        sqlite3_finalize(pSt);
        if( rc!=SQLITE_ROW && rc!=SQLITE_DONE ) return vk_sql_status(p, rc);
    }
    return VK_OK;
}

VkStatus vk_log_span(VkStore *p, VkBytes log, VkBytes author, uint64_t *pLow, uint64_t *pHigh){
    static const char *azSql[2] = {
        "SELECT pos FROM assertion WHERE log=?1 AND log<>x'' AND author=?2"
        " AND kind NOT IN " VK_TOMB_KINDS " ORDER BY pos ASC LIMIT 1",
        "SELECT pos FROM assertion WHERE log=?1 AND log<>x'' AND author=?2"
        " AND kind NOT IN " VK_TOMB_KINDS " ORDER BY pos DESC LIMIT 1"
    };
    int i;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    for(i=0; i<2; i++){
        sqlite3_stmt *pSt = 0;
        uint64_t v = 0;
        int rc = vk_prepare(p, azSql[i], &pSt);
        if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
        vk_bind_bytes(pSt, 1, log);
        vk_bind_bytes(pSt, 2, author);
        rc = vk_step(pSt);
        if( rc==SQLITE_ROW ) v = (uint64_t)sqlite3_column_int64(pSt, 0);
        sqlite3_finalize(pSt);
        if( rc==SQLITE_DONE ) return VK_NOTFOUND;
        if( rc!=SQLITE_ROW ) return vk_sql_status(p, rc);
        if( i==0 ) *pLow = v; else *pHigh = v;
    }
    return VK_OK;
}

VkStatus vk_append(VkStore *p, const VkAssertion *a, VkId idOut){
    VkId aSkip[VK_MAX_SKIP];
    VkAssertion b = *a;
    uint64_t head = 0;
    uint32_t j;
    const char *zBad;
    VkStatus st;
    int bAdded = 0, rc;
    if( a->log.n==0 ) return vk_refuse(p, VK_EINVAL, "LOG-1: vk_append needs a log");
    if( a->erased ) return vk_refuse(p, VK_EINVAL, "A-6: a write cannot arrive erased");
    if( !bytes_eq(a->author, p->principal) )
        return vk_refuse(p, VK_EDENIED, "I-1: the author must be this store's principal");
    if( !vk_has_right(p, needed_right(a)) )
        return vk_refuse(p, VK_EDENIED, "D-3: the principal lacks the right this write needs");
    st = log_head(p, a->author, a->log, &head);
    if( st!=VK_OK ) return st;
    if( head>=VK_MAX_POS ) return vk_refuse(p, VK_EINVAL, "LOG-1: the log is full");
    b.pos = head+1;
    b.nSkip = skip_count(b.pos);
    for(j=0; j<b.nSkip; j++){
        st = log_id(p, a->author, a->log, b.pos-((uint64_t)1<<j), aSkip[j]);
        if( st==VK_NOTFOUND ) return vk_refuse(p, VK_EINVAL, "LOG-2: a position to link to is not held");
        if( st!=VK_OK ) return st;
    }
    b.skip = (const VkId*)aSkip;
    zBad = shape_error(&b);
    if( zBad ) return vk_refuse(p, VK_EINVAL, zBad);
    vk_assertion_id(&b, idOut);
    rc = store_row(p, &b, idOut, &bAdded);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    return bAdded==1 ? VK_OK : VK_EXISTS;
}

VkStatus vk_forget(VkStore *p, VkBytes log, VkBytes target, uint64_t from, uint64_t to,
                   VkBytes ts, const uint8_t rank[VK_RANK_BYTES], VkId idOut){
    uint8_t aBody[8+VK_MAX_SKIP*VK_ID_BYTES];
    VkId aKey;
    VkAssertion a;
    sqlite3_stmt *pSt = 0;
    uint64_t q;
    uint32_t n = 0;
    int i, rc;
    if( !vk_has_right(p, VK_ERASE) )
        return vk_refuse(p, VK_EDENIED, "D-3: the principal lacks the right this write needs");
    if( from<1 || from>to || to>VK_MAX_POS ) return vk_refuse(p, VK_EINVAL, "W-15: malformed range");

    rc = vk_prepare(p, "SELECT 1 FROM edge e JOIN assertion c ON c.id=e.child"
                       " WHERE e.rel=0 AND c.log=?1 AND c.log<>x'' AND c.author=?2 AND c.pos BETWEEN ?3 AND ?4"
                       " AND c.kind NOT IN " VK_TOMB_KINDS
                       " AND EXISTS(SELECT 1 FROM assertion o WHERE o.id=e.parent AND NOT (" VK_IN_RANGE("o.") ")) LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    bind_range(pSt, log, target, from, to);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    if( rc==SQLITE_ROW ) return vk_refuse(p, VK_EINVAL, "W-17: the range supersedes a row outside it");
    if( rc!=SQLITE_DONE ) return vk_sql_status(p, rc);

    for(i=0; i<8; i++) aBody[i] = (uint8_t)(from>>(56-8*i));
    for(q=to; q; q&=q-1, n++){
        VkStatus st = log_id(p, target, log, q, *(VkId*)(aBody+8+n*VK_ID_BYTES));
        if( st==VK_NOTFOUND ) return vk_refuse(p, VK_EINVAL, "W-15: an id the tombstone must carry is not held");
        if( st!=VK_OK ) return st;
    }
    log_key(target, log, aKey);
    memset(&a, 0, sizeof(a));
    a.author = p->principal;
    a.kind.p = (const uint8_t*)VK_RANGE;
    a.kind.n = (uint32_t)strlen(VK_RANGE);
    a.akey.p = aKey;  a.akey.n = VK_ID_BYTES;
    a.ts = ts;
    memcpy(a.rank, rank, VK_RANK_BYTES);
    a.reference = target;
    a.log = log;
    a.pos = to;
    a.body.p = aBody;  a.body.n = 8+n*VK_ID_BYTES;
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

#define VK_ROW_COLUMNS "seq,id,author,kind,akey,ts,rank,reference,body_hash," VK_BODY("") ",log,pos,skip"

/* Prepares c over zRowSql, whose one parameter the caller binds. */
static VkStatus cursor_prepare(VkStore *p, const char *zRowSql, VkCursor *c){
    int rc;
    memset(c, 0, sizeof(*c));
    c->p = p;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p, zRowSql, &c->pRow);
    if( rc==SQLITE_OK ) rc = vk_prepare(p,
        "SELECT rel,parent FROM edge WHERE child=?1 ORDER BY rel,parent", &c->pEdge);
    if( rc!=SQLITE_OK ){
        VkStatus st = vk_sql_status(p, rc);
        vk_cursor_close(c);
        return st;
    }
    return VK_OK;
}

VkStatus vk_cursor_open(VkStore *p, int64_t afterSeq, VkCursor *c){
    VkStatus st = cursor_prepare(p,
        "SELECT " VK_ROW_COLUMNS " FROM assertion WHERE seq>?1 ORDER BY seq", c);
    if( st==VK_OK ) sqlite3_bind_int64(c->pRow, 1, afterSeq);
    return st;
}

VkStatus vk_get(VkStore *p, const VkId id, VkCursor *c){
    /* The unlogged arm uses the id index; the logged arm is LOG-4's scan, and
    ** LIMIT 1 stops before it when the first arm found the row. */
    /* A row from an unentitled author reads as absent (R-4a). */
    VkStatus st = cursor_prepare(p,
        "SELECT " VK_ROW_COLUMNS " FROM assertion a WHERE a.id=?1 AND a.log=x'' AND " VK_VALID("a")
        " UNION ALL"
        " SELECT " VK_ROW_COLUMNS " FROM assertion a INDEXED BY assertion_log"
        " WHERE a.log<>x'' AND a.id=?1 AND " VK_VALID("a")
        " LIMIT 1", c);
    if( st!=VK_OK ) return st;
    sqlite3_bind_blob(c->pRow, 1, id, VK_ID_BYTES, SQLITE_TRANSIENT);
    st = vk_cursor_next(c);
    return st==VK_DONE ? VK_NOTFOUND : st;
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
    c->a.log    = column_bytes(c->pRow, 10);
    c->a.pos    = (uint64_t)sqlite3_column_int64(c->pRow, 11);
    b = column_bytes(c->pRow, 12);
    c->a.nSkip  = b.n/VK_ID_BYTES;
    if( c->a.nSkip>VK_MAX_SKIP ) c->a.nSkip = 0;   /* then the id will not recompute */
    if( c->a.nSkip ) memcpy(c->skip, b.p, c->a.nSkip*VK_ID_BYTES);
    c->a.skip   = (const VkId*)c->skip;

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

VkStatus vk_standing(VkStore *p, VkBytes akey, const VkId id, int *pbHead, int *pnHeads){
    /* arm holds exactly the rows no valid assertion supersedes (LOG-3). */
    sqlite3_stmt *pSt = 0;
    int rc;
    *pbHead = 0; *pnHeads = 0;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p, "SELECT count(*), coalesce(sum(id=?2),0) FROM arm WHERE akey=?1", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, akey);
    sqlite3_bind_blob(pSt, 2, id, VK_ID_BYTES, SQLITE_STATIC);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        *pnHeads = sqlite3_column_int(pSt, 0);
        *pbHead = sqlite3_column_int(pSt, 1)>0;
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_ROW ) return vk_sql_status(p, rc);
    return *pnHeads ? VK_OK : VK_NOTFOUND;
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
    if( !zBad ){
        rc = outline_error(dst, a, 0, &zBad);
        if( rc!=SQLITE_OK ) return vk_sql_status(dst, rc);
    }
    if( zBad ){
        rc = quarantine(dst, id, zBad);
        if( rc!=SQLITE_OK ) return vk_sql_status(dst, rc);
        rep->quarantined++;
        return VK_OK;
    }
    rc = store_row(dst, a, id, &bAdded);
    if( rc!=SQLITE_OK ) return vk_sql_status(dst, rc);
    if( bAdded==1 ) rep->added++;
    else if( bAdded==-1 ) rep->covered++;
    else rep->present++;
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

/* Sums a hash per row, so no sort, and memory does not follow the store
** (ENV-2). A comparison between honest peers, not a proof. */
VkStatus vk_digest(VkStore *p, VkId out){
    sqlite3_stmt *pSt = 0;
    int rc, i;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: every read path requires r");
    rc = vk_prepare(p, "SELECT id, body IS NULL FROM assertion", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    memset(out, 0, VK_ID_BYTES);
    while( (rc = vk_step(pSt))==SQLITE_ROW ){
        sha256_ctx c;
        uint8_t h[VK_ID_BYTES];
        unsigned char bErased = (unsigned char)sqlite3_column_int(pSt, 1);
        unsigned carry = 0;
        sha256_init(&c);
        sha256_update(&c, (const unsigned char*)sqlite3_column_blob(pSt, 0), VK_ID_BYTES);
        sha256_update(&c, &bErased, 1);
        sha256_final(&c, h);
        for(i=VK_ID_BYTES-1; i>=0; i--){
            carry += (unsigned)out[i] + h[i];
            out[i] = (uint8_t)carry;
            carry >>= 8;
        }
    }
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_DONE ) return vk_sql_status(p, rc);
    return VK_OK;
}

