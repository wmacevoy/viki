/*
** store_test.c -- viki_store.c against REQUIREMENTS_v3. Each test names the
** requirement it holds to; the output is what test/gate.py reads.
**
** Built with VK_TEST_FAULTS: the fault tests fail every SQLite call and
** every allocation in turn, and require a retry to reach the same store.
*/
#include "viki_store.h"
#include "sha256.h"
#include "viki_store_int.h"   /* the delta codec, tested directly */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- faults ---- */
static long gFaultAt = 0, gFaultCount = 0;
static int gFaultFired = 0;

static int tick(void){
    if( gFaultAt==0 ) return 0;
    if( ++gFaultCount==gFaultAt ){ gFaultFired = 1; return 1; }
    return 0;
}
int vk_test_fault(void){ return tick(); }

static int gMallocArmed = 0;
static sqlite3_mem_methods gRealMem;
static void *fail_malloc(int n){
    if( gMallocArmed && tick() ) return 0;
    return gRealMem.xMalloc(n);
}
static void *fail_realloc(void *p, int n){
    if( gMallocArmed && tick() ) return 0;
    return gRealMem.xRealloc(p, n);
}
static void disarm(void){ gFaultAt = 0; }

/* ---- fixtures ---- */
static int gFailed;
#define CHECK(c) do{ if(!(c)){ printf("  at line %d: %s\n", __LINE__, #c); gFailed = 1; } }while(0)
#define B(z) bytes_of(z)
static VkBytes bytes_of(const char *z){ VkBytes b; b.p=(const uint8_t*)z; b.n=(uint32_t)strlen(z); return b; }

enum { ALL = VK_ERASE|VK_READ|VK_SUPERSEDE|VK_ORIGINATE };
static VkGrant gGrants[7];
static void init_grants(void){
    gGrants[0].principal = B("alice");  gGrants[0].rights = ALL;
    gGrants[1].principal = B("bob");    gGrants[1].rights = ALL;
    gGrants[2].principal = B("logger"); gGrants[2].rights = VK_SUPERSEDE;
    gGrants[3].principal = B("starter");gGrants[3].rights = VK_ORIGINATE|VK_READ;
    gGrants[4].principal = B("nobody"); gGrants[4].rights = VK_READ;
    gGrants[5].principal = B("mallory");gGrants[5].rights = 0;
    gGrants[6].principal = B("full");   gGrants[6].rights = ALL;
}
/* A source store that believes mallory is entitled, for rows that arrive by merge. */
static VkGrant gLoose[1];

typedef struct { sqlite3 *db; VkStore *st; } Peer;

static Peer peer(const char *zWho){
    Peer p;
    CHECK( sqlite3_open(":memory:", &p.db)==SQLITE_OK );
    CHECK( vk_open(p.db, B(zWho), gGrants, 7, &p.st)==VK_OK );
    return p;
}
static Peer loose_peer(const char *zWho){
    Peer p;
    gLoose[0].principal = B(zWho); gLoose[0].rights = ALL;
    CHECK( sqlite3_open(":memory:", &p.db)==SQLITE_OK );
    CHECK( vk_open(p.db, B(zWho), gLoose, 1, &p.st)==VK_OK );
    return p;
}
static void drop(Peer p){
    vk_close(p.st);
    CHECK( sqlite3_close(p.db)==SQLITE_OK );   /* fails on any leaked statement */
}

static VkAssertion mk(const char *zWho, const char *zKey, int rank, const char *zBody){
    VkAssertion a;
    memset(&a, 0, sizeof(a));
    a.author = B(zWho); a.kind = B("note"); a.akey = B(zKey);
    a.ts = B("2026-11-09T00:00:00Z");
    a.rank[15] = (uint8_t)rank;
    a.body = B(zBody);
    return a;
}
static uint8_t gRank0[VK_RANK_BYTES];

static int id_eq(const VkId a, const VkId b){ return memcmp(a, b, VK_ID_BYTES)==0; }

static int64_t rows(Peer p, const char *zSql){
    sqlite3_stmt *st; int64_t n = -1;
    if( sqlite3_prepare_v2(p.db, zSql, -1, &st, 0)==SQLITE_OK ){
        if( sqlite3_step(st)==SQLITE_ROW ) n = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}

static void digest(Peer p, VkId out){ CHECK( vk_digest(p.st, out)==VK_OK ); }

/* A database in a real file, for tests that measure pages or memory: SQLCipher
** keeps a temporary database ("") in RAM (TEMP_STORE 2), which would measure
** nothing. Each test gets its own directory, removed by drop_file. */
static char gDir[64];
static void open_file(Peer *p, const char *zName, char *zPath, size_t n){
    if( !gDir[0] ){
        snprintf(gDir, sizeof gDir, "%s/vk-test-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        if( !mkdtemp(gDir) ){ gDir[0] = 0; CHECK( !"mkdtemp" ); return; }
    }
    snprintf(zPath, n, "%s/%s", gDir, zName);
    unlink(zPath);
    CHECK( sqlite3_open(zPath, &p->db)==SQLITE_OK );
}
static void drop_file(Peer p, const char *zPath){
    char z[96];
    drop(p);
    unlink(zPath);
    snprintf(z, sizeof z, "%s-journal", zPath); unlink(z);
}

/* ---- A: identity ---- */
static void t_sha(void){
    static const uint8_t want[4] = {0xba,0x78,0x16,0xbf};
    sha256_ctx c; uint8_t h[32];
    sha256_init(&c); sha256_update(&c, (const unsigned char*)"abc", 3); sha256_final(&c, h);
    CHECK( memcmp(h, want, 4)==0 );
}
static void t_a1(void){
    Peer x = peer("alice"), y = peer("alice");
    VkAssertion a = mk("alice","k",1,"same");
    VkId i, j;
    CHECK( vk_put(x.st,&a,i)==VK_OK ); CHECK( vk_put(y.st,&a,j)==VK_OK );
    CHECK( id_eq(i,j) );
    drop(x); drop(y);
}
static void t_a2(void){
    VkId base, id, p1, p2;
    VkAssertion a = mk("alice","k",1,"body"), b;
    memset(p1,1,32); memset(p2,2,32);
    vk_assertion_id(&a, base);
#define DIFFERS(stmt) do{ b = a; stmt; vk_assertion_id(&b,id); CHECK( !id_eq(base,id) ); }while(0)
    DIFFERS(b.author = B("bob"));
    DIFFERS(b.kind = B("task"));
    DIFFERS(b.akey = B("k2"));
    DIFFERS(b.ts = B("2026-11-10T00:00:00Z"));
    DIFFERS(b.rank[0] = 9);
    DIFFERS(b.reference = B("ref"));
    DIFFERS(b.supersedes = (const VkId*)&p1; b.nSupersedes = 1);
    DIFFERS(b.derivedFrom = (const VkId*)&p1; b.nDerivedFrom = 1);
    DIFFERS(b.body = B("other"));
    DIFFERS(b.log = B("door"));
    DIFFERS(b.pos = 7);
    DIFFERS(b.skip = (const VkId*)&p1; b.nSkip = 1);
    a.skip = (const VkId*)&p1; a.nSkip = 1;
    vk_assertion_id(&a, base);
    DIFFERS(b.skip = (const VkId*)&p2);         /* the links themselves, not only how many */
}
static void t_a2_sorted(void){
    VkId ab[2], ba[2], x, y;
    VkAssertion a = mk("alice","k",1,"b"), b = a;
    memset(ab[0],1,32); memset(ab[1],2,32); memcpy(ba[0],ab[1],32); memcpy(ba[1],ab[0],32);
    a.supersedes = (const VkId*)ab; a.nSupersedes = 2;
    b.supersedes = (const VkId*)ba; b.nSupersedes = 2;
    vk_assertion_id(&a,x); vk_assertion_id(&b,y);
    CHECK( id_eq(x,y) );
}
static void t_a3(void){
    VkId x, y;
    VkAssertion a = mk("ab","k",1,"b"), b = mk("a","k",1,"b");
    /* adjacent fields, or the bytes between them would hide a missing prefix */
    a.kind = B("c"); b.kind = B("bc");
    vk_assertion_id(&a,x); vk_assertion_id(&b,y);
    CHECK( !id_eq(x,y) );
}
static void t_a4(void){
    VkId x, y;
    VkAssertion a = mk("alice","k",1,"caf\xc3\xa9"), b = mk("alice","k",1,"cafe\xcc\x81");
    vk_assertion_id(&a,x); vk_assertion_id(&b,y);
    CHECK( !id_eq(x,y) );    /* narrowed: viki hashes the bytes it is given */
}
static void t_a4a(void){
    static const uint8_t raw[5] = {0,0xff,0,'x',0xfe};
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"");
    VkId id; VkRow r;
    a.body.p = raw; a.body.n = 5;
    CHECK( vk_put(p.st,&a,id)==VK_OK );
    CHECK( vk_current(p.st,B("k"),&r)==VK_OK );
    CHECK( r.body.n==5 && memcmp(r.body.p,raw,5)==0 );
    vk_row_close(&r);
    drop(p);
}
static void t_a5(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"x");
    VkId i, j;
    CHECK( vk_put(p.st,&a,i)==VK_OK ); CHECK( vk_put(p.st,&a,j)==VK_EXISTS );
    CHECK( id_eq(i,j) );
    CHECK( rows(p,"SELECT count(*) FROM assertion")==1 );
    drop(p);
}
static void t_a7(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"real"), lie = a;
    VkId id; VkMergeReport rep; VkRow r;
    memset(&rep,0,sizeof(rep));
    vk_assertion_id(&a,id);
    lie.body = B("forged");
    CHECK( vk_ingest(p.st,&lie,id,&rep)==VK_OK );
    CHECK( rep.quarantined==1 && rep.added==0 );
    CHECK( vk_current(p.st,B("k"),&r)==VK_NOTFOUND );
    drop(p);
}
static void t_a7_recompute(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"text"), b = mk("alice","k",2,"more");
    VkId i, j, t, par[1]; int64_t bad = -1;
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    memcpy(par[0],i,32);
    b.supersedes = (const VkId*)par; b.nSupersedes = 1;
    b.derivedFrom = (const VkId*)par; b.nDerivedFrom = 1;
    CHECK( vk_put(p.st,&b,j)==VK_OK );
    CHECK( vk_erase(p.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_verify(p.st,&bad)==VK_OK );
    CHECK( bad==0 );
    /* the control: a changed stored byte must be seen */
    CHECK( sqlite3_exec(p.db,"UPDATE assertion SET ts=x'00' WHERE kind='note' OR kind=CAST('note' AS BLOB)",0,0,0)==SQLITE_OK );
    CHECK( vk_verify(p.st,&bad)==VK_OK );
    CHECK( bad==2 );
    drop(p);
}

/* ---- D: rights ---- */
static void t_d3(void){
    Peer s = peer("starter"), n = peer("nobody"), a = peer("alice");
    VkAssertion orig = mk("starter","k",1,"x"), sup = mk("starter","k",2,"y"), by = mk("nobody","k",1,"x");
    VkId id, par[1];
    memset(par[0],7,32);
    CHECK( vk_put(s.st,&orig,id)==VK_OK );          /* o may start a chain */
    sup.supersedes = (const VkId*)par; sup.nSupersedes = 1;
    CHECK( vk_put(s.st,&sup,id)==VK_EDENIED );      /* o does not grant s */
    CHECK( strstr(vk_errmsg(s.st),"D-3")!=0 );      /* E-3: names the requirement */
    CHECK( vk_put(n.st,&by,id)==VK_EDENIED );       /* r grants neither */
    orig.author = B("starter");
    CHECK( vk_put(a.st,&orig,id)==VK_EDENIED );     /* cannot write as another (I-1) */
    drop(s); drop(n); drop(a);
}
static void t_d3a(void){
    Peer log = peer("logger"), al = peer("alice");
    VkAssertion orig = mk("alice","slot",1,"start"), up = mk("logger","slot",2,"tick"), fresh = mk("logger","new",1,"x");
    VkId id; VkRow r; VkMergeReport rep; VkCursor c; VkSearch q; VkId d;
    memset(&rep,0,sizeof(rep));
    CHECK( vk_put(al.st,&orig,id)==VK_OK );
    CHECK( vk_merge(log.st,al.st,&rep)==VK_OK );
    CHECK( vk_put_chain(log.st,&up,id)==VK_OK );        /* extends a given chain */
    CHECK( vk_put_chain(log.st,&fresh,id)==VK_EDENIED ); /* cannot start one */
    CHECK( vk_current(log.st,B("slot"),&r)==VK_EDENIED );
    CHECK( vk_resolve(log.st,B("slot"),&r)==VK_EDENIED );
    CHECK( vk_cursor_open(log.st,0,&c)==VK_EDENIED );  vk_cursor_close(&c);
    CHECK( vk_search_open(log.st,&vkGrep,B("start"),&q)==VK_EDENIED ); vk_search_close(&q);
    CHECK( vk_digest(log.st,d)==VK_EDENIED );
    CHECK( vk_erase(log.st,id,B("t"),gRank0,d)==VK_EDENIED );
    drop(log); drop(al);
}
static void t_d3f(void){
    Peer a = peer("alice"), b = peer("bob");
    VkAssertion one = mk("alice","k",1,"a"), two = mk("bob","k",2,"b"), next = mk("alice","k",3,"c");
    VkId id; VkRow r; VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    CHECK( vk_put(a.st,&one,id)==VK_OK );
    CHECK( vk_put(b.st,&two,id)==VK_OK );
    CHECK( vk_merge(a.st,b.st,&rep)==VK_OK );
    CHECK( vk_current(a.st,B("k"),&r)==VK_FORKED );
    CHECK( vk_put_chain(a.st,&next,id)==VK_OK );        /* supersedes both arms */
    CHECK( vk_current(a.st,B("k"),&r)==VK_OK );
    CHECK( id_eq(r.id,id) && r.nArms==1 );
    vk_row_close(&r);
    next = mk("alice","empty",1,"o-right");
    CHECK( vk_put_chain(a.st,&next,id)==VK_OK );        /* no chain: an origination */
    CHECK( rows(a,"SELECT count(*) FROM edge WHERE child IN (SELECT id FROM assertion WHERE akey=CAST('empty' AS BLOB))")==0 );
    drop(a); drop(b);
}

/* ---- R: resolution ---- */
static void t_r1(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",9,"old"), b = mk("alice","k",1,"new");
    VkId i, j, par[1]; VkRow r;
    CHECK( vk_current(p.st,B("k"),&r)==VK_NOTFOUND );
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    memcpy(par[0],i,32); b.supersedes = (const VkId*)par; b.nSupersedes = 1;
    CHECK( vk_put(p.st,&b,j)==VK_OK );
    CHECK( vk_current(p.st,B("k"),&r)==VK_OK );   /* rank not consulted: 1 beats 9 */
    CHECK( id_eq(r.id,j) );
    vk_row_close(&r);
    drop(p);
}
static void fork2(Peer p, const char *z1, int r1, const char *z2, int r2, VkId i, VkId j){
    VkAssertion a = mk("alice","k",r1,z1), b = mk("alice","k",r2,z2);
    CHECK( vk_put(p.st,&a,i)==VK_OK ); CHECK( vk_put(p.st,&b,j)==VK_OK );
}
static void t_r2(void){
    Peer p = peer("alice"), q = peer("alice");
    VkId i, j, hi; VkRow r;
    fork2(p,"lo",1,"hi",2,i,j);
    CHECK( vk_resolve(p.st,B("k"),&r)==VK_OK );
    CHECK( id_eq(r.id,j) );          /* rank descending */
    vk_row_close(&r);
    fork2(q,"tie-a",5,"tie-b",5,i,j);
    memcpy(hi, memcmp(i,j,32)>0 ? i : j, 32);
    CHECK( vk_resolve(q.st,B("k"),&r)==VK_OK );
    CHECK( id_eq(r.id,hi) );         /* then id descending */
    vk_row_close(&r);
    drop(p); drop(q);
}
static void t_r5(void){
    Peer p = peer("alice");
    VkId i, j; VkRow r;
    fork2(p,"a",1,"b",2,i,j);
    CHECK( vk_current(p.st,B("k"),&r)==VK_FORKED );
    CHECK( vk_resolve(p.st,B("k"),&r)==VK_OK );
    CHECK( r.nArms==2 );
    vk_row_close(&r);
    drop(p);
}
static void t_r7(void){
    Peer p = peer("alice");
    VkId i, j, h, par[2]; VkRow r;
    VkAssertion heal = mk("alice","k",3,"healed");
    fork2(p,"a",1,"b",2,i,j);
    memcpy(par[0],i,32); memcpy(par[1],j,32);
    heal.supersedes = (const VkId*)par; heal.nSupersedes = 2;
    CHECK( vk_put(p.st,&heal,h)==VK_OK );
    CHECK( vk_current(p.st,B("k"),&r)==VK_OK );
    CHECK( id_eq(r.id,h) );
    vk_row_close(&r);
    drop(p);
}

/* a small history used by the order tests: a chain, a fork, an erasure */
#define NHIST 6
static void history(VkAssertion *as, VkId *ids, VkId par[][1], VkId *tomb){
    int i;
    as[0] = mk("alice","a",1,"a0");
    as[1] = mk("alice","a",2,"a1");
    as[2] = mk("bob","a",3,"a-fork");
    as[3] = mk("bob","b",1,"b0");
    as[4] = mk("alice","b",2,"b1");
    vk_assertion_id(&as[0],ids[0]);
    memcpy(par[0][0],ids[0],32);
    as[1].supersedes = (const VkId*)par[0]; as[1].nSupersedes = 1;
    vk_assertion_id(&as[3],ids[3]);
    memcpy(par[1][0],ids[3],32);
    as[4].supersedes = (const VkId*)par[1]; as[4].nSupersedes = 1;
    for(i=0;i<5;i++) vk_assertion_id(&as[i],ids[i]);
    /* a tombstone for a1 */
    memset(&as[5],0,sizeof(as[5]));
    as[5].author = B("alice"); as[5].kind = B("tombstone"); as[5].ts = B("t");
    memcpy(tomb[0],ids[1],32);
    as[5].akey.p = tomb[0]; as[5].akey.n = 32; as[5].body = as[5].akey;
    vk_assertion_id(&as[5],ids[5]);
}
static void t_order(void){
    VkAssertion as[NHIST]; VkId ids[NHIST], par[2][1], tomb[1], want, got;
    unsigned seed = 12345; int run, i;
    history(as,ids,par,tomb);
    for(run=0; run<30; run++){
        int order[NHIST]; Peer p = peer("alice"); VkMergeReport rep; VkRow r;
        memset(&rep,0,sizeof(rep));
        for(i=0;i<NHIST;i++) order[i]=i;
        if( run ) for(i=NHIST-1;i>0;i--){ int k,t; seed=seed*1103515245u+12345u; k=(int)((seed>>16)%(unsigned)(i+1)); t=order[i];order[i]=order[k];order[k]=t; }
        for(i=0;i<NHIST;i++) CHECK( vk_ingest(p.st,&as[order[i]],ids[order[i]],&rep)==VK_OK );
        CHECK( rep.added==NHIST );
        digest(p,got);
        if( run==0 ) memcpy(want,got,32);
        CHECK( id_eq(want,got) );
        CHECK( vk_resolve(p.st,B("b"),&r)==VK_OK ); CHECK( id_eq(r.id,ids[4]) ); vk_row_close(&r);
        CHECK( vk_resolve(p.st,B("a"),&r)==VK_OK ); CHECK( id_eq(r.id,ids[2]) && r.nArms==2 ); vk_row_close(&r);
        CHECK( rows(p,"SELECT count(*) FROM assertion WHERE body IS NULL")==1 );
        drop(p);
    }
}
static void t_r4a(void){
    Peer m = loose_peer("mallory"), al = peer("alice");
    VkAssertion a = mk("alice","k",1,"true"), up = mk("mallory","k",9,"takeover");
    VkId i, j, par[1], t; VkRow r; VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    CHECK( vk_put(al.st,&a,i)==VK_OK );
    memcpy(par[0],i,32); up.supersedes = (const VkId*)par; up.nSupersedes = 1;
    CHECK( vk_put(m.st,&up,j)==VK_OK );
    CHECK( vk_erase(m.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_merge(al.st,m.st,&rep)==VK_OK );
    CHECK( rep.added==2 && rep.quarantined==0 );            /* stored ... */
    CHECK( vk_current(al.st,B("k"),&r)==VK_OK );            /* ... and ignored */
    CHECK( id_eq(r.id,i) && !r.erased );
    CHECK( r.body.n==4 );                                   /* its tombstone destroyed nothing */
    vk_row_close(&r);
    drop(m); drop(al);
}

/* ---- LOG ---- */
static VkAssertion reading(const char *zWho, const char *zLog, int n, char *zKey, size_t nKey){
    VkAssertion a;
    snprintf(zKey,nKey,"%s/%d",zLog,n);
    a = mk(zWho,zKey,n&0xff,zKey);
    a.log = B(zLog);
    return a;
}
static void append_n(Peer p, const char *zWho, const char *zLog, int from, int to, VkId *ids){
    int i; char key[40]; VkId id;
    for(i=from;i<=to;i++){
        VkAssertion a = reading(zWho,zLog,i,key,sizeof key);
        CHECK( vk_append(p.st,&a,id)==VK_OK );
        if( ids ) memcpy(ids[i],id,32);
    }
}
static void t_log1(void){
    Peer p = peer("alice"), log = peer("logger");
    char key[40]; VkId id; VkMergeReport rep; uint64_t lo = 0, hi = 0;
    VkAssertion a = reading("alice","door",1,key,sizeof key);
    CHECK( vk_log_span(p.st,B("door"),B("alice"),&lo,&hi)==VK_NOTFOUND );
    append_n(p,"alice","door",1,5,0);
    CHECK( vk_log_span(p.st,B("door"),B("alice"),&lo,&hi)==VK_OK && lo==1 && hi==5 );
    CHECK( vk_log_span(log.st,B("door"),B("alice"),&lo,&hi)==VK_EDENIED );
    CHECK( vk_forget(p.st,B("door"),B("alice"),1,2,B("t"),gRank0,id)==VK_OK );
    CHECK( vk_log_span(p.st,B("door"),B("alice"),&lo,&hi)==VK_OK && lo==3 && hi==5 );
    append_n(p,"alice","gps",1,2,0);
    CHECK( rows(p,"SELECT max(pos) FROM assertion WHERE log=CAST('door' AS BLOB) AND kind=CAST('note' AS BLOB)")==5 );
    CHECK( rows(p,"SELECT max(pos) FROM assertion WHERE log=CAST('gps' AS BLOB)")==2 );
    CHECK( vk_put(p.st,&a,id)==VK_EINVAL );          /* only vk_append chooses a position */
    a.log = B("");
    a.pos = 3;
    vk_assertion_id(&a,id);
    memset(&rep,0,sizeof(rep));
    CHECK( vk_ingest(p.st,&a,id,&rep)==VK_OK && rep.quarantined==1 );   /* a position with no log */
    drop(p); drop(log);
}
static void t_log2(void){
    Peer p = peer("alice");
    VkId ids[9], bad; VkCursor c; int nSeen = 0;
    VkAssertion a; VkMergeReport rep;
    append_n(p,"alice","door",1,8,ids);
    CHECK( vk_cursor_open(p.st,0,&c)==VK_OK );
    while( vk_cursor_next(&c)==VK_OK ){
        uint64_t q = c.a.pos;
        if( q==1 ) CHECK( c.a.nSkip==0 );
        if( q==3 ){ CHECK( c.a.nSkip==1 && id_eq(c.a.skip[0],ids[2]) ); nSeen++; }
        if( q==6 ){ CHECK( c.a.nSkip==2 && id_eq(c.a.skip[0],ids[5]) && id_eq(c.a.skip[1],ids[4]) ); nSeen++; }
        if( q==8 ){ CHECK( c.a.nSkip==3 && id_eq(c.a.skip[0],ids[7]) && id_eq(c.a.skip[1],ids[6]) && id_eq(c.a.skip[2],ids[4]) ); nSeen++; }
    }
    vk_cursor_close(&c);
    CHECK( nSeen==3 );
    a = mk("alice","bad",1,"x");       /* position 6 needs two links; this has one */
    a.log = B("door"); a.pos = 6;
    a.skip = (const VkId*)ids; a.nSkip = 1;
    vk_assertion_id(&a,bad);
    memset(&rep,0,sizeof(rep));
    CHECK( vk_ingest(p.st,&a,bad,&rep)==VK_OK && rep.quarantined==1 );
    drop(p);
}
static int count_op(void *pv){ (*(long*)pv)++; return 0; }
static long ops_of_append(Peer p, const char *zLog, int n){
    long c = 0; char key[40]; VkId id;
    VkAssertion a = reading("alice",zLog,n,key,sizeof key);
    sqlite3_progress_handler(p.db,1,count_op,&c);
    CHECK( vk_append(p.st,&a,id)==VK_OK );
    sqlite3_progress_handler(p.db,0,0,0);
    return c;
}
static long ops_of_chain(Peer p, int n){
    long c = 0; VkId id; char body[24];
    VkAssertion a;
    snprintf(body,sizeof body,"v%d",n);
    a = mk("alice","chain",1,body);
    sqlite3_progress_handler(p.db,1,count_op,&c);
    CHECK( vk_put_chain(p.st,&a,id)==VK_OK );
    sqlite3_progress_handler(p.db,0,0,0);
    return c;
}
/* VM operations rather than time, so the test repeats exactly (T5). */
static void t_log3(void){
    Peer p = peer("alice");
    Peer few = peer("alice");
    long small, big, cSmall = 0, cBig = 0, cFew = 0; int i;
    for(i=1;i<=300;i++) if( i==300 ) cFew = ops_of_chain(few,i); else (void)ops_of_chain(few,i);
    sqlite3_exec(p.db,"BEGIN",0,0,0);
    append_n(p,"alice","door",1,256,0);
    small = ops_of_append(p,"door",257);        /* both odd: one skip link each */
    append_n(p,"alice","door",258,8192,0);
    big = ops_of_append(p,"door",8193);
    for(i=1;i<=300;i++) if( i==300 ) cSmall = ops_of_chain(p,i); else (void)ops_of_chain(p,i);
    for(i=301;i<=6000;i++) if( i==6000 ) cBig = ops_of_chain(p,i); else (void)ops_of_chain(p,i);
    sqlite3_exec(p.db,"COMMIT",0,0,0);
    printf("  vm ops: append %ld at 257, %ld at 8193; chain %ld at 300 in a small store, %ld at 300, %ld at 6000\n",small,big,cFew,cSmall,cBig);
    CHECK( big < small + small/2 );
    CHECK( cBig < cSmall + cSmall/2 );
    CHECK( cSmall < cFew + cFew/2 );              /* nor with what else the store holds */
    drop(p); drop(few);
}

/* Pages written per batch must not follow the store: an index on the random
** id would dirty a scattered page for each row. CACHE_WRITE is a count, so
** this repeats exactly. */
static int pages_of_batch(Peer p, int from, int to){
    int cur = 0, hi = 0;
    sqlite3_db_status(p.db,SQLITE_DBSTATUS_CACHE_WRITE,&cur,&hi,1);
    sqlite3_exec(p.db,"BEGIN",0,0,0);
    append_n(p,"alice","door",from,to,0);
    sqlite3_exec(p.db,"COMMIT",0,0,0);
    sqlite3_db_status(p.db,SQLITE_DBSTATUS_CACHE_WRITE,&cur,&hi,1);
    return cur;
}
static void t_log4(void){
    Peer p; int small, big, i; char path[96];
    open_file(&p,"log4.db",path,sizeof path);
    sqlite3_exec(p.db,"PRAGMA cache_size=32;PRAGMA synchronous=OFF",0,0,0);
    CHECK( vk_open(p.db,B("alice"),gGrants,7,&p.st)==VK_OK );
    for(i=0;i<8;i++) (void)pages_of_batch(p,i*256+1,(i+1)*256);
    small = pages_of_batch(p,2049,2304);
    for(i=9;i<64;i++) (void)pages_of_batch(p,i*256+1,(i+1)*256);
    big = pages_of_batch(p,16385,16640);
    printf("  pages written per 256 rows: %d at 2k rows, %d at 16k rows\n",small,big);
    CHECK( small>0 );
    CHECK( big < small + small/2 );
    drop_file(p,path);
}
/* A logged row has no id index, so it must still be erasable by id. */
static void t_w3_logged(void){
    Peer p = peer("alice"), er = peer("alice"), d = peer("alice");
    VkId ids[4], t; VkMergeReport rep; char key[40]; VkRow r;
    memset(&rep,0,sizeof(rep));
    append_n(p,"alice","door",1,3,ids);
    CHECK( vk_erase(p.st,ids[2],B("t"),gRank0,t)==VK_OK );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE body IS NULL")==1 );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE kind=CAST('note' AS BLOB)")==3 );
    snprintf(key,sizeof key,"door/2");
    CHECK( vk_current(p.st,B(key),&r)==VK_OK ); CHECK( r.erased ); vk_row_close(&r);
    /* and when the tombstone arrives first */
    CHECK( vk_erase(er.st,ids[2],B("t"),gRank0,t)==VK_OK );
    CHECK( vk_merge(d.st,er.st,&rep)==VK_OK );
    CHECK( vk_merge(d.st,p.st,&rep)==VK_OK );
    CHECK( rows(d,"SELECT count(*) FROM assertion WHERE body IS NULL")==1 );
    drop(p); drop(er); drop(d);
}
static void t_log4_dedupe(void){
    Peer p = peer("alice"), q = peer("alice");
    VkId ids[4], x, y; VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    append_n(p,"alice","door",1,3,ids);
    CHECK( vk_merge(q.st,p.st,&rep)==VK_OK && rep.added==3 );
    memset(&rep,0,sizeof(rep));
    CHECK( vk_merge(q.st,p.st,&rep)==VK_OK && rep.present==3 && rep.added==0 );
    digest(p,x); digest(q,y);
    CHECK( id_eq(x,y) );
    drop(p); drop(q);
}
/* A write that fails for lack of space must say so, after its undo. */
static void t_e_full(void){
    Peer p; int i; VkStatus st = VK_OK; VkId id; char key[32], body[900];
    CHECK( sqlite3_open("",&p.db)==SQLITE_OK );
    CHECK( vk_open(p.db,B("alice"),gGrants,7,&p.st)==VK_OK );
    CHECK( sqlite3_exec(p.db,"PRAGMA max_page_count=32",0,0,0)==SQLITE_OK );
    memset(body,'x',899); body[899] = 0;
    for(i=0;i<500 && st==VK_OK;i++){
        VkAssertion a;
        snprintf(key,sizeof key,"k%d",i);
        a = mk("alice",key,1,body);
        st = vk_put(p.st,&a,id);
    }
    CHECK( st==VK_ESQL );
    CHECK( (vk_sqlite_code(p.st)&0xff)==SQLITE_FULL );
    drop(p);
}
static void t_r1a(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","one",1,"a"), b = mk("alice","two",1,"b");
    VkId i, j, par[1]; VkRow r;
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    memcpy(par[0],i,32); b.supersedes = (const VkId*)par; b.nSupersedes = 1;
    CHECK( vk_put(p.st,&b,j)==VK_OK );
    CHECK( vk_current(p.st,B("one"),&r)==VK_OK ); CHECK( id_eq(r.id,i) ); vk_row_close(&r);
    /* a refold must reach the same answer */
    { static VkGrant other[1]; VkStore *st2;
      other[0].principal = B("alice"); other[0].rights = ALL;
      vk_close(p.st);
      CHECK( vk_open(p.db,B("alice"),other,1,&st2)==VK_OK );
      p.st = st2;
      CHECK( vk_current(p.st,B("one"),&r)==VK_OK ); CHECK( id_eq(r.id,i) ); vk_row_close(&r); }
    drop(p);
}
static void t_digest_erased(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"x");
    VkId i, before, after;
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    digest(p,before);
    CHECK( sqlite3_exec(p.db,"UPDATE assertion SET body=NULL",0,0,0)==SQLITE_OK );
    digest(p,after);
    CHECK( !id_eq(before,after) );
    drop(p);
}

/* ---- W: forgetting ---- */
static void t_w15(void){
    Peer p = peer("alice"), n = peer("nobody");
    VkId ids[9], t, d; int64_t bad = -1;
    append_n(p,"alice","door",1,8,ids);
    CHECK( vk_forget(n.st,B("door"),B("alice"),1,5,B("t"),gRank0,t)==VK_EDENIED );
    CHECK( vk_forget(p.st,B("door"),B("alice"),6,5,B("t"),gRank0,t)==VK_EINVAL );
    CHECK( vk_forget(p.st,B("door"),B("alice"),1,5,B("t"),gRank0,t)==VK_OK );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE kind=CAST('note' AS BLOB)")==3 );   /* frames gone */
    CHECK( rows(p,"SELECT count(*) FROM assertion")==4 );                                   /* one tombstone */
    CHECK( rows(p,"SELECT count(*) FROM arm WHERE id NOT IN (SELECT id FROM assertion)")==0 );
    CHECK( rows(p,"SELECT length(body) FROM assertion WHERE kind=CAST('tombstone.range' AS BLOB)")==8+2*32 );  /* 5=101b: two ids */
    CHECK( vk_verify(p.st,&bad)==VK_OK && bad==0 );
    CHECK( vk_digest(p.st,d)==VK_OK );
    drop(p); drop(n);
}
/* The ids a tombstone carries are the ids later appends need. */
static void t_w15_carry(void){
    Peer p = peer("alice");
    VkId ids[17], t, ctl[17]; VkCursor c; int nSeen = 0;
    Peer q = peer("alice");
    append_n(p,"alice","door",1,8,ids);
    append_n(q,"alice","door",1,8,ctl);
    CHECK( vk_forget(p.st,B("door"),B("alice"),1,8,B("t"),gRank0,t)==VK_OK );
    CHECK( rows(p,"SELECT count(*) FROM assertion")==1 );
    append_n(p,"alice","door",9,16,ids);
    append_n(q,"alice","door",9,16,ctl);
    CHECK( rows(p,"SELECT min(pos) FROM assertion WHERE kind=CAST('note' AS BLOB)")==9 );
    CHECK( id_eq(ids[16],ctl[16]) );      /* same chain as a log that forgot nothing */
    CHECK( vk_cursor_open(p.st,0,&c)==VK_OK );
    while( vk_cursor_next(&c)==VK_OK ){
        if( c.a.pos==16 && c.a.nSkip==4 ){ CHECK( id_eq(c.a.skip[3],ctl[8]) ); nSeen++; }
    }
    vk_cursor_close(&c);
    CHECK( nSeen==1 );
    drop(p); drop(q);
}
static void t_w15_unentitled(void){
    Peer m = loose_peer("mallory"), al = peer("alice");
    VkId ids[6], t; VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    append_n(al,"alice","door",1,5,ids);
    CHECK( vk_merge(m.st,al.st,&rep)==VK_OK );
    CHECK( vk_forget(m.st,B("door"),B("alice"),1,5,B("t"),gRank0,t)==VK_OK );
    memset(&rep,0,sizeof(rep));
    CHECK( vk_merge(al.st,m.st,&rep)==VK_OK );
    CHECK( rows(al,"SELECT count(*) FROM assertion WHERE kind=CAST('note' AS BLOB)")==5 );   /* stored, ignored */
    drop(m); drop(al);
}
static void t_w16(void){
    Peer src = peer("alice"), tomb = peer("alice"), x = peer("alice"), y = peer("alice");
    VkId ids[9], t, dx, dy; VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    append_n(src,"alice","door",1,8,ids);
    CHECK( vk_merge(tomb.st,src.st,&rep)==VK_OK );
    CHECK( vk_forget(tomb.st,B("door"),B("alice"),1,5,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_merge(x.st,src.st,&rep)==VK_OK );            /* rows first */
    CHECK( vk_merge(x.st,tomb.st,&rep)==VK_OK );
    memset(&rep,0,sizeof(rep));
    CHECK( vk_merge(y.st,tomb.st,&rep)==VK_OK );           /* tombstone first */
    CHECK( vk_merge(y.st,src.st,&rep)==VK_OK );
    CHECK( rep.covered==5 );
    digest(x,dx); digest(y,dy);
    CHECK( id_eq(dx,dy) );
    CHECK( rows(y,"SELECT count(*) FROM assertion")==4 );
    drop(src); drop(tomb); drop(x); drop(y);
}
static void t_w17(void){
    Peer p = peer("alice");
    VkAssertion base = mk("alice","k",1,"outside"), up = mk("alice","k",2,"inside");
    VkId i, j, t, par[1];
    CHECK( vk_put(p.st,&base,i)==VK_OK );
    memcpy(par[0],i,32);
    up.supersedes = (const VkId*)par; up.nSupersedes = 1;
    up.log = B("door");
    CHECK( vk_append(p.st,&up,j)==VK_OK );
    CHECK( vk_forget(p.st,B("door"),B("alice"),1,1,B("t"),gRank0,t)==VK_EINVAL );
    CHECK( strstr(vk_errmsg(p.st),"W-17")!=0 );
    CHECK( rows(p,"SELECT count(*) FROM assertion")==2 );
    drop(p);
}
/* Which rows are heads follows the grants: reopen under others, and re-fold. */
static void t_arm_rebuild(void){
    Peer m = loose_peer("mallory"), al = peer("alice");
    VkAssertion a = mk("alice","k",1,"true"), up = mk("mallory","k",9,"takeover");
    VkId i, j, par[1]; VkRow r; VkMergeReport rep; VkStore *st2;
    static VkGrant both[2];
    memset(&rep,0,sizeof(rep));
    CHECK( vk_put(al.st,&a,i)==VK_OK );
    memcpy(par[0],i,32); up.supersedes = (const VkId*)par; up.nSupersedes = 1;
    CHECK( vk_put(m.st,&up,j)==VK_OK );
    CHECK( vk_merge(al.st,m.st,&rep)==VK_OK );
    CHECK( vk_current(al.st,B("k"),&r)==VK_OK ); CHECK( id_eq(r.id,i) ); vk_row_close(&r);
    vk_close(al.st);
    both[0].principal = B("alice");   both[0].rights = ALL;
    both[1].principal = B("mallory"); both[1].rights = ALL;
    CHECK( vk_open(al.db,B("alice"),both,2,&st2)==VK_OK );
    CHECK( vk_current(st2,B("k"),&r)==VK_OK ); CHECK( id_eq(r.id,j) ); vk_row_close(&r);
    al.st = st2;
    drop(m); drop(al);
}


static void t_w3_no_resurrection(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"old"), b = mk("alice","k",2,"secret");
    VkId i, j, t, par[1]; VkRow r;
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    memcpy(par[0],i,32); b.supersedes = (const VkId*)par; b.nSupersedes = 1;
    CHECK( vk_put(p.st,&b,j)==VK_OK );
    CHECK( vk_erase(p.st,j,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_current(p.st,B("k"),&r)==VK_OK );
    CHECK( id_eq(r.id,j) && r.erased && r.body.n==0 );     /* not "old" come back */
    vk_row_close(&r);
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE instr(body,CAST('secret' AS BLOB))>0")==0 );
    CHECK( rows(p,"SELECT count(*) FROM assertion")==3 );  /* the frame stays */
    drop(p);
}
static void t_w6(void){
    Peer holder = peer("alice"), eraser = peer("alice"), d = peer("alice");
    VkAssertion a = mk("alice","k",1,"secret");
    VkId i, t; VkMergeReport rep; VkRow r;
    memset(&rep,0,sizeof(rep));
    CHECK( vk_put(holder.st,&a,i)==VK_OK );
    CHECK( vk_erase(eraser.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_merge(d.st,eraser.st,&rep)==VK_OK );          /* tombstone first */
    CHECK( vk_merge(d.st,holder.st,&rep)==VK_OK );
    CHECK( vk_current(d.st,B("k"),&r)==VK_OK );
    CHECK( r.erased );
    vk_row_close(&r);
    drop(holder); drop(eraser); drop(d);
}
static void t_w2(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"secret");
    VkId i, t, t2;
    uint8_t r1[VK_RANK_BYTES] = {1};
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    CHECK( vk_erase(p.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( vk_erase(p.st,t,B("t2"),r1,t2)==VK_OK );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE kind=CAST('tombstone' AS BLOB) AND body IS NULL")==0 );
    drop(p);
}

/* ---- M: merge ---- */
static void put_n(Peer p, const char *zWho, const char *zKey, int n){
    int i; VkId id; char body[32];
    for(i=0;i<n;i++){
        VkAssertion a;
        snprintf(body,sizeof body,"%s-%d",zKey,i);
        a = mk(zWho,zKey,i,body);
        CHECK( vk_put(p.st,&a,id)==VK_OK );
    }
}
static void t_m1(void){
    Peer a = peer("alice"), b = peer("bob"), c = peer("alice");
    Peer ab_c = peer("alice"), c_ba = peer("alice");
    VkMergeReport rep; VkId x, y, z;
    memset(&rep,0,sizeof(rep));
    put_n(a,"alice","a",4); put_n(b,"bob","b",4); put_n(c,"alice","c",4);
    vk_merge(ab_c.st,a.st,&rep); vk_merge(ab_c.st,b.st,&rep); vk_merge(ab_c.st,c.st,&rep);
    vk_merge(c_ba.st,c.st,&rep); vk_merge(c_ba.st,b.st,&rep); vk_merge(c_ba.st,a.st,&rep);
    digest(ab_c,x); digest(c_ba,y);
    CHECK( id_eq(x,y) );                         /* commutative, associative */
    CHECK( vk_merge(ab_c.st,c.st,&rep)==VK_OK ); /* idempotent */
    digest(ab_c,z);
    CHECK( id_eq(x,z) );
    drop(a); drop(b); drop(c); drop(ab_c); drop(c_ba);
}
static void t_m2(void){
    Peer a = peer("alice");
    VkMergeReport rep; VkId x, y;
    memset(&rep,0,sizeof(rep));
    put_n(a,"alice","a",3);
    digest(a,x);
    CHECK( vk_merge(a.st,a.st,&rep)==VK_OK );
    digest(a,y);
    CHECK( id_eq(x,y) && rep.added==0 && rep.present==3 );
    drop(a);
}
static void t_m3(void){
    Peer a = peer("alice"), b = peer("alice");
    VkMergeReport rep;
    memset(&rep,0,sizeof(rep));
    put_n(a,"alice","a",5);
    gFaultCount = 0; gFaultFired = 0; gFaultAt = 12;
    CHECK( vk_merge(b.st,a.st,&rep)!=VK_OK );
    disarm();
    CHECK( gFaultFired && rep.incomplete==1 );
    drop(a); drop(b);
}
static void t_m7(void){
    Peer p = peer("alice");
    VkAssertion good = mk("alice","k",1,"fine"), bad = mk("alice","x",1,"y");
    VkId gid, junk; VkMergeReport rep; VkRow r;
    memset(&rep,0,sizeof(rep));
    memset(junk,9,32);
    vk_assertion_id(&good,gid);
    CHECK( vk_ingest(p.st,&bad,junk,&rep)==VK_OK );   /* a bad row does not stop the merge */
    CHECK( vk_ingest(p.st,&good,gid,&rep)==VK_OK );
    CHECK( rep.quarantined==1 && rep.added==1 );
    CHECK( rows(p,"SELECT count(*) FROM quarantine")==1 );
    CHECK( vk_current(p.st,B("k"),&r)==VK_OK ); vk_row_close(&r);
    drop(p);
}

/* ---- Q: search ---- */
static int count_hits(Peer p, const char *zQ){
    VkSearch s; VkId id; int n = 0;
    CHECK( vk_search_open(p.st,&vkGrep,B(zQ),&s)==VK_OK );
    while( vk_search_next(&s,id)==VK_OK ) n++;
    vk_search_close(&s);
    return n;
}
static void t_q6_grep(void){
    Peer p = peer("alice");
    VkId id;
    VkAssertion a = mk("alice","a",1,"the Needle here"), b = mk("alice","b",1,"needle again"), c = mk("alice","c",1,"nothing");
    vk_put(p.st,&a,id); vk_put(p.st,&b,id); vk_put(p.st,&c,id);
    CHECK( count_hits(p,"needle")==1 );     /* exact bytes, so case-sensitive */
    CHECK( count_hits(p,"e")==2 );     /* every hit, not just the first */
    CHECK( count_hits(p,"absent")==0 );
    drop(p);
}
static void t_q8(void){
    Peer p = peer("alice"), ro = peer("mallory");
    VkId first, second, logged, missing, again; VkCursor c; char key[40];
    VkAssertion a = mk("alice","k",1,"one"), b = mk("alice","k",2,"two"), r = reading("alice","door",1,key,sizeof key);
    CHECK( vk_put(p.st,&a,first)==VK_OK );
    b.supersedes = (const VkId*)&first; b.nSupersedes = 1;
    b.derivedFrom = (const VkId*)&first; b.nDerivedFrom = 1;
    CHECK( vk_put(p.st,&b,second)==VK_OK );
    CHECK( vk_append(p.st,&r,logged)==VK_OK );
    CHECK( vk_get(p.st,second,&c)==VK_OK );
    CHECK( id_eq(c.id,second) && c.a.body.n==3 && memcmp(c.a.body.p,"two",3)==0 );
    CHECK( c.a.nSupersedes==1 && id_eq(c.a.supersedes[0],first) && c.a.nDerivedFrom==1 );
    vk_assertion_id(&c.a,again);
    CHECK( id_eq(again,second) );                 /* the whole frame came back */
    vk_cursor_close(&c);
    CHECK( vk_get(p.st,logged,&c)==VK_OK && c.a.pos==1 );   /* a logged row: a scan, but found */
    vk_cursor_close(&c);
    memset(missing,7,sizeof missing);
    CHECK( vk_get(p.st,missing,&c)==VK_NOTFOUND );
    vk_cursor_close(&c);
    CHECK( vk_get(ro.st,second,&c)==VK_EDENIED );
    vk_cursor_close(&c);
    drop(p); drop(ro);
    {   /* a row from an unentitled author reads as absent (R-4a) */
        Peer m = loose_peer("mallory"), al = peer("alice");
        VkAssertion x = mk("mallory","k",1,"claim"); VkId xi; VkMergeReport rep;
        memset(&rep,0,sizeof(rep));
        CHECK( vk_put(m.st,&x,xi)==VK_OK );
        CHECK( vk_merge(al.st,m.st,&rep)==VK_OK && rep.added==1 );
        CHECK( vk_get(al.st,xi,&c)==VK_NOTFOUND );
        vk_cursor_close(&c);
        drop(m); drop(al);
    }
}
static void t_q4(void){
    Peer p = peer("alice"), m = loose_peer("mallory"), ro = peer("mallory");
    VkAssertion a = mk("alice","k",1,"one"), b = mk("alice","k",2,"two");
    VkAssertion c = mk("alice","f",1,"left"), d = mk("alice","f",2,"right"), e = mk("alice","f",3,"healed");
    VkAssertion up = mk("mallory","k",9,"takeover");
    VkId ia, ib, ic, id, ie, iu, two[2]; VkMergeReport rep; int bHead = -1, n = -1;
    CHECK( vk_put(p.st,&a,ia)==VK_OK );
    b.supersedes = (const VkId*)&ia; b.nSupersedes = 1;
    CHECK( vk_put(p.st,&b,ib)==VK_OK );
    CHECK( vk_standing(p.st,B("k"),ia,&bHead,&n)==VK_OK && bHead==0 && n==1 );   /* superseded */
    CHECK( vk_standing(p.st,B("k"),ib,&bHead,&n)==VK_OK && bHead==1 && n==1 );   /* current */
    CHECK( vk_put(p.st,&c,ic)==VK_OK && vk_put(p.st,&d,id)==VK_OK );
    CHECK( vk_standing(p.st,B("f"),ic,&bHead,&n)==VK_OK && bHead==1 && n==2 );   /* one head of a fork */
    memcpy(two[0],ic,32); memcpy(two[1],id,32);
    e.supersedes = (const VkId*)two; e.nSupersedes = 2;
    CHECK( vk_put(p.st,&e,ie)==VK_OK );
    CHECK( vk_standing(p.st,B("f"),id,&bHead,&n)==VK_OK && bHead==0 && n==1 );
    CHECK( vk_standing(p.st,B("f"),ie,&bHead,&n)==VK_OK && bHead==1 && n==1 );
    /* an unentitled supersession leaves the head standing */
    up.supersedes = (const VkId*)&ib; up.nSupersedes = 1;
    CHECK( vk_put(m.st,&up,iu)==VK_OK );
    memset(&rep,0,sizeof(rep));
    CHECK( vk_merge(p.st,m.st,&rep)==VK_OK );
    CHECK( vk_standing(p.st,B("k"),ib,&bHead,&n)==VK_OK && bHead==1 && n==1 );
    CHECK( vk_standing(p.st,B("none"),ib,&bHead,&n)==VK_NOTFOUND );
    CHECK( vk_standing(ro.st,B("k"),ib,&bHead,&n)==VK_EDENIED );
    drop(p); drop(m); drop(ro);
}
static int ranked(Peer p, const char *zQ, VkId *out, int nMax){
    VkSearch s; VkId id; int n = 0; VkStatus st;
    st = vk_search_open(p.st,&vkBm25,B(zQ),&s);
    if( st!=VK_OK ){ vk_search_close(&s); return -(int)st; }
    while( vk_search_next(&s,id)==VK_OK ){ if( n<nMax ) memcpy(out[n],id,32); n++; }
    vk_search_close(&s);
    return n;
}
static void t_q6_bm25(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","a",1,"the parking lot sensor went silent"),
                b = mk("alice","b",1,"sensor sensor sensor: the sensor log"),
                c = mk("alice","c",1,"nothing to see"),
                d = mk("alice","d",1,"a later note about the sensor");
    VkId ia, ib, ic, id, hit[8]; VkSearch s; VkCoverage cov;
    CHECK( vk_put(p.st,&a,ia)==VK_OK && vk_put(p.st,&b,ib)==VK_OK && vk_put(p.st,&c,ic)==VK_OK );
    CHECK( ranked(p,"sensor",hit,8)==-(int)VK_BLIND );     /* not yet indexed: blind, never not-found */
    CHECK( vk_bm25_enable(p.st)==VK_OK );
    CHECK( vk_bm25_enable(p.st)==VK_OK );                   /* a second time changes nothing */
    CHECK( vk_put(p.st,&d,id)==VK_OK );                     /* rows written after enabling go in too */
    CHECK( ranked(p,"sensor",hit,8)==3 );                   /* the rows written before were back-filled */
    CHECK( id_eq(hit[0],ib) );                              /* ranked: the dense mention first */
    CHECK( ranked(p,"parking nothing",hit,8)==2 );         /* terms are OR'd, not AND'd */
    CHECK( ranked(p,"\"sensor\" OR (",hit,8)==3 );          /* query syntax is quoted, not run */
    CHECK( ranked(p,"lot\"sensor",hit,8)==1 );              /* a quote inside a word is a phrase, not an error */
    CHECK( ranked(p,"absent",hit,8)==0 );
    CHECK( vk_search_open(p.st,&vkBm25,B("sensor"),&s)==VK_OK );
    CHECK( vk_search_coverage(&s,&cov)==VK_OK && cov.bExhaustive==0 && strcmp(cov.zMethod,"bm25")==0 && cov.nSearched==4 );
    vk_search_close(&s);
    CHECK( vkBm25.selfcheck(p.st)==VK_OK );
    drop(p);
}
static void t_q5_bm25(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"secret word");
    VkId i, t, ids[9], hit[4]; uint64_t lo, hi;
    CHECK( vk_bm25_enable(p.st)==VK_OK );
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    CHECK( ranked(p,"secret",hit,4)==1 );
    CHECK( rows(p,"SELECT count(*) FROM vk_bm25_data WHERE instr(block,'secret')>0")>0 );   /* the control */
    CHECK( vk_erase(p.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( ranked(p,"secret",hit,4)==0 );                   /* erased: gone from the answers */
    CHECK( rows(p,"SELECT count(*) FROM vk_bm25_data WHERE instr(block,'secret')>0")==0 );  /* and the pages */
    append_n(p,"alice","door",1,4,ids);                     /* bodies "door/1" .. "door/4" */
    CHECK( ranked(p,"door",hit,4)==4 );
    CHECK( vk_forget(p.st,B("door"),B("alice"),1,3,B("t"),gRank0,t)==VK_OK );
    CHECK( ranked(p,"door",hit,4)==1 );                     /* a forgotten range leaves the answers */
    /* and the index itself: FTS5 checks every entry against assertion's rows */
    CHECK( sqlite3_exec(p.db,"INSERT INTO vk_bm25(vk_bm25, rank) VALUES('integrity-check', 1)",0,0,0)==SQLITE_OK );
    CHECK( vk_log_span(p.st,B("door"),B("alice"),&lo,&hi)==VK_OK && lo==4 );
    drop(p);
}
static void t_env4(void){
    Peer p = peer("alice");
    printf("  SQLite %s\n", sqlite3_libversion());
    CHECK( sqlite3_libversion_number()>=3044000 );          /* secure-delete */
    CHECK( sqlite3_compileoption_used("ENABLE_FTS5") );
    {   /* SQLCipher, not stock SQLite: stock SQLite answers an unknown pragma with no row */
        sqlite3_stmt *st = 0;
        CHECK( sqlite3_prepare_v2(p.db,"PRAGMA cipher_version",-1,&st,0)==SQLITE_OK );
        CHECK( sqlite3_step(st)==SQLITE_ROW );
        if( sqlite3_column_text(st,0) ) printf("  SQLCipher %s\n", (const char*)sqlite3_column_text(st,0));
        sqlite3_finalize(st);
    }
    drop(p);
}
/* ---- DS: reverse deltas ---- */
/* Version v of a 60-line document: line (v*7)%60 says "uniqVz", a word in v alone. */
static void doc(int v, char *buf, size_t n){
    size_t at = 0; int i;
    for(i=0;i<60 && at<n;i++){
        if( v>=0 && i==(v*7)%60 ) at += (size_t)snprintf(buf+at, n-at, "line %02d: this version says uniq%dz here\n", i, v);
        else at += (size_t)snprintf(buf+at, n-at, "line %02d: the steady text of section %d, unchanged\n", i, i/10);
    }
}
static uint32_t gLcg = 1;
static uint32_t lcg(void){ gLcg = gLcg*1103515245u + 12345u; return gLcg >> 8; }
static void t_ds_codec(void){
    static uint8_t src[6000], tgt[7000];
    int k, bad = 0;
    gLcg = 7;
    for(k=0;k<400;k++){
        uint32_t nS = lcg()%5000, nT, i, e;
        uint8_t *d = 0, *back = 0; uint32_t nD = 0, nBack = 0;
        for(i=0;i<nS;i++) src[i] = k%3 ? (uint8_t)"abcde \n"[lcg()%7] : (uint8_t)lcg();
        memcpy(tgt, src, nS); nT = nS;
        for(e=lcg()%5; e>0; e--){               /* insert, delete or replace a run */
            uint32_t at = nT ? lcg()%nT : 0, len = lcg()%60;
            switch( lcg()%3 ){
            case 0: if( nT+len < sizeof tgt ){ memmove(tgt+at+len, tgt+at, nT-at); for(i=0;i<len;i++) tgt[at+i]=(uint8_t)lcg(); nT += len; } break;
            case 1: if( at+len > nT ) len = nT-at; memmove(tgt+at, tgt+at+len, nT-at-len); nT -= len; break;
            default: for(i=0;i<len && at+i<nT;i++) tgt[at+i]=(uint8_t)lcg(); break;
            }
        }
        if( k%10==9 ){ nT = lcg()%3000; for(i=0;i<nT;i++) tgt[i]=(uint8_t)lcg(); }   /* unrelated */
        CHECK( vk_delta_make(src,nS,tgt,nT,&d,&nD)==SQLITE_OK );
        if( vk_delta_apply(src,nS,d,nD,&back,&nBack)!=SQLITE_OK || nBack!=nT || memcmp(back,tgt,nT)!=0 ) bad++;
        sqlite3_free(back);
        for(i=0;i<nD;i++){                       /* every cut-short delta is refused or exact */
            int rc = vk_delta_apply(src,nS,d,i,&back,&nBack);
            if( rc==SQLITE_OK ){ if( nBack!=nT || memcmp(back,tgt,nT)!=0 ) bad++; sqlite3_free(back); }
            else if( rc!=SQLITE_CORRUPT ) bad++;
            if( i>64 ) i += i/8;                 /* sample long deltas */
        }
        sqlite3_free(d);
    }
    CHECK( bad==0 );
    {   /* a copy past the source's end, and a length that does not add up */
        static const uint8_t past[] = { 8, (8<<1)|1, 0 }, shortlen[] = { 9, (2<<1), 'a', 'b' };
        uint8_t *back = 0; uint32_t nBack = 0;
        CHECK( vk_delta_apply((const uint8_t*)"abc",3,past,sizeof past,&back,&nBack)==SQLITE_CORRUPT );
        CHECK( vk_delta_apply((const uint8_t*)"abc",3,shortlen,sizeof shortlen,&back,&nBack)==SQLITE_CORRUPT );
    }
    {   /* a near-copy is small */
        char a[4000], b[4000]; uint8_t *d = 0; uint32_t nD = 0;
        doc(1,a,sizeof a); doc(2,b,sizeof b);
        CHECK( vk_delta_make((const uint8_t*)b,(uint32_t)strlen(b),(const uint8_t*)a,(uint32_t)strlen(a),&d,&nD)==SQLITE_OK );
        CHECK( nD < strlen(a)/10 );
        sqlite3_free(d);
    }
}
static int body_is(Peer p, const VkId id, const char *zWant){
    VkCursor c; int ok;
    ok = vk_get(p.st,id,&c)==VK_OK && c.a.body.n==strlen(zWant) && memcmp(c.a.body.p,zWant,c.a.body.n)==0;
    vk_cursor_close(&c);
    return ok;
}
static VkAssertion version(char *buf, size_t n, int v, const VkId *prev){
    VkAssertion a;
    doc(v,buf,n);
    a = mk("alice","big",v,buf);
    if( prev ){ a.supersedes = prev; a.nSupersedes = 1; }
    return a;
}
static void t_ds1(void){
    Peer p = peer("alice");
    char b0[4000], b1[4000], small[200], other[4000];
    VkId i0, i1, s0, s1, o0, o1, hit[4]; VkRow r; VkAssertion a;
    a = version(b0,sizeof b0,0,0);  CHECK( vk_put(p.st,&a,i0)==VK_OK );
    a = version(b1,sizeof b1,1,(const VkId*)&i0); CHECK( vk_put(p.st,&a,i1)==VK_OK );
    CHECK( rows(p,"SELECT dbase FROM assertion WHERE seq=1")==2 );          /* the older is a delta ... */
    CHECK( rows(p,"SELECT length(body) FROM assertion WHERE seq=1") < 300 );   /* ... and small */
    CHECK( rows(p,"SELECT dbase IS NULL FROM assertion WHERE seq=2")==1 );  /* the latest is whole */
    CHECK( body_is(p,i0,b0) && body_is(p,i1,b1) );
    CHECK( vk_current(p.st,B("big"),&r)==VK_OK && r.body.n==strlen(b1) && memcmp(r.body.p,b1,r.body.n)==0 );
    vk_row_close(&r);
    CHECK( count_hits(p,"uniq0z")==1 && count_hits(p,"uniq1z")==1 );
    CHECK( count_hits(p,"line 30: the steady")==2 );    /* shared text: in the delta only as a copy */
    CHECK( vk_bm25_enable(p.st)==VK_OK );
    CHECK( ranked(p,"uniq0z",hit,4)==1 && id_eq(hit[0],i0) );
    CHECK( ranked(p,"steady",hit,4)==2 );                 /* the index holds the text, not the delta */
    CHECK( sqlite3_exec(p.db,"INSERT INTO vk_bm25(vk_bm25, rank) VALUES('integrity-check', 1)",0,0,0)==SQLITE_OK );
    /* not worth it: a small body, and a body unlike the one superseding it */
    memset(small,'s',150); small[150] = 0;
    a = mk("alice","small",1,small); CHECK( vk_put(p.st,&a,s0)==VK_OK );
    small[3] = 't'; a = mk("alice","small",2,small); a.supersedes = (const VkId*)&s0; a.nSupersedes = 1;
    CHECK( vk_put(p.st,&a,s1)==VK_OK );
    doc(5,other,sizeof other); a = mk("alice","other",1,other); CHECK( vk_put(p.st,&a,o0)==VK_OK );
    { int i; for(i=0;other[i];i++) other[i] = (char)('A' + (i*7)%26); }
    a = mk("alice","other",2,other); a.supersedes = (const VkId*)&o0; a.nSupersedes = 1;
    CHECK( vk_put(p.st,&a,o1)==VK_OK );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE akey IN (CAST('small' AS BLOB),CAST('other' AS BLOB)) AND dbase IS NOT NULL")==0 );
    drop(p);
}
static void t_ds2(void){
    Peer p = peer("alice");
    static VkId ids[40]; char buf[4000]; int v; int64_t bad = -1; VkAssertion a;
    for(v=0;v<40;v++){
        a = version(buf,sizeof buf,v,v ? (const VkId*)&ids[v-1] : 0);
        CHECK( vk_put(p.st,&a,ids[v])==VK_OK );
    }
    CHECK( rows(p,"WITH RECURSIVE c(seq,d) AS (SELECT seq,0 FROM assertion WHERE dbase IS NULL"
                  " UNION ALL SELECT a.seq,c.d+1 FROM assertion a JOIN c ON a.dbase=c.seq)"
                  " SELECT max(d) FROM c") <= 16 );                           /* the bound holds */
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE dbase IS NOT NULL") >= 35 );   /* and most are deltas */
    for(v=0;v<40;v++){ doc(v,buf,sizeof buf); CHECK( body_is(p,ids[v],buf) ); }
    CHECK( vk_verify(p.st,&bad)==VK_OK && bad==0 );
    drop(p);
}
static void t_ds3(void){
    Peer a = peer("alice"), b = peer("alice");
    static VkId ids[10]; char buf[4000]; int v; VkId da, db; VkAssertion x;
    for(v=0;v<10;v++){ x = version(buf,sizeof buf,v,v ? (const VkId*)&ids[v-1] : 0); CHECK( vk_put(a.st,&x,ids[v])==VK_OK ); }
    for(v=9;v>=0;v--){ VkId id; x = version(buf,sizeof buf,v,v ? (const VkId*)&ids[v-1] : 0); CHECK( vk_put(b.st,&x,id)==VK_OK ); }
    CHECK( rows(b,"SELECT count(*) FROM assertion WHERE dbase IS NOT NULL") >= 8 );   /* deltas on arrival */
    for(v=0;v<10;v++){ doc(v,buf,sizeof buf); CHECK( body_is(b,ids[v],buf) ); }
    digest(a,da); digest(b,db);
    CHECK( id_eq(da,db) );
    drop(a); drop(b);
    {   /* a logged row is never a base, so a range tombstone deletes no body another rests on */
        Peer p = peer("alice");
        char key[40], b0[4000]; VkId lid, xid; VkAssertion l = reading("alice","door",1,key,sizeof key);
        doc(0,b0,sizeof b0); x = mk("alice","big",0,b0);
        vk_assertion_id(&x,xid);
        doc(1,buf,sizeof buf); l.akey = B("big"); l.body = B(buf);
        l.supersedes = (const VkId*)&xid; l.nSupersedes = 1;  /* the logged row supersedes x, which arrives after */
        CHECK( vk_append(p.st,&l,lid)==VK_OK );
        CHECK( vk_put(p.st,&x,xid)==VK_OK );
        CHECK( rows(p,"SELECT count(*) FROM assertion WHERE dbase IS NOT NULL")==0 );
        drop(p);
    }
}
static void t_ds4(void){
    Peer p = peer("alice");
    static VkId ids[6]; char buf[4000]; int v; VkId t, hit[4]; VkAssertion a; VkCursor c;
    CHECK( vk_bm25_enable(p.st)==VK_OK );
    for(v=0;v<6;v++){ a = version(buf,sizeof buf,v,v ? (const VkId*)&ids[v-1] : 0); CHECK( vk_put(p.st,&a,ids[v])==VK_OK ); }
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE dbase IS NOT NULL")==5 );
    CHECK( vk_erase(p.st,ids[3],B("t"),gRank0,t)==VK_OK );                  /* the middle */
    for(v=0;v<6;v++){ if( v==3 ) continue; doc(v,buf,sizeof buf); CHECK( body_is(p,ids[v],buf) ); }
    CHECK( vk_get(p.st,ids[3],&c)==VK_OK && c.a.erased && c.a.body.n==0 );
    vk_cursor_close(&c);
    CHECK( count_hits(p,"uniq3z")==0 && ranked(p,"uniq3z",hit,4)==0 );
    CHECK( rows(p,"SELECT count(*) FROM assertion WHERE instr(body,'uniq3z')>0")==0 );   /* not left in a delta */
    CHECK( vk_erase(p.st,ids[5],B("t"),gRank0,t)==VK_OK );                  /* the head */
    for(v=0;v<5;v++){ if( v==3 ) continue; doc(v,buf,sizeof buf); CHECK( body_is(p,ids[v],buf) ); }
    CHECK( count_hits(p,"uniq5z")==0 );
    CHECK( sqlite3_exec(p.db,"INSERT INTO vk_bm25(vk_bm25, rank) VALUES('integrity-check', 1)",0,0,0)==SQLITE_OK );
    drop(p);
}
/* ---- OL: outlines ---- */
static VkAssertion outline_of(const char *zWho, const VkId target, const char *zJson, VkId key,
                              const VkId *der, uint32_t nDer){
    VkAssertion a = mk(zWho,"x",1,zJson);
    vk_outline_key(target,B(zWho),key);
    a.kind = B("outline");
    a.akey.p = key; a.akey.n = VK_ID_BYTES;
    a.reference.p = target; a.reference.n = VK_ID_BYTES;
    a.derivedFrom = der; a.nDerivedFrom = nDer;
    return a;
}
static const char *zGood =
    "[{\"title\":\"first half\",\"lo\":0,\"hi\":1500,\"children\":["
    "{\"title\":\"opening\",\"lo\":0,\"hi\":500},{\"title\":\"middle\",\"lo\":500,\"hi\":1500}]},"
    "{\"title\":\"second half\",\"lo\":1500,\"hi\":2900,\"children\":[]}]";
static void t_ol1(void){
    Peer p = peer("alice"), q = peer("bob");
    char buf[4000]; VkId tid, key, oid, missing; VkAssertion t0, o; VkMergeReport rep;
    static const char *azBad[] = {
        "not json",
        "{\"title\":\"x\",\"lo\":0,\"hi\":10}",                         /* not a list */
        "[{\"lo\":0,\"hi\":10}]",                                         /* no title */
        "[{\"title\":\"x\",\"lo\":10,\"hi\":10}]",                     /* empty range */
        "[{\"title\":\"x\",\"lo\":-1,\"hi\":10}]",                     /* before the start */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":100,\"children\":[{\"title\":\"y\",\"lo\":50,\"hi\":150}]}]",  /* outside its parent */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":100},{\"title\":\"y\",\"lo\":50,\"hi\":150}]",               /* siblings overlap */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":100},{\"title\":\"y\",\"lo\":0,\"hi\":40}]",                 /* out of order */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":10,\"extra\":1}]",        /* an unknown field */
        "[{\"title\":7,\"lo\":0,\"hi\":10}]",                            /* a title that is not text */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":99999}]",                   /* past the target's end */
        "[1]",                                                                /* a node that is not an object */
        "[{\"title\":\"x\",\"lo\":0,\"hi\":10,\"children\":[[]]}]",  /* a list inside children */
    };
    size_t i;
    doc(0,buf,sizeof buf);
    t0 = mk("alice","big",1,buf);
    CHECK( vk_put(p.st,&t0,tid)==VK_OK );
    o = outline_of("alice",tid,zGood,key,(const VkId*)&tid,1);
    CHECK( vk_put(p.st,&o,oid)==VK_OK );
    for(i=0;i<sizeof(azBad)/sizeof(azBad[0]);i++){
        VkId bad;
        VkStatus st;
        o = outline_of("alice",tid,azBad[i],key,(const VkId*)&tid,1);
        st = vk_put(p.st,&o,bad);
        if( st!=VK_EINVAL || strncmp(vk_errmsg(p.st),"OL-1",4)!=0 ) printf("  accepted: %s\n",azBad[i]);
        CHECK( st==VK_EINVAL );
    }
    {   /* JSON text, not JSONB: a valid outline's JSONB bytes are refused */
        sqlite3_stmt *st = 0; VkId bad; VkAssertion x;
        CHECK( sqlite3_prepare_v2(p.db,"SELECT jsonb(?1)",-1,&st,0)==SQLITE_OK );
        sqlite3_bind_text(st,1,zGood,-1,SQLITE_STATIC);
        CHECK( sqlite3_step(st)==SQLITE_ROW );
        x = outline_of("alice",tid,"",key,(const VkId*)&tid,1);
        x.body.p = (const uint8_t*)sqlite3_column_blob(st,0);
        x.body.n = (uint32_t)sqlite3_column_bytes(st,0);
        CHECK( vk_put(p.st,&x,bad)==VK_EINVAL );
        sqlite3_finalize(st);
    }
    {   /* the frame: the akey, the reference, the link to the target */
        VkId bad, other; VkAssertion x;
        memset(other,9,sizeof other);
        x = outline_of("alice",tid,zGood,key,(const VkId*)&tid,1); x.akey = B("not the key");
        CHECK( vk_put(p.st,&x,bad)==VK_EINVAL );
        x = outline_of("alice",tid,zGood,key,(const VkId*)&other,1);       /* derived from something else */
        CHECK( vk_put(p.st,&x,bad)==VK_EINVAL );
        memset(missing,5,sizeof missing);
        x = outline_of("alice",missing,zGood,key,(const VkId*)&missing,1); /* a target this store lacks */
        CHECK( vk_put(p.st,&x,bad)==VK_EINVAL && strncmp(vk_errmsg(p.st),"OL-1",4)==0 );
    }
    /* by merge: a broken shape is quarantined; a target not yet held is no reason to refuse */
    {
        VkId k2, i2; VkAssertion x;
        memset(&rep,0,sizeof rep);
        x = outline_of("bob",tid,azBad[5],k2,(const VkId*)&tid,1);
        vk_assertion_id(&x,i2);
        CHECK( vk_ingest(p.st,&x,i2,&rep)==VK_OK && rep.quarantined==1 );   /* a broken shape */
        x = outline_of("bob",missing,zGood,k2,(const VkId*)&missing,1);
        vk_assertion_id(&x,i2);
        CHECK( vk_ingest(p.st,&x,i2,&rep)==VK_OK && rep.added==1 );         /* a target not yet held */
        x = outline_of("bob",tid,azBad[10],k2,(const VkId*)&tid,1);
        vk_assertion_id(&x,i2);
        CHECK( vk_ingest(p.st,&x,i2,&rep)==VK_OK && rep.quarantined==2 );   /* past a held target's end */
    }
    drop(p); drop(q);
}
static void t_ol2(void){
    Peer p = peer("alice"), b = peer("bob");
    char buf[4000]; VkId tid, ka, kb, oa, ob, oa2; VkAssertion t0, o; VkRow r; VkMergeReport rep;
    doc(0,buf,sizeof buf);
    t0 = mk("alice","big",1,buf);
    CHECK( vk_put(p.st,&t0,tid)==VK_OK );
    o = outline_of("alice",tid,zGood,ka,(const VkId*)&tid,1);
    CHECK( vk_put(p.st,&o,oa)==VK_OK );
    memset(&rep,0,sizeof rep);
    CHECK( vk_merge(b.st,p.st,&rep)==VK_OK );
    o = outline_of("bob",tid,"[{\"title\":\"all of it\",\"lo\":0,\"hi\":2900}]",kb,(const VkId*)&tid,1);
    CHECK( vk_put(b.st,&o,ob)==VK_OK );
    memset(&rep,0,sizeof rep);
    CHECK( vk_merge(p.st,b.st,&rep)==VK_OK );
    CHECK( !id_eq(ka,kb) );                                     /* two authors: two keys, side by side */
    CHECK( vk_current(p.st,(VkBytes){ka,VK_ID_BYTES},&r)==VK_OK && id_eq(r.id,oa) ); vk_row_close(&r);
    CHECK( vk_current(p.st,(VkBytes){kb,VK_ID_BYTES},&r)==VK_OK && id_eq(r.id,ob) ); vk_row_close(&r);
    /* one author's newer outline supersedes the older, and may name it as carried forward (OL-3) */
    {
        VkId der[2]; memcpy(der[0],tid,32); memcpy(der[1],oa,32);
        o = outline_of("alice",tid,"[{\"title\":\"the document\",\"lo\":0,\"hi\":2900}]",ka,(const VkId*)der,2);
        o.supersedes = (const VkId*)&oa; o.nSupersedes = 1; o.rank[15] = 2;
        CHECK( vk_put(p.st,&o,oa2)==VK_OK );
    }
    CHECK( vk_current(p.st,(VkBytes){ka,VK_ID_BYTES},&r)==VK_OK && id_eq(r.id,oa2) ); vk_row_close(&r);
    CHECK( vk_current(p.st,(VkBytes){kb,VK_ID_BYTES},&r)==VK_OK && id_eq(r.id,ob) ); vk_row_close(&r);
    drop(p); drop(b);
}
static void t_q2(void){
    Peer p = peer("alice");
    VkSearch s; VkCoverage c; VkId id;
    VkAssertion a = mk("alice","a",1,"x"), b = mk("alice","b",1,"y");
    vk_put(p.st,&a,id); vk_put(p.st,&b,id);
    CHECK( vk_search_open(p.st,&vkGrep,B("absent"),&s)==VK_OK );
    CHECK( vk_search_coverage(&s,&c)==VK_OK );
    CHECK( c.nSearched==2 && c.bExhaustive==1 && strcmp(c.zMethod,"grep")==0 );
    vk_search_close(&s);
    drop(p);
}
static VkStatus bad_check(VkStore *p){ (void)p; return VK_BLIND; }
static VkStatus bad_open(VkStore *p, VkBytes q, sqlite3_stmt **pp){ (void)p;(void)q;(void)pp; return VK_ESQL; }
static void t_q3(void){
    static const VkSearchMethod broken = { "broken", 1, bad_open, bad_check };
    Peer p = peer("alice");
    VkSearch s;
    CHECK( vkGrep.selfcheck(p.st)==VK_OK );
    CHECK( vk_search_open(p.st,&broken,B("x"),&s)==VK_BLIND );  /* blind, never not-found */
    vk_search_close(&s);
    drop(p);
}
static void t_q5(void){
    Peer p = peer("alice");
    VkAssertion a = mk("alice","k",1,"secret word");
    VkId i, t;
    CHECK( vk_put(p.st,&a,i)==VK_OK );
    CHECK( count_hits(p,"secret")==1 );
    CHECK( vk_erase(p.st,i,B("t"),gRank0,t)==VK_OK );
    CHECK( count_hits(p,"secret")==0 );
    drop(p);
}

/* ---- ENV-2: memory does not follow the store ---- */
static sqlite3_int64 merge_highwater(int n){
    Peer a, b; VkMergeReport rep; int i; VkId id; char body[256], key[32];
    sqlite3_int64 hw; char pa[96], pb[96];
    open_file(&a,"env2a.db",pa,sizeof pa); open_file(&b,"env2b.db",pb,sizeof pb);
    sqlite3_exec(a.db,"PRAGMA cache_size=32;PRAGMA synchronous=OFF;PRAGMA journal_mode=MEMORY",0,0,0);
    sqlite3_exec(b.db,"PRAGMA cache_size=32;PRAGMA synchronous=OFF;PRAGMA journal_mode=MEMORY",0,0,0);
    CHECK( vk_open(a.db,B("alice"),gGrants,7,&a.st)==VK_OK );
    CHECK( vk_open(b.db,B("alice"),gGrants,7,&b.st)==VK_OK );
    memset(&rep,0,sizeof(rep));
    sqlite3_exec(a.db,"BEGIN",0,0,0);
    for(i=0;i<n;i++){
        VkAssertion x;
        snprintf(key,sizeof key,"k%d",i);
        memset(body,'b',200); snprintf(body+200,40,"%d",i);
        x = mk("alice",key,1,body);
        CHECK( vk_put(a.st,&x,id)==VK_OK );
    }
    sqlite3_exec(a.db,"COMMIT",0,0,0);
    sqlite3_status64(SQLITE_STATUS_MEMORY_USED,&hw,&hw,1);   /* reset the high-water mark */
    CHECK( vk_merge(b.st,a.st,&rep)==VK_OK );
    CHECK( rep.added==n );
    { sqlite3_int64 cur; sqlite3_status64(SQLITE_STATUS_MEMORY_USED,&cur,&hw,0); }
    drop_file(a,pa); drop_file(b,pb);
    return hw;
}
static void t_env2(void){
    sqlite3_int64 small = merge_highwater(200), big = merge_highwater(3000);
    printf("  merge high water: 200 rows %lld bytes, 3000 rows %lld bytes\n",(long long)small,(long long)big);
    CHECK( small>0 );
    CHECK( big < small + small/4 );
}

/* ---- every failure, in turn ---- */
typedef struct { int nStep; Peer p; Peer src; VkId i, j, b0, b1; int nDone; } Script;

static VkStatus script_step(Script *s, int k){
    VkId id, par[1]; VkRow r; VkMergeReport rep; VkSearch q; VkStatus st; int64_t bad;
    VkAssertion a = mk("alice","k",1,"one"), b = mk("alice","k",2,"two"), c = mk("alice","k",3,"three");
    memset(&rep,0,sizeof(rep));
    vk_assertion_id(&a,s->i);
    switch( k ){
    case 0: st = vk_open(s->p.db,B("alice"),gGrants,7,&s->p.st); if(st==VK_OK) return VK_OK; s->p.st = 0; return st;
    case 1: return vk_put(s->p.st,&a,id);
    case 2: memcpy(par[0],s->i,32); b.supersedes=(const VkId*)par; b.nSupersedes=1; return vk_put(s->p.st,&b,s->j);
    case 3: return vk_put_chain(s->p.st,&c,id);
    case 4: return vk_merge(s->p.st,s->src.st,&rep);
    case 5: return vk_erase(s->p.st,s->i,B("t"),gRank0,id);
    case 6: st = vk_current(s->p.st,B("k"),&r); if(st==VK_OK) vk_row_close(&r); return st;
    case 7: st = vk_search_open(s->p.st,&vkGrep,B("three"),&q);
            if(st==VK_OK){ while((st=vk_search_next(&q,id))==VK_OK){} vk_search_close(&q); if(st==VK_DONE) st=VK_OK; }
            return st;
    case 8: return vk_verify(s->p.st,&bad);
    case 9: { char key[40]; VkAssertion l = reading("alice","door",1,key,sizeof key); return vk_append(s->p.st,&l,id); }
    case 10: { char key[40]; VkAssertion l = reading("alice","door",2,key,sizeof key); return vk_append(s->p.st,&l,id); }
    case 11: return vk_forget(s->p.st,B("door"),B("alice"),1,2,B("t"),gRank0,id);
    case 12: { char key[40]; VkAssertion l = reading("alice","door",3,key,sizeof key); return vk_append(s->p.st,&l,id); }
    /* DS: a body big enough to become a delta, superseded, then its head erased */
    case 13: { char buf[4000]; VkAssertion x = version(buf,sizeof buf,0,0); return vk_put(s->p.st,&x,s->b0); }
    case 14: { char buf[4000]; VkAssertion x = version(buf,sizeof buf,1,(const VkId*)&s->b0); return vk_put(s->p.st,&x,s->b1); }
    case 15: return vk_erase(s->p.st,s->b1,B("t"),gRank0,id);
    /* OL-1: an outline of the surviving version, so its checks are failed in turn too */
    case 16: { VkId key; VkAssertion o = outline_of("alice",s->b0,"[{\"title\":\"top\",\"lo\":0,\"hi\":100}]",
                                                key,(const VkId*)&s->b0,1);
               return vk_put(s->p.st,&o,id); }
    }
    return VK_OK;
}
#define NSCRIPT 17

/* Runs the script once, failing the k-th tick; 0 means no fault. A step that
** fails must succeed when retried, and the store must end up the same. */
static void run_script(long k, int bMalloc, VkId out, int *pFired){
    Script s; int i;
    memset(&s,0,sizeof(s));
    CHECK( sqlite3_open(":memory:",&s.p.db)==SQLITE_OK );
    s.src = peer("bob");
    put_n(s.src,"bob","other",2);
    gFaultCount = 0; gFaultFired = 0; gFaultAt = k; gMallocArmed = bMalloc;
    for(i=0;i<NSCRIPT;i++){
        VkStatus st = script_step(&s,i);
        if( st!=VK_OK && st!=VK_EXISTS ){
            disarm();
            st = script_step(&s,i);
            if( st!=VK_OK && st!=VK_EXISTS ) printf("  fault %ld (malloc=%d): step %d retry gave %d\n",k,bMalloc,i,(int)st);
            CHECK( st==VK_OK || st==VK_EXISTS );
        }
    }
    *pFired = gFaultFired;
    disarm(); gMallocArmed = 0;
    digest(s.p,out);
    { int64_t bad = -1; CHECK( vk_verify(s.p.st,&bad)==VK_OK ); CHECK( bad==0 ); }
    drop(s.p); drop(s.src);
}
static void faults(int bMalloc){
    VkId want, got; int fired = 1; long k;
    run_script(0,bMalloc,want,&fired);
    for(k=1; k<200000; k++){
        run_script(k,bMalloc,got,&fired);
        if( !fired ) break;
        if( !id_eq(want,got) ){ printf("  fault %ld (malloc=%d): different store\n",k,bMalloc); gFailed = 1; }
        if( gFailed ) break;
    }
    printf("  %ld %s points failed in turn\n",k-1,bMalloc?"allocation":"sqlite call");
}
static void t_fault_sql(void){ faults(0); }
static void t_fault_mem(void){ faults(1); }

/* ---- runner ---- */
typedef struct { const char *zId; const char *zName; void (*x)(void); } Test;
static const Test aTest[] = {
    {"A-1","sha-256 known answer",t_sha},
    {"A-1","two peers stating the same thing get one id",t_a1},
    {"A-2","every framed field changes the id",t_a2},
    {"A-2","link order does not change the id",t_a2_sorted},
    {"A-3","no value can forge a field boundary",t_a3},
    {"A-4","narrowed: bytes are hashed as given",t_a4},
    {"A-4a","arbitrary bytes round-trip",t_a4a},
    {"A-5","a repeat is a reported no-op",t_a5},
    {"A-7","a forged body is refused on arrival",t_a7},
    {"A-7","every stored id recomputes, erased rows included",t_a7_recompute},
    {"D-3","originate and supersede are separate rights",t_d3},
    {"D-3a","a logger holding only s extends chains and nothing else",t_d3a},
    {"D-3f","supersede by chain takes every current arm",t_d3f},
    {"R-1","current follows the chain and ignores rank",t_r1},
    {"R-2","resolve is rank then id, descending",t_r2},
    {"R-3","resolution does not depend on arrival order",t_order},
    {"R-4a","a row from an unentitled author is stored and ignored",t_r4a},
    {"R-5","a fork is reported",t_r5},
    {"R-7","one assertion heals a fork",t_r7},
    {"LOG-1","positions count per log; a position needs a log",t_log1},
    {"LOG-2","a row links to pos-2^j, and a wrong link count is refused",t_log2},
    {"LOG-3","append and chain cost do not follow the length",t_log3},
    {"LOG-4","pages written per row do not follow the store",t_log4},
    {"LOG-4","a logged row is deduplicated without an id index",t_log4_dedupe},
    {"W-3","a logged row can be erased by id, in either order",t_w3_logged},
    {"M-1","the digest tells an erased row from a held one",t_digest_erased},
    {"E-1","a failed write reports the SQLite cause after its undo",t_e_full},
    {"R-1a","a link to another akey supersedes nothing",t_r1a},
    {"W-15","a range goes entirely and one tombstone stays",t_w15},
    {"W-15","the ids a tombstone carries let the log continue",t_w15_carry},
    {"W-15","an unentitled range tombstone is stored and ignored",t_w15_unentitled},
    {"W-16","a covered row is not stored, in either order",t_w16},
    {"W-17","a range that would orphan a superseded row is refused",t_w17},
    {"R-4a","heads are refolded when the grants change",t_arm_rebuild},
    {"W-3","erasing a correction does not bring back the corrected",t_w3_no_resurrection},
    {"W-2","a tombstone cannot be erased",t_w2},
    {"W-6","a tombstone arriving first still takes effect",t_w6},
    {"M-1","merge is idempotent, commutative, associative",t_m1},
    {"M-2","self-merge changes nothing",t_m2},
    {"M-3","an unfinished merge says so",t_m3},
    {"M-7","a bad row is quarantined and counted",t_m7},
    {"Q-2","a search states its coverage",t_q2},
    {"Q-3","a method failing self-check is blind",t_q3},
    {"Q-5","erased content never appears",t_q5},
    {"Q-6","grep is exact and exhaustive",t_q6_grep},
    {"Q-8","a hit's id reads back as its whole assertion",t_q8},
    {"Q-4","a hit says whether it is current, superseded, or one head of a fork",t_q4},
    {"OL-1","an outline's shape and frame are checked on write and on merge",t_ol1},
    {"OL-2","outlines by two authors stand side by side; an author's newer one supersedes",t_ol2},
    {"DS-1","a delta rebuilds its version exactly, and a malformed one is refused",t_ds_codec},
    {"DS-1","the latest is whole, the superseded a delta, and both read and search as written",t_ds1},
    {"DS-2","a long chain stays within the bound and every version reads back",t_ds2},
    {"DS-3","arrival order does not change what is read or the digest",t_ds3},
    {"DS-4","erasing a middle version or a head loses only that version",t_ds4},
    {"ENV-4","the SQLite underneath is SQLCipher, 3.44 or later, with FTS5",t_env4},
    {"Q-6","bm25 is ranked, OR'd, quoted, and blind until indexed",t_q6_bm25},
    {"Q-5","erased and forgotten content leaves the bm25 index",t_q5_bm25},
    {"ENV-2","merge memory does not grow with the store",t_env2},
    {"-","every sqlite call fails in turn; retry reaches the same store",t_fault_sql},
    {"-","every allocation fails in turn; retry reaches the same store",t_fault_mem},
};

int main(void){
    sqlite3_mem_methods m;
    size_t i; int nFail = 0;
    sqlite3_config(SQLITE_CONFIG_GETMALLOC,&gRealMem);
    m = gRealMem; m.xMalloc = fail_malloc; m.xRealloc = fail_realloc;
    sqlite3_config(SQLITE_CONFIG_MALLOC,&m);
    sqlite3_config(SQLITE_CONFIG_MEMSTATUS,1);
    sqlite3_initialize();
    init_grants();
    for(i=0;i<sizeof(aTest)/sizeof(aTest[0]);i++){
        gFailed = 0;
        aTest[i].x();
        printf("%s %s %s\n",gFailed?"FAIL":"PASS",aTest[i].zId,aTest[i].zName);
        fflush(stdout);
        nFail += gFailed;
    }
    printf("%d of %d failed\n",nFail,(int)(sizeof(aTest)/sizeof(aTest[0])));
    if( gDir[0] ) rmdir(gDir);
    return nFail!=0;
}
