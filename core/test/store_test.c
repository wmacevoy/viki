/*
** store_test.c -- viki_store.c against REQUIREMENTS_v3. Each test names the
** requirement it holds to; the output is what test/gate.py reads.
**
** Built with VK_TEST_FAULTS: the fault tests fail every SQLite call and
** every allocation in turn, and require a retry to reach the same store.
*/
#include "viki_store.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- W: forgetting ---- */
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
    sqlite3_int64 hw;
    sqlite3_open("",&a.db); sqlite3_open("",&b.db);
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
    drop(a); drop(b);
    return hw;
}
static void t_env2(void){
    sqlite3_int64 small = merge_highwater(200), big = merge_highwater(3000);
    printf("  merge high water: 200 rows %lld bytes, 3000 rows %lld bytes\n",(long long)small,(long long)big);
    CHECK( small>0 );
    CHECK( big < small + small/4 );
}

/* ---- every failure, in turn ---- */
typedef struct { int nStep; Peer p; Peer src; VkId i, j; int nDone; } Script;

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
    }
    return VK_OK;
}
#define NSCRIPT 9

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
    return nFail!=0;
}
