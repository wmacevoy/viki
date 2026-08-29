/*
** core_probe.c -- viki-core's proof.
**
** Most assertions are paired with a CONTROL that must come out the other way,
** because this repository's recurring failure is a suite that is green for
** reasons unrelated to the claim: a keywrap build once passed its own
** round-trip while emitting keys real `age` rejected, and a retrieval probe
** scored 7/7 against a binary with no leg at all.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "viki_core.h"
#include "viki_cal.h"

static int nPass = 0, nFail = 0;
static void ok(const char *z){ nPass++; printf("  ok    %s\n", z); }
static void bad(const char *z, const char *why){
    nFail++; printf("  FAIL  %s\n        %s\n", z, why?why:"");
}
static void check(int c, const char *z, const char *why){ if(c) ok(z); else bad(z,why); }

/* ---- a stub embedder ------------------------------------------------
** TOPIC vectors, deliberately built so a query can hit a chunk it shares NO
** WORD with. That is what makes E2 a real test of the vector leg rather than
** a second keyword leg wearing a float coat. */
#define DIM 4
static const char *azTopic[DIM][6] = {
  {"gate","latch","fence","hinge","boundary","fastener"},
  {"invoice","payment","billing","refund","charge","receipt"},
  {"vet","dog","puppy","kennel","veterinary","paw"},
  {"kernel","compiler","pointer","socket","syscall","linker"}
};
static int stubEmbed(void *pApp, const char *zText, float *aOut, int nDim){
    int d, w; (void)pApp;
    if( nDim!=DIM ) return 1;
    for(d=0;d<DIM;d++){
        aOut[d] = 0.0f;
        for(w=0;w<6;w++){
            const char *p = zText;
            size_t n = strlen(azTopic[d][w]);
            while( (p = strstr(p, azTopic[d][w])) ){ aOut[d] += 1.0f; p += n; }
        }
    }
    return 0;
}

/* deep call sites: nothing here is handed a store */
static void deepest(void){ viki_note("written from the deepest frame"); }
static void c3(void){ deepest(); }
static void c2(void){ c3(); }
static void via_callback(void *ignored){ (void)ignored; c2(); }

/* A deliberately broken subtype: key() returns NULL. Legal by the vftbl
** signature, and exactly the shape an unkeyed calendar assertion would have. */
static const VikiType   nullKeyType  = { "nullkey", 0, sizeof(VikiNote) };
static const char *nkKey  (const VikiAssert *p){ (void)p; return 0; }
static const char *nkRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *nkText (const VikiAssert *p){ return ((const VikiNote*)p)->zText; }
static const char *nkCanon(const VikiAssert *p){ return ((const VikiNote*)p)->zText; }
static const struct VikiAssertVftbl nullKeyVftbl = {
    &nullKeyType, nkKey, nkRank, nkText, nkCanon
};

/* THE PROBE DOES NOT READ THE TABLES.
**
** Everything below goes through the API, and that is a requirement rather
** than tidiness: if a property cannot be asserted through viki_count() /
** viki_each() / viki_ask(), the API has a gap, and a probe reaching past it
** would hide that gap instead of reporting it. core-probe.sh's C8 enforces
** it by grepping this file for table names.
**
** The one exception is viki_sql(), which IS the API -- SCOPES 1b requires a
** raw rung so a curated verb is never the only door. Using it here is going
** THROUGH core, not around it. */
static int nOf(VikiCountWhat what, const char *zFilter){
    int n = -1;
    viki_count(what, zFilter, &n);
    return n;
}
/* Collects the bodies of matching assertions, for assertions about content. */
typedef struct { char *az[16]; int n; int nCurrent; } Bodies;
static int collect(void *pApp, const char *zId, const char *zKind,
                   const char *zKey, const char *zTs, const char *zBody, int bCur){
    Bodies *b = (Bodies*)pApp;
    (void)zId; (void)zKind; (void)zKey; (void)zTs;
    if( bCur ) b->nCurrent++;
    if( b->n < 16 ) b->az[b->n++] = zBody ? strdup(zBody) : 0;
    return 0;
}
static void bodies_free(Bodies *b){ int i; for(i=0;i<b->n;i++) free(b->az[i]); b->n=0; }
/* A scalar read THROUGH viki_sql -- the sanctioned raw rung (SCOPES 1b),
** not a second door. Used only where the assertion is deliberately about the
** SCHEMA CONTRACT rather than about behaviour: "viki_chunk has no text
** column", "no two rows share a range". Those are white-box on purpose, and
** going through viki_sql keeps even them inside the API. */
static int sqlRow(void *pApp, int nCol, const char *const *az){
    (void)nCol; *(int*)pApp = az[0] ? atoi(az[0]) : 0; return 1;
}
static int sqlN(const char *zSql){ int n = -1; viki_sql(zSql, sqlRow, &n); return n; }

static Bodies gather(const char *zKind, const char *zKey){
    Bodies b; memset(&b,0,sizeof b); viki_each(zKind, zKey, collect, &b); return b;
}

/* Collects resolved events through the calendar's own read verb, so the
** probe never names viki_event either. */
typedef struct {
    int n; char zTzid[64]; char zForm[16]; char zSummary[128];
    int nZoned, nOffset, nFloating;
    int  bMasterOnly;      /* recurrence_id == "" -- the series master       */
    char zWant[64];
    char zWantUid[64];
} Cal;
static int calRow(void *pApp, const VikiEvent *e){
    Cal *c = (Cal*)pApp;
    if( c->bMasterOnly && e->zRecurrenceId[0] ) return 0;
    if( c->zWant[0] && strcmp(e->zRecurrenceId, c->zWant)!=0 ) return 0;
    if( c->zWantUid[0] && strcmp(e->zUid, c->zWantUid)!=0 ) return 0;
    c->n++;
    if( e->zDtstartForm && strcmp(e->zDtstartForm,"zoned")==0 )    c->nZoned++;
    if( e->zDtstartForm && strcmp(e->zDtstartForm,"floating")==0 ) c->nFloating++;
    if( e->zDtstart && (strchr(e->zDtstart,'+') || strchr(e->zDtstart,'Z')) ) c->nOffset++;
    snprintf(c->zTzid, sizeof c->zTzid, "%s", e->zDtstartTzid?e->zDtstartTzid:"");
    snprintf(c->zForm, sizeof c->zForm, "%s", e->zDtstartForm?e->zDtstartForm:"");
    snprintf(c->zSummary, sizeof c->zSummary, "%s", e->zSummary?e->zSummary:"");
    return 0;
}
/* zRecur: NULL = every row, "" = the series MASTER only (an empty string is
** a real recurrence-id value, so it cannot double as "no filter"), otherwise
** that override. */
static Cal calAll(const char *zRecur, const char *zUid){
    Cal c; memset(&c,0,sizeof c);
    if( zRecur && !zRecur[0] ) c.bMasterOnly = 1;
    else if( zRecur ) snprintf(c.zWant, sizeof c.zWant, "%s", zRecur);
    if( zUid )   snprintf(c.zWantUid, sizeof c.zWantUid, "%s", zUid);
    viki_cal_events(0, 0, calRow, &c);
    return c;
}

/* ---- a listener ------------------------------------------------------ */
typedef struct {
    int nPut, nSup, nForget, nMerge, nProject;
    char zLastId[VIKI_ID_HEX+1];
    char zLastKey[256];
    int  bMutateRefused;
} Seen;
static void onEvent(void *pApp, const VikiEvent2 *e){
    Seen *s = (Seen*)pApp;
    switch( e->kind ){
      case VIKI_EV_PUT:        s->nPut++;     break;
      case VIKI_EV_SUPERSEDED: s->nSup++;     break;
      case VIKI_EV_FORGOTTEN:  s->nForget++;  break;
      case VIKI_EV_MERGED:     s->nMerge++;   break;
      case VIKI_EV_PROJECTED:  s->nProject++; break;
    }
    if( e->zId )  snprintf(s->zLastId,  sizeof(s->zLastId),  "%s", e->zId);
    if( e->zKey ) snprintf(s->zLastKey, sizeof(s->zLastKey), "%s", e->zKey);
}
/* A listener that tries to WRITE. Must be refused, not deadlock or corrupt
** the queue it is being dispatched from. */
static void onEventMutating(void *pApp, const VikiEvent2 *e){
    Seen *s = (Seen*)pApp;
    (void)e;
    if( viki_note("written from inside a listener")==VIKI_EBUSY ) s->bMutateRefused = 1;
}

/* ---- two stand-in identities ----------------------------------------
** A toy keyed hash, not a cipher: what is under test is core's BOOKKEEPING --
** which rows exist, what merges, what verify is asked -- not anyone's
** cryptography. A real host calls the Secure Enclave or an ed25519 library
** here, and core cannot tell the difference, which is the point. */
typedef struct { const char *zPub; const char *zPriv; int nSignCalls; int bDecline; } Key;
static void toysig(const char *zPriv, const char *zId, unsigned char *aOut, int *pn){
    unsigned h1 = 2166136261u, h2 = 16777619u;
    const char *p;
    int i;
    for(p=zPriv; *p; p++){ h1 = (h1 ^ (unsigned char)*p) * 16777619u; }
    for(p=zId;   *p; p++){ h2 = (h2 ^ (unsigned char)*p) * 2166136261u; h2 ^= h1; }
    for(i=0;i<16;i++){ aOut[i] = (unsigned char)(h2 >> ((i%4)*8)); h2 = h2*31 + (unsigned)i; }
    *pn = 16;
}
static int keySign(void *pApp, const char *zId, unsigned char *aSig, int *pnSig){
    Key *k = (Key*)pApp;
    k->nSignCalls++;
    if( k->bDecline ) return 1;          /* a cancelled thumbprint prompt */
    toysig(k->zPriv, zId, aSig, pnSig);
    return 0;
}
/* VERIFY TAKES A PUBLIC KEY AND NOTHING ELSE -- no pApp secret is consulted.
** A peer checking who said what holds no private material. */
static int keyVerify(void *pApp, const char *zPubKey, const char *zId,
                     const unsigned char *aSig, int nSig){
    unsigned char aWant[VIKI_SIG_MAX]; int nWant = 0;
    (void)pApp;
    /* the toy's "public key" is its private key spelled backwards, so the
    ** verifier can derive the expected signature without holding a secret of
    ** its own -- the property S3 is about, faked just enough to test it */
    { static char zPriv[64]; size_t n = strlen(zPubKey), i2;
      if( n>=sizeof zPriv ) return 1;
      for(i2=0;i2<n;i2++) zPriv[i2] = zPubKey[n-1-i2];
      zPriv[n] = 0;
      toysig(zPriv, zId, aWant, &nWant); }
    return (nWant==nSig && memcmp(aWant,aSig,(size_t)nSig)==0) ? 0 : 1;
}

int main(void){
    sqlite3 *dbA = 0, *dbB = 0;
    VikiStore sA, sB;
    VikiEmbed emb;
    VikiHits *h = 0;
    char id1[VIKI_ID_HEX+1], id2[VIKI_ID_HEX+1];
    int n = 0, i, rank_gate;

    sqlite3_open(":memory:", &dbA);
    sqlite3_open(":memory:", &dbB);
    sA.db = dbA; sB.db = dbB;
    check(viki_attach(dbA)==VIKI_OK, "A0 viki_attach creates the schema", viki_errmsg());
    viki_attach(dbB);

    printf("\n== A: the context ==\n");
    check(viki_note("this must not be silently lost")==VIKI_ENOCTX,
          "A1 CONTROL: with NO store retained, viki_note FAILS", "it returned OK");

    {
        RETAIN_BEGIN(VikiStore, &sA, gA);
        check(viki_note("the gate latch sticks below freezing")==VIKI_OK,
              "A2 viki_note() takes one argument", viki_errmsg());
        via_callback(0);
        check(nOf(VIKI_N_ASSERT,0)==2,
              "A3 a note four frames down, through a callback, was stored", "count != 2");

        viki_noteid("identical bytes", id1);
        viki_noteid("identical bytes", id2);
        check(strcmp(id1,id2)==0 && nOf(VIKI_N_ASSERT,0)==3,
              "A4 identity IS the content hash: same bytes, same id, one row", "differed");

        {   /* A nested store, then the outer restored.
            ** nB is read INSIDE the nested scope on purpose: viki_count()
            ** answers about the RETAINED store, so asking outside would ask
            ** about A. That the counts cannot be taken from the wrong place
            ** is the property, not an inconvenience. */
            int nB;
            RETAIN_BEGIN(VikiStore, &sB, gB);
            viki_note("this belongs to store B");
            nB = nOf(VIKI_N_ASSERT,0);
            RETAIN_END(gB);
            check(nB==1 && nOf(VIKI_N_ASSERT,0)==3,
                  "A5 a nested store is separate, and the outer one is restored", "leaked");
        }
        RETAIN_END(gA);
    }
    check(viki_note("after scope")==VIKI_ENOCTX,
          "A6 CONTROL: after the scope ends, the store is gone again", "still retained");

    printf("\n== M: merge is union ==\n");
    {
        RETAIN_BEGIN(VikiStore, &sA, g);
        check(viki_merge(dbB, &n)==VIKI_OK && n==1,
              "M1 merging another store adds its assertions", "n != 1");
        check(viki_merge(dbB, &n)==VIKI_OK && n==0,
              "M2 CONTROL: merging again adds NOTHING (union is idempotent)", "n != 0");
        RETAIN_END(g);
    }

    printf("\n== S: resolution at read time ==\n");
    {
        VikiNote a, b; char idA[65];
        RETAIN_BEGIN(VikiStore, &sA, g);
        memset(&a,0,sizeof a); a.vftbl=&vikiNoteVftbl;
        a.zText="draft one"; a.zKey="plan"; a.zTs="2026-08-01T00:00:00Z";
        viki_put((VikiAssert*)&a); memcpy(idA, a.zId, 65);
        memset(&b,0,sizeof b); b.vftbl=&vikiNoteVftbl;
        b.zText="draft two"; b.zKey="plan"; b.zTs="2026-08-02T00:00:00Z";
        viki_put((VikiAssert*)&b);
        {
            char *zBody = 0;
            check(viki_current("plan", 0, &zBody)==VIKI_OK
                  && zBody && strcmp(zBody,"draft two")==0,
                  "S1 the highest-ranked assertion on a key is current", zBody?zBody:"none");
            free(zBody);
        }
        {   /* now supersede the winner explicitly */
            VikiNote c; char *zBody = 0;
            memset(&c,0,sizeof c); c.vftbl=&vikiNoteVftbl;
            c.zText="draft three"; c.zKey="plan"; c.zTs="2026-08-03T00:00:00Z";
            c.zSupersedes = b.zId;
            viki_put((VikiAssert*)&c);
            viki_current("plan", 0, &zBody);
            check(zBody && strcmp(zBody,"draft three")==0,
                  "S2 a superseded assertion is NOT current", zBody?zBody:"none");
            free(zBody);
            check(gather(0,"plan").n==3,
                  "S3 CONTROL: the superseded row is RETAINED, not deleted", "row vanished");
        }
        RETAIN_END(g);
    }

    printf("\n== R: retrieval ==\n");
    {
        RETAIN_BEGIN(VikiStore, &sA, g);
        viki_note("the veterinary appointment for the puppy is on Tuesday");
        viki_note("kernel pointer arithmetic in the linker is subtle");
        viki_reindex(0, &n);
        check(n>0, "R0 reindex produced chunks", "no chunks");
        check(viki_ask("gate latch freezing", 5, &h)==VIKI_OK && h->n>0
              && strstr(h->a[0].zText,"gate latch"),
              "R1 keyword+literal find the planted answer at rank 1",
              h&&h->n?h->a[0].zText:"no hits");
        check(h && h->bDegraded==1,
              "R2 with no embedder retained, hits are marked DEGRADED", "not marked");
        viki_hits_free(h); h=0;

        check(viki_ask("zzzznotpresent", 5, &h)==VIKI_OK && h->n==0,
              "R3 CONTROL: a query matching nothing returns nothing", "returned hits");
        viki_hits_free(h); h=0;
        RETAIN_END(g);
    }

    printf("\n== Q: the vector leg ==\n");
    {
        RETAIN_BEGIN(VikiStore, &sA, g);
        emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zModel = "stub-v1";
        /* CONTROL FIRST: with no embedder, a query sharing no word with the
        ** target cannot find it. If this passes, E2 below proves nothing. */
        check(viki_ask("boundary fastener hinge", 5, &h)==VIKI_OK
              && (h->n==0 || !strstr(h->a[0].zText,"gate latch")),
              "Q1 CONTROL: without vectors, a no-shared-word query MISSES",
              "it found it anyway -- Q3 would be vacuous");
        viki_hits_free(h); h=0;

        {
            RETAIN_BEGIN(VikiEmbed, &emb, ge);
            viki_reindex(0, &n);
            check(nOf(VIKI_N_VECTOR,0)>0,
                  "Q2 reindex at an embed model stored vectors", "no vectors");
            rank_gate = -1;
            if( viki_ask("boundary fastener hinge", 5, &h)==VIKI_OK ){
                for(i=0;i<h->n;i++)
                    if( strstr(h->a[i].zText,"gate latch") ){ rank_gate = i; break; }
            }
            check(rank_gate==0,
                  "Q3 the vector leg finds a chunk sharing NO WORD with the query",
                  "not at rank 1");
            check(h && h->bDegraded==0,
                  "Q4 with an embedder retained, hits are NOT degraded", "still degraded");
            viki_hits_free(h); h=0;
            RETAIN_END(ge);
        }
        RETAIN_END(g);
    }

    printf("\n== G: the audit's findings, as standing assertions ==\n");
    {
        /* Every assertion here corresponds to a defect an adversarial audit
        ** found in code the 20 above were already green against. They are
        ** written to fail on the ORIGINAL bug, not merely to exercise the
        ** fixed line. */
        sqlite3 *dbG = 0; VikiStore sG;
        sqlite3_open(":memory:", &dbG); sG.db = dbG; viki_attach(dbG);
        emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zModel = "stub-v1";
        {
            RETAIN_BEGIN(VikiStore, &sG, g);
            /* ORDER IS THE WHOLE POINT: index ONLY at the embed epoch. The
            ** original probe reindexed degraded first, so epoch '' chunks
            ** existed and hid this entirely. */
            {
                RETAIN_BEGIN(VikiEmbed, &emb, ge);
                viki_note("the gate latch sticks below freezing");
                viki_reindex(0, &n);
                RETAIN_END(ge);
            }
            check(viki_ask("gate latch freezing", 5, &h)==VIKI_OK && h && h->n>0,
                  "G1 degraded ask still answers over a corpus indexed WITH an embedder",
                  "zero hits -- the required path returned 'nothing is known'");
            viki_hits_free(h); h=0;

            {   /* identity must cover POSITION, not just content */
                VikiNote a, b;
                int before = nOf(VIKI_N_ASSERT,0);
                memset(&a,0,sizeof a); a.vftbl=&vikiNoteVftbl;
                a.zText="same words"; a.zKey="k1"; a.zTs="2026-01-01T00:00:00Z";
                memset(&b,0,sizeof b); b.vftbl=&vikiNoteVftbl;
                b.zText="same words"; b.zKey="k2"; b.zTs="2026-01-01T00:00:00Z";
                viki_put((VikiAssert*)&a); viki_put((VikiAssert*)&b);
                check(nOf(VIKI_N_ASSERT,0)==before+2
                      && strcmp(a.zId,b.zId)!=0,
                      "G2 same text under a DIFFERENT key is a different assertion",
                      "the second write vanished into the first");
            }

            {   /* a vtable slot returning NULL must fail loudly, not store
                ** nothing and report success -- INSERT OR IGNORE suppresses a
                ** NOT NULL violation exactly as happily as a duplicate key. */
                VikiNote bad;
                memset(&bad,0,sizeof bad); bad.vftbl=&nullKeyVftbl;
                bad.zText="has no key"; bad.zTs="2026-01-01T00:00:00Z";
                check(viki_put((VikiAssert*)&bad)!=VIKI_OK,
                      "G2b a vtable slot returning NULL is refused, not silently dropped",
                      "returned OK having written no row");
            }

            {   /* core must never commit the caller's transaction */
                int after;
                viki_sql("BEGIN", 0, 0);
                viki_note("written inside the CALLER's transaction");
                viki_reindex(0, &n);                 /* used to COMMIT it */
                viki_sql("ROLLBACK", 0, 0);
                after = sqlN("SELECT count(*) FROM viki_assert WHERE body LIKE '%CALLER%'");
                check(after==0,
                      "G3 core does not commit the caller's open transaction",
                      "the rollback could not undo it -- core had committed");
            }

            {   /* the raw rung must report a step-time error */
                VikiStatus rc2 = viki_sql("SELECT abs(-9223372036854775807-1)", 0, 0);
                check(rc2!=VIKI_OK,
                      "G4 viki_sql reports an error raised at STEP time", "returned OK");
            }
            {   /* ...and must run every statement it was given */
                viki_sql("CREATE TABLE g5a(x); CREATE TABLE g5b(x);", 0, 0);
                check(sqlN("SELECT count(*) FROM sqlite_master WHERE name IN ('g5a','g5b')")==2,
                      "G5 viki_sql runs EVERY statement, not just the first",
                      "the tail was dropped");
            }
            check(viki_get("nope", 0)==VIKI_ENOTFOUND,
                  "G6 viki_get(id, NULL) is legal -- an existence check", "it crashed or errored");
            {   /* a reserved/empty epoch must be refused, not silently no-op */
                VikiEmbed bad = { stubEmbed, 0, DIM, "" };
                RETAIN_BEGIN(VikiEmbed, &bad, gb);
                check(viki_reindex(0, &n)==VIKI_EINVAL,
                      "G7 an empty zModel is refused (it collides with 'unembedded')",
                      "accepted, and reindex became a permanent no-op");
                RETAIN_END(gb);
            }
            {   /* merging into a store that was never attached must fail */
                sqlite3 *dbRaw = 0; VikiStore sRaw;
                sqlite3_open(":memory:", &dbRaw); sRaw.db = dbRaw;
                {
                    RETAIN_BEGIN(VikiStore, &sRaw, gr);
                    check(viki_merge(dbG, &n)!=VIKI_OK,
                          "G8 a merge that cannot complete is NOT reported as success",
                          "returned OK with a truncated union");
                    RETAIN_END(gr);
                }
                sqlite3_close(dbRaw);
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbG);
    }

    printf("\n== E: encryption at rest ==\n");
    {
        /* A DIARY HOLDS WHAT SOMEONE TOLD IT IN CONFIDENCE, so encryption is
        ** the baseline rather than an option. These assertions are the
        ** predecessor's E-series, ported: the point is never "we called
        ** PRAGMA key and it returned OK" -- SQLCipher-less builds do that
        ** happily -- but that the bytes on disk are really ciphertext, proved
        ** against a plaintext control written by the SAME binary. */
        sqlite3 *dbE=0; VikiStore sE;
        int bCipher = 0;
        sqlite3_open(":memory:",&dbE);
        {   sqlite3_stmt *q=0;
            if( sqlite3_prepare_v2(dbE,"PRAGMA cipher_version",-1,&q,0)==SQLITE_OK
             && sqlite3_step(q)==SQLITE_ROW
             && sqlite3_column_text(q,0) ) bCipher = 1;
            sqlite3_finalize(q); }
        sqlite3_close(dbE);
        if( !bCipher ){
            printf("  --    E1-E5 skipped: built against stock SQLite, NOT SQLCipher\n");
            printf("  --    (a skip here is not a pass -- the diary would be plaintext)\n");
        }else{
            const char *zKey = "PRAGMA key = \"x'2b7e151628aed2a6abf7158809cf4f3c"
                               "762e7160f38b4da56a784d9045190cfe'\"";
            const char *zEnc = "/tmp/vk_probe_enc.db", *zPln = "/tmp/vk_probe_pln.db";
            char aHdr[16]; FILE *f; int bLeakE=0, bLeakP=0;
            unlink(zEnc); unlink(zPln);
            sqlite3_open(zEnc,&dbE);
            check(sqlite3_exec(dbE,zKey,0,0,0)==SQLITE_OK, "E0 PRAGMA key is accepted", "rejected");
            sE.db=dbE; viki_attach(dbE);
            { RETAIN_BEGIN(VikiStore,&sE,g); viki_note("the gate latch sticks below freezing"); RETAIN_END(g); }
            sqlite3_close(dbE);
            /* the SAME binary and the SAME content, unkeyed */
            sqlite3_open(zPln,&dbE); sE.db=dbE; viki_attach(dbE);
            { RETAIN_BEGIN(VikiStore,&sE,g); viki_note("the gate latch sticks below freezing"); RETAIN_END(g); }
            sqlite3_close(dbE);

            f=fopen(zEnc,"rb"); memset(aHdr,0,sizeof aHdr);
            if(f){ if(fread(aHdr,1,15,f)!=15){} fclose(f); }
            check(memcmp(aHdr,"SQLite format 3",15)!=0,
                  "E1 the keyed diary does NOT begin 'SQLite format 3'", "it is plaintext");
            f=fopen(zPln,"rb"); memset(aHdr,0,sizeof aHdr);
            if(f){ if(fread(aHdr,1,15,f)!=15){} fclose(f); }
            check(memcmp(aHdr,"SQLite format 3",15)==0,
                  "E2 CONTROL: the unkeyed one DOES (so E1 can fail)", "control is wrong");

            {   /* the note itself must not be readable in the file */
                char *zBuf; long n2; size_t rd;
                f=fopen(zEnc,"rb"); fseek(f,0,SEEK_END); n2=ftell(f); fseek(f,0,SEEK_SET);
                zBuf=(char*)malloc((size_t)n2+1); rd=fread(zBuf,1,(size_t)n2,f); fclose(f);
                zBuf[rd]=0;
                { size_t k; for(k=0;k+10<rd;k++) if(!memcmp(zBuf+k,"gate latch",10)){ bLeakE=1; break; } }
                free(zBuf);
                f=fopen(zPln,"rb"); fseek(f,0,SEEK_END); n2=ftell(f); fseek(f,0,SEEK_SET);
                zBuf=(char*)malloc((size_t)n2+1); rd=fread(zBuf,1,(size_t)n2,f); fclose(f);
                zBuf[rd]=0;
                { size_t k; for(k=0;k+10<rd;k++) if(!memcmp(zBuf+k,"gate latch",10)){ bLeakP=1; break; } }
                free(zBuf);
            }
            check(!bLeakE, "E3 the note does not appear in the keyed file", "plaintext on disk");
            check(bLeakP,  "E3b CONTROL: it DOES in the unkeyed one (so E3 can fail)", "control is wrong");

            {   /* a wrong key must fail, non-interactively */
                sqlite3 *dbW2=0; int rcW;
                sqlite3_open(zEnc,&dbW2);
                sqlite3_exec(dbW2,"PRAGMA key = \"x'00112233445566778899aabbccddeeff"
                                  "00112233445566778899aabbccddeeff'\"",0,0,0);
                /* sqlite_master, not a core table: a wrong key fails to
                ** decrypt the FILE, so any read fails -- and reaching for a
                ** viki table here would have been the probe naming core's
                ** schema to test something that is purely the host's. */
                rcW = sqlite3_exec(dbW2,"SELECT count(*) FROM sqlite_master",0,0,0);
                sqlite3_close(dbW2);
                check(rcW!=SQLITE_OK, "E4 a WRONG key is rejected", "it opened anyway");
            }
            unlink(zEnc); unlink(zPln);
        }
    }

    printf("\n== I: identity and signing ==\n");
    {
        sqlite3 *dbI=0, *dbP=0; VikiStore sI, sP;
        Key kMe   = { "esiwed", "dewise", 0, 0 };   /* pub is priv reversed */
        Key kAgent= { "tnega",  "agent",  0, 0 };
        VikiIdentity idMe, idAgent;
        char idMeAssert[VIKI_ID_HEX+1], idAgentAssert[VIKI_ID_HEX+1];
        char idNote[VIKI_ID_HEX+1], zWho[VIKI_ID_HEX+1];
        VikiSigState st2;
        sqlite3_open(":memory:",&dbI); sI.db=dbI; viki_attach(dbI);
        {
            RETAIN_BEGIN(VikiStore, &sI, g);
            /* AN IDENTITY IS AN ASSERTION IN YOUR OWN DIARY. The agent's is a
            ** row you wrote, so its authority traces to something you can
            ** read -- which is what makes an agent's actions attributable
            ** without a second system. */
            viki_identity_put("warren (laptop, secure enclave)", kMe.zPub, idMeAssert);
            viki_identity_put("claude (agent, key I issued)",    kAgent.zPub, idAgentAssert);
            check(nOf(VIKI_N_ASSERT,"identity")==2,
                  "I1 identities are ordinary assertions in the diary", "not stored");

            /* unsigned is a WORKING path, not a failure */
            viki_noteid("written with no identity retained", idNote);
            viki_signed(idNote, &st2, zWho);
            check(st2==VIKI_SIG_NONE,
                  "I2 with no identity retained, writes are UNSIGNED and fine", "wrong state");

            idMe.zSigner=idMeAssert; idMe.pApp=&kMe;
            idMe.xSign=keySign; idMe.xVerify=keyVerify;
            idAgent.zSigner=idAgentAssert; idAgent.pApp=&kAgent;
            idAgent.xSign=keySign; idAgent.xVerify=keyVerify;

            {   RETAIN_BEGIN(VikiIdentity, &idMe, gi);
                viki_noteid("the gate latch sticks below freezing", idNote);
                viki_signed(idNote, &st2, zWho);
                check(st2==VIKI_SIG_OK && strcmp(zWho, idMeAssert)==0,
                      "I3 a retained identity signs what it writes, and verifies",
                      "not verified");
                RETAIN_END(gi); }

            /* S5, THE ONE THAT MATTERS: another identity's key must NOT
            ** verify, or the signature proves nothing about WHO. */
            {   Key kImposter = { "esiwed", "wrongkey", 0, 0 };
                VikiIdentity bad = idMe;
                char idFake[VIKI_ID_HEX+1];
                bad.pApp = &kImposter;
                RETAIN_BEGIN(VikiIdentity, &bad, gb);
                viki_noteid("a statement signed with the wrong key", idFake);
                viki_signed(idFake, &st2, zWho);
                check(st2==VIKI_SIG_BAD,
                      "I4 CONTROL: a signature from another key does NOT verify",
                      "an imposter passed -- the signature proves nothing about WHO");
                RETAIN_END(gb); }

            /* declining is not an error: a cancelled thumbprint stores the
            ** assertion unsigned rather than losing it */
            {   Key kLocked = { "esiwed", "dewise", 0, 1 };
                VikiIdentity dec = idMe;
                char idDec[VIKI_ID_HEX+1];
                dec.pApp = &kLocked;
                RETAIN_BEGIN(VikiIdentity, &dec, gd);
                check(viki_noteid("written while the keychain was locked", idDec)==VIKI_OK,
                      "I5 a DECLINED signature still stores the assertion", "the write was lost");
                viki_signed(idDec, &st2, zWho);
                check(st2==VIKI_SIG_NONE, "I5b ...unsigned, and honestly reported as such",
                      "it claimed a signature");
                RETAIN_END(gd); }

            /* countersigning: several identities on one statement */
            {   RETAIN_BEGIN(VikiIdentity, &idAgent, ga);
                viki_countersign(idNote);
                RETAIN_END(ga); }
            check(sqlN("SELECT count(*) FROM viki_sig WHERE id IS NOT NULL")>=2,
                  "I6 several identities can sign ONE assertion", "countersign did not add a row");
            RETAIN_END(g);
        }

        /* S3: VERIFICATION NEEDS NO SECRET. A fresh peer merges the diary and
        ** can establish who said what while holding no private material at
        ** all -- it never retains a signer, only a verifier. */
        sqlite3_open(":memory:",&dbP); sP.db=dbP; viki_attach(dbP);
        {
            VikiIdentity vOnly;
            memset(&vOnly, 0, sizeof vOnly);
            vOnly.xVerify = keyVerify;      /* no xSign, no zSigner, no key */
            RETAIN_BEGIN(VikiStore, &sP, gp);
            viki_merge(dbI, &n);
            check(n>0, "I7 a peer merges the diary", "nothing merged");
            RETAIN_BEGIN(VikiIdentity, &vOnly, gv);
            viki_signed(idNote, &st2, zWho);
            check(st2==VIKI_SIG_OK,
                  "I8 that peer verifies WHO said it, holding NO private key",
                  "signatures did not survive the merge, or verify needs a secret");
            RETAIN_END(gv);
            RETAIN_END(gp);
        }
        /* CONTROL: a peer that has the signature but NOT the signer's identity
        ** must say UNKNOWN, not OK and not BAD -- that is a fact about its
        ** coverage, and it is what a peer sees before the identity reaches it. */
        {
            sqlite3 *dbQ=0; VikiStore sQ; VikiIdentity vOnly;
            memset(&vOnly, 0, sizeof vOnly); vOnly.xVerify = keyVerify;
            sqlite3_open(":memory:",&dbQ); sQ.db=dbQ; viki_attach(dbQ);
            RETAIN_BEGIN(VikiStore, &sQ, gq);
            viki_sql("SELECT 1", 0, 0);
            { sqlite3_stmt *w=0;
              viki_sql("INSERT INTO viki_assert(id,kind,akey,arank,ts,body,atext)"
                       " VALUES('x','note','k','r','t','b','b')", 0, 0);
              (void)w; }
            viki_sql("INSERT INTO viki_sig(id,signer,sig) VALUES('x','nobody',x'00')", 0, 0);
            RETAIN_BEGIN(VikiIdentity, &vOnly, gv2);
            viki_signed("x", &st2, zWho);
            check(st2==VIKI_SIG_UNKNOWN,
                  "I9 CONTROL: a signature from an identity this diary lacks is UNKNOWN",
                  "it was reported as OK or BAD rather than as missing coverage");
            RETAIN_END(gv2);
            RETAIN_END(gq);
            sqlite3_close(dbQ);
        }
        sqlite3_close(dbI); sqlite3_close(dbP);
    }

    printf("\n== B: blobs -- the text is not the bytes ==\n");
    {
        sqlite3 *dbB2=0; VikiStore sB2;
        unsigned char aPayload[8192];
        char idB[VIKI_ID_HEX+1];
        const void *pOut=0; sqlite3_int64 nOut=0;
        int i2, nRange=0;
        for(i2=0;i2<8192;i2++) aPayload[i2] = (unsigned char)(i2*167 + (i2>>3));
        sqlite3_open(":memory:",&dbB2); sB2.db=dbB2; viki_attach(dbB2);
        {
            RETAIN_BEGIN(VikiStore, &sB2, g);
            check(viki_blob_put(
                    "all-MiniLM-L6-v2 sentence embedding model, ONNX, int8 "
                    "quantised for arm64, 384 dimensions",
                    "e3b0c44298fc1c149afbf4c8996fb924", aPayload, sizeof aPayload,
                    idB)==VIKI_OK,
                  "B1 a blob stores with a description and a content hash", viki_errmsg());
            check(viki_blob_get(idB, &pOut, &nOut)==VIKI_OK
                  && nOut==(sqlite3_int64)sizeof aPayload
                  && memcmp(pOut, aPayload, sizeof aPayload)==0,
                  "B2 the payload reads back BYTE-IDENTICAL", "the bytes changed");

            viki_reindex(0, &n);
            viki_count(VIKI_N_RANGE, 0, &nRange);
            check(nRange==1, "B3 ONE range over the blob, not many", "wrong range count");

            /* THE POINT. Chunking 23 MB of int8 coefficients would produce
            ** ranges of noise and a vector that means nothing. What is worth
            ** embedding is the DESCRIPTION -- so text() and canon() are
            ** different slots, and a blob is what makes that load-bearing. */
            check(viki_ask("which embedding model do I have", 3, &h)==VIKI_OK
                  && h && h->n>0 && strstr(h->a[0].zText, "MiniLM"),
                  "B4 the blob is found by its DESCRIPTION",
                  (h&&h->n)?h->a[0].zText:"nothing");
            check(h && h->n>0 && !strstr(h->a[0].zText, "\xa7"),
                  "B4b CONTROL: the range holds the description, not the coefficients",
                  "binary leaked into the indexed text");
            viki_hits_free(h); h=0;

            /* The payload lives OUT of viki_assert, so a 23 MB model does not
            ** sit in the row that every resolve, count and merge scans. */
            check(sqlN("SELECT max(length(body)) FROM viki_assert") < 512,
                  "B5 the payload is NOT in the assertion row", "the bytes are in viki_assert");

            /* AN IMAGE IS THE SAME THING, and this is why it matters that
            ** the description goes through the ORDINARY chunk path rather
            ** than a side table: it lands in viki_chunk_text, so viki_fts
            ** indexes it, so the PRIMITIVE legs find it. A photo is findable
            ** with no embedder loaded at all -- which is the case a phone,
            ** a fresh clone, or a degraded run is actually in. */
            {
                unsigned char aPng[64];
                char idImg[VIKI_ID_HEX+1];
                int i3;
                for(i3=0;i3<64;i3++) aPng[i3] = (unsigned char)(i3*13);
                aPng[0]=0x89; aPng[1]='P'; aPng[2]='N'; aPng[3]='G';
                viki_blob_put("photograph of the north gate latch, rusted through, "
                              "taken after the January freeze",
                              "aa11bb22", aPng, sizeof aPng, idImg);
                viki_reindex(0, &n);
                /* NO VikiEmbed retained here -- keyword and literal only. */
                check(viki_ask("rusted latch photograph", 5, &h)==VIKI_OK
                      && h && h->n>0 && strstr(h->a[0].zText,"north gate"),
                      "B7 an image blob is found by the KEYWORD leg, no embedder",
                      (h&&h->n)?h->a[0].zText:"nothing");
                check(h && h->bDegraded==1,
                      "B7b CONTROL: ...and that search really was degraded",
                      "an embedder was retained, so B7 proves less");
                viki_hits_free(h); h=0;
                check(viki_ask("aPng binary 0x89", 5, &h)==VIKI_OK
                      && (h->n==0 || !strstr(h->a[0].zText,"\x89")),
                      "B7c CONTROL: the PAYLOAD is not searchable, only the description",
                      "binary reached the index");
                viki_hits_free(h); h=0;
            }

            /* Identity is (content hash, description): the same bytes
            ** described differently are two claims about one payload. */
            {
                /* Baseline captured rather than a literal: this section grew
                 * an image blob above it once already, and a magic number
                 * would have made B6 fail for a reason that was not its
                 * subject. */
                char id2[VIKI_ID_HEX+1];
                int nBefore = nOf(VIKI_N_ASSERT,"blob");
                viki_blob_put("a different description of the same file",
                              "e3b0c44298fc1c149afbf4c8996fb924",
                              aPayload, sizeof aPayload, id2);
                check(strcmp(id2, idB)!=0 && nOf(VIKI_N_ASSERT,"blob")==nBefore+1,
                      "B6 the same bytes under a different description are a second assertion",
                      "they collided");
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbB2);
    }

    printf("\n== N: ONE model, its own database, MANY tribes ==\n");
    {
        /* THE MODEL IS NOT PART OF A STORE. D-11 pins one model universal
        ** across peers, so a copy per tribe is N copies of a byte-identical
        ** artifact. Keeping it in its own database and retaining the embedder
        ** OUTSIDE the stores is the shape that follows -- and it needs no
        ** change to core, because VikiEmbed and VikiStore are separate retain
        ** stacks with independent lifetimes. */
        sqlite3 *dbM=0, *dbX=0, *dbY=0;
        VikiStore sM, sX, sY;
        int a=0, b=0;
        sqlite3_open(":memory:",&dbM); sM.db=dbM;
        sqlite3_open(":memory:",&dbX); sX.db=dbX; viki_attach(dbX);
        sqlite3_open(":memory:",&dbY); sY.db=dbY; viki_attach(dbY);
        emb.xEmbed=stubEmbed; emb.pApp=0; emb.nDim=DIM; emb.zModel="shared-v1";
        {
            RETAIN_BEGIN(VikiEmbed, &emb, ge);      /* ONE model, outermost */
            { RETAIN_BEGIN(VikiStore, &sX, g1);
              viki_note("the gate latch sticks below freezing");
              viki_reindex(0,&n); viki_count(VIKI_N_VECTOR,0,&a);
              RETAIN_END(g1); }
            { RETAIN_BEGIN(VikiStore, &sY, g2);
              viki_note("the invoice payment is late");
              viki_reindex(0,&n); viki_count(VIKI_N_VECTOR,0,&b);
              RETAIN_END(g2); }
            check(a>0 && b>0,
                  "N1 one retained embedder serves SEVERAL stores -- no duplication",
                  "a tribe went unembedded");
            RETAIN_END(ge);
        }
        /* CONTROL: outside that scope the same stores degrade, which is what
        ** proves M1 was the retained embedder and not something ambient. */
        { RETAIN_BEGIN(VikiStore, &sX, g3);
          viki_note("a second note, with no model retained");
          viki_reindex(0,&n);
          check(nOf(VIKI_N_MODEL,0)==1,
                "N1b CONTROL: with the embedder out of scope, new ranges carry no model",
                "it embedded anyway");
          RETAIN_END(g3); }

        /* The model itself is a BLOB ASSERTION -- the same machinery the
        ** B series covers -- so the probe needs no model table of its own.
        ** Before blobs existed this section hand-rolled one, which was the
        ** probe reaching past the API for something the API should have
        ** provided. C8 is what noticed. */
        {
            unsigned char aIn[1024]; int i2; char idM[VIKI_ID_HEX+1];
            const void *pOut=0; sqlite3_int64 nOut=0;
            for(i2=0;i2<1024;i2++) aIn[i2] = (unsigned char)(i2*31);
            viki_attach(dbM);
            { RETAIN_BEGIN(VikiStore, &sM, gm);
              check(viki_blob_put("the pinned embedding model, as a blob",
                                  "deadbeef", aIn, sizeof aIn, idM)==VIKI_OK
                 && viki_blob_get(idM,&pOut,&nOut)==VIKI_OK
                 && nOut==1024 && memcmp(pOut,aIn,1024)==0,
                    "N2 the model is an ordinary blob assertion in its own diary",
                    "round trip failed");
              RETAIN_END(gm); }
        }
        sqlite3_close(dbM); sqlite3_close(dbX); sqlite3_close(dbY);
    }

    printf("\n== O: observability -- writes only, on commit ==\n");
    {
        sqlite3 *dbO = 0; VikiStore sO; Seen seen; int tok = 0;
        char idO[VIKI_ID_HEX+1];
        memset(&seen, 0, sizeof seen);
        sqlite3_open(":memory:", &dbO); sO.db = dbO; viki_attach(dbO);
        check(viki_watch(dbO, onEvent, &seen, &tok)==VIKI_OK && tok>0,
              "O1 a listener can be registered", viki_errmsg());
        {
            RETAIN_BEGIN(VikiStore, &sO, g);
            viki_noteid("the first thing worth remembering", idO);
            check(seen.nPut==1 && strcmp(seen.zLastId, idO)==0,
                  "O2 a write notifies, and names the assertion", "no event");

            {   /* supersession is its OWN event, not a second put */
                VikiNote a; memset(&a,0,sizeof a); a.vftbl=&vikiNoteVftbl;
                a.zText="the corrected version"; a.zKey="k"; a.zTs="2026-01-02T00:00:00Z";
                a.zSupersedes = idO;
                viki_put((VikiAssert*)&a);
                check(seen.nSup==1, "O3 a supersession is a distinct event", "not distinguished");
            }
            viki_reindex(0, &n);
            check(seen.nProject==1, "O4 a projection pass notifies once, with a count", "no event");

            /* NOTHING FIRES FOR A READ. A read can be an arbitrary join, and
            ** there is no change to announce -- so the event set is
            ** write-only by construction, not by omission. */
            {
                int before = seen.nPut + seen.nSup + seen.nForget
                           + seen.nMerge + seen.nProject;
                viki_ask("remembering", 3, &h); viki_hits_free(h); h=0;
                { Bodies b = gather(0,0); bodies_free(&b); }
                nOf(VIKI_N_ASSERT,0);
                check(before == seen.nPut + seen.nSup + seen.nForget
                              + seen.nMerge + seen.nProject,
                      "O5 CONTROL: reads fire NOTHING -- the event set is write-only",
                      "a read produced an event");
            }

            /* ROLLED BACK WORK MUST NOT BE ANNOUNCED. A listener that wrote to
            ** a UI or told a peer cannot take it back, so events are buffered
            ** and discarded when the host rolls back. */
            {
                int before = seen.nPut;
                viki_sql("BEGIN", 0, 0);
                viki_note("this will be rolled back");
                viki_sql("ROLLBACK", 0, 0);
                check(seen.nPut==before,
                      "O6 a change the HOST rolled back is never announced",
                      "the listener was told about work that did not happen");
                { Bodies b = gather(0,0);
                  check(b.n>0, "O6b CONTROL: the store still has its earlier assertions",
                        "the rollback took everything"); bodies_free(&b); }
            }

            check(viki_forget(idO)==VIKI_OK && seen.nForget==1,
                  "O7 a withdrawal notifies", "no event");

            check(viki_unwatch(dbO, tok)==VIKI_OK, "O8 a listener can be removed", "failed");
            {
                int before = seen.nPut;
                viki_note("after unwatch");
                check(seen.nPut==before, "O8b CONTROL: an unwatched listener stops hearing",
                      "still receiving");
            }

            {   /* a listener that writes is refused, not deadlocked */
                Seen s2; int t2 = 0;
                memset(&s2, 0, sizeof s2);
                viki_watch(dbO, onEventMutating, &s2, &t2);
                viki_note("this triggers the mutating listener");
                check(s2.bMutateRefused==1,
                      "O9 a listener that tries to WRITE is refused with EBUSY",
                      "it was allowed to reenter");
                viki_unwatch(dbO, t2);
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbO);
    }

    printf("\n== V: ranges, and two chunkings over one blob ==\n");
    {
        sqlite3 *dbV = 0; VikiStore sV;
        VikiChunking fine  = { "l2o0",  2, 0 };
        VikiChunking coarse= { "l8o2",  8, 2 };
        char idV[VIKI_ID_HEX+1];
        sqlite3_open(":memory:", &dbV); sV.db = dbV; viki_attach(dbV);
        emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zModel = "stub-v1";
        {
            RETAIN_BEGIN(VikiStore, &sV, g);
            /* the gate topic and the invoice topic are SIX LINES APART, so a
            ** 2-line chunking can never put them in one range and an 8-line
            ** one always does */
            viki_noteid(
              "the gate latch sticks\n"
              "filler line two\n"
              "filler line three\n"
              "filler line four\n"
              "filler line five\n"
              "the invoice payment is late\n"
              "filler line seven\n"
              "filler line eight\n", idV);
            {
                RETAIN_BEGIN(VikiEmbed, &emb, ge);
                viki_reindex(&fine, &n);
                check(n>0, "V1 a fine chunking produced ranges", "none");
                /* THE CONTROL COMES FIRST. With only the 2-line chunking, no
                ** range can hold both topics -- they are six lines apart. If
                ** this finds one, V4 below proves nothing about coarse ranges. */
                rank_gate = -1;
                if( viki_ask("boundary fastener invoice billing", 5, &h)==VIKI_OK ){
                    for(i=0;i<h->n;i++)
                        if( strstr(h->a[i].zText,"gate latch")
                         && strstr(h->a[i].zText,"invoice payment") ){ rank_gate = i; break; }
                }
                check(rank_gate<0,
                      "V3b CONTROL: with only the FINE chunking, no range holds both topics",
                      "it found one -- V4 would be vacuous");
                viki_hits_free(h); h=0;

                {   /* THE COLLISION THE PREDECESSOR HAD IS UNREPRESENTABLE.
                    ** Two policies over the same assertion at the same model
                    ** used to agree on the key (content_hash, model, ordinal)
                    ** and disagree on the text. The extent is in the key now. */
                    int before = nOf(VIKI_N_RANGE,0);
                    viki_reindex(&coarse, &n);
                    check(nOf(VIKI_N_RANGE,0) > before,
                          "V2 a SECOND chunking adds ranges without disturbing the first",
                          "the second policy collided with the first");
                    check(nOf(VIKI_N_CHUNKING,0)==2,
                          "V3 both chunkings coexist over one assertion", "only one survived");
                }
                /* THE REASON FOR TWO CHUNKINGS, and V5 is its control. */
                rank_gate = -1;
                if( viki_ask("boundary fastener invoice billing", 5, &h)==VIKI_OK ){
                    for(i=0;i<h->n;i++)
                        if( strstr(h->a[i].zText,"gate latch")
                         && strstr(h->a[i].zText,"invoice payment") ){ rank_gate = i; break; }
                }
                check(rank_gate>=0,
                      "V4 a query spanning two topics finds the COARSE range holding both",
                      "no hit contained both halves");
                viki_hits_free(h); h=0;
                RETAIN_END(ge);
            }
            check(sqlN("SELECT count(*) FROM viki_chunk c JOIN viki_chunk d"
                            " ON c.id=d.id AND c.lo=d.lo AND c.hi=d.hi AND c.model=d.model"
                            " AND c.seq<>d.seq")==0,
                  "V5 CONTROL: no two rows share (id, lo, hi, model) -- the key is the extent",
                  "a duplicate range exists");
            {   /* THE TEXT EXISTS EXACTLY ONCE. viki_chunk has no text column
                ** at all; a chunk's text is computed by substr over the
                ** assertion, which is what makes overlap free. */
                int bHasText = sqlN("SELECT count(*) FROM pragma_table_info('viki_chunk')"
                                    " WHERE name='text'");
                check(bHasText==0, "V6 viki_chunk stores NO text -- ranges only", "a text column exists");
                check(sqlN("SELECT count(*) FROM viki_chunk_text WHERE text<>''")>0,
                      "V6b CONTROL: the text is still reachable, computed from the range",
                      "the view returns nothing");
            }
            {   /* FTS can regenerate itself from ranges alone */
                check(viki_sql("INSERT INTO viki_fts(viki_fts) VALUES('rebuild')", 0, 0)==VIKI_OK,
                      "V7 the FTS index rebuilds from the ranges, with no stored text",
                      viki_errmsg());
                check(viki_ask("gate latch", 3, &h)==VIKI_OK && h && h->n>0,
                      "V7b ...and still answers afterwards", "empty after rebuild");
                viki_hits_free(h); h=0;
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbV);
    }

    printf("\n== W: withdrawal, and the delete ORDER ==\n");
    {
        sqlite3 *dbW = 0; VikiStore sW; char idW[VIKI_ID_HEX+1];
        sqlite3_open(":memory:", &dbW); sW.db = dbW; viki_attach(dbW);
        {
            RETAIN_BEGIN(VikiStore, &sW, g);
            viki_noteid("the passphrase is hunter2 and should never have been typed", idW);
            viki_note("an unrelated note about fence posts");
            viki_reindex(0, &n);
            check(viki_ask("passphrase hunter2", 5, &h)==VIKI_OK && h && h->n>0,
                  "W1 the text is findable before forgetting", "not found");
            viki_hits_free(h); h=0;

            check(viki_forget(idW)==VIKI_OK, "W2 viki_forget removes the assertion", viki_errmsg());
            check(nOf(VIKI_N_ASSERT,0)==1
               && nOf(VIKI_N_RANGE,0)==1,
                  "W3 the assertion and its chunks are gone", "rows remain");

            check(viki_ask("hunter2", 5, &h)==VIKI_OK && h && h->n==0,
                  "W4 the withdrawn text is no longer returned by viki_ask", "it still answers");
            viki_hits_free(h); h=0;

            /* THE ASSERTION THAT MATTERS, and W4 is NOT it.
            **
            ** viki_ask() JOINs viki_chunk, so a stale FTS entry can never
            ** become a hit -- W4 stays green through exactly the bug it looks
            ** like it is testing. Measured, not assumed. FTS5's
            ** integrity-check does not catch it either: it PASSES over an
            ** index whose content row was deleted out from under it.
            **
            ** So assert the property directly, against viki_fts itself. This
            ** is idiom-independent: it holds whether the implementation uses
            ** the explicit-value 'delete' command (which needs no content row)
            ** or `DELETE FROM viki_fts WHERE rowid=?` (which does, and for
            ** which the delete ORDER becomes load-bearing). */
            check(sqlN("SELECT count(*) FROM viki_fts WHERE viki_fts MATCH 'hunter2'")==0,
                  "W4b the withdrawn text is gone from the FTS INDEX itself",
                  "viki_fts still matches it -- the withdrawal was cosmetic");

            check(viki_ask("fence posts", 5, &h)==VIKI_OK && h && h->n>0,
                  "W5 CONTROL: forgetting one assertion did not empty the index",
                  "everything vanished");
            viki_hits_free(h); h=0;
            check(viki_forget(idW)==VIKI_ENOTFOUND,
                  "W6 CONTROL: forgetting again reports NOT FOUND, not success", "returned OK");

            {   /* pruning a dead epoch leaves the assertions alone */
                int nDrop = 0, nBefore = nOf(VIKI_N_ASSERT,0);
                emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zModel = "old-v1";
                { RETAIN_BEGIN(VikiEmbed, &emb, ge); viki_reindex(0, &n); RETAIN_END(ge); }
                check(nOf(VIKI_N_RANGE,"old-v1")>0,
                      "W7 a second model's ranges exist", "none written");
                check(viki_prune_model("old-v1", &nDrop)==VIKI_OK && nDrop>0
                   && nOf(VIKI_N_RANGE,"old-v1")==0,
                      "W8 pruning a model drops its ranges", "chunks remain");
                check(nOf(VIKI_N_ASSERT,0)==nBefore,
                      "W9 CONTROL: pruning a projection does NOT touch the assertions",
                      "truth was deleted with the projection");
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbW);
    }

    printf("\n== K: calendar assertions, with SQLite as the parser ==\n");
    {
        sqlite3 *dbK = 0; VikiStore sK; int nAdd = 0;
        sqlite3_open(":memory:", &dbK); sK.db = dbK;
        viki_attach(dbK); viki_cal_attach(dbK);
        {
            RETAIN_BEGIN(VikiStore, &sK, g);
            check(viki_cal_ingest("this is not json at all", "probe", &nAdd)==VIKI_EINVAL,
                  "K1 CONTROL: invalid JSON is REFUSED, not read as an empty calendar",
                  "an HTML error page would read as a quiet day");
            check(viki_cal_ingest("{\"@type\":\"Event\",\"title\":\"no uid\"}", "probe", &nAdd)==VIKI_OK
                  && nAdd==0,
                  "K1b CONTROL: an object with no uid has no identity and is skipped",
                  "it was stored anyway");

            {   /* one jsCalendar Event, with an override */
                static const char zJs[] =
                  "{\"@type\":\"Event\",\"uid\":\"ev1@probe\",\"sequence\":0,"
                  "\"updated\":\"2026-08-01T12:00:00Z\","
                  "\"title\":\"Quarterly review\",\"description\":\"budget and headcount\","
                  "\"start\":\"2026-09-01T15:00:00\",\"timeZone\":\"America/Denver\","
                  "\"duration\":\"PT1H\",\"status\":\"confirmed\","
                  "\"recurrenceRules\":[{\"frequency\":\"weekly\"}],"
                  "\"recurrenceOverrides\":{\"2026-09-08T15:00:00\":{\"title\":\"moved to Boulder\"}}}";
                Cal c;
                check(viki_cal_ingest(zJs, "probe", &nAdd)==VIKI_OK && nAdd==2,
                      "K2 one Event plus its override become TWO assertions", "wrong count");
                c = calAll("", 0);
                check(strcmp(c.zTzid,"America/Denver")==0,
                      "K3 the IANA zone name is kept", c.zTzid);
                /* jsCalendar's `start` is LOCAL AS WRITTEN and `timeZone` is a
                ** NAME, so "never store an offset" is the format rather than a
                ** rule this code has to enforce. */
                { Cal all = calAll(0,0);
                  check(all.nOffset==0,
                        "K4 CONTROL: no UTC OFFSET is stored -- start is local, zone is a name",
                        "an offset leaked in"); }
                /* Scoped to the master: the override INHERITS the zone, which
                ** is correct (RFC 8984 SS 4.3 -- an override patches the
                ** recurrence, it does not restate it) and would make an
                ** unscoped count read 2. */
                { Cal m = calAll("",0), all = calAll(0,0);
                  check(m.nZoned==1 && all.nZoned==2,
                        "K5 timeZone present and not Etc/UTC means 'zoned' (and an override inherits it)",
                        "wrong form"); }
                /* the override is keyed by its recurrence id, which IS the
                ** (uid, recurrence-id) identity RFC 5546 resolution needs */
                c = calAll("2026-09-08T15:00:00", 0);
                check(c.n==1 && strcmp(c.zSummary,"moved to Boulder")==0,
                      "K6 a recurrenceOverride is its own assertion on its own key", c.zSummary);
            }

            {   /* RFC 5546 precedence through core's shared resolver */
                static const char zUpd[] =
                  "{\"@type\":\"Event\",\"uid\":\"ev1@probe\",\"sequence\":1,"
                  "\"updated\":\"2026-08-02T12:00:00Z\",\"title\":\"Quarterly review MOVED\","
                  "\"start\":\"2026-09-02T15:00:00\",\"timeZone\":\"America/Denver\"}";
                Cal c;
                viki_cal_ingest(zUpd, "probe", &nAdd);
                viki_cal_reproject(&n);
                /* viki_cal_events returns the RESOLVED tier, so the winner is
                ** the only master row it yields -- no rank query needed here. */
                c = calAll("", "ev1@probe");
                check(c.n==1 && strstr(c.zSummary,"MOVED")!=0,
                      "K7 a higher sequence wins -- RFC 5546, via core's ONE resolver", c.zSummary);
                check(nOf(VIKI_N_ASSERT,"event")==3 && nOf(VIKI_N_CURRENT,"event")==2,
                      "K8 CONTROL: 3 assertions stored, 2 resolved winners (master + override)",
                      "wrong counts");
            }

            {   /* an array, and a JMAP envelope, without the caller saying which */
                static const char zArr[] =
                  "[{\"@type\":\"Event\",\"uid\":\"a@p\",\"start\":\"2026-10-01T09:00:00\"},"
                  " {\"@type\":\"Event\",\"uid\":\"b@p\",\"start\":\"2026-10-02T09:00:00\"}]";
                static const char zEnv[] =
                  "{\"list\":[{\"@type\":\"Event\",\"uid\":\"c@p\",\"start\":\"2026-10-03T09:00:00\"}]}";
                viki_cal_ingest(zArr, "probe", &nAdd);
                check(nAdd==2, "K9 a bare JSON array of events is accepted", "wrong count");
                viki_cal_ingest(zEnv, "probe", &nAdd);
                check(nAdd==1, "K10 a JMAP-shaped {\"list\":[...]} envelope is accepted too", "wrong count");
            }

            {   /* the range-bound normalisation */
                Cal c; memset(&c,0,sizeof c);
                viki_cal_events("2026-10-01", "2026-10-02", calRow, &c);
                check(c.n==2, "K11 an ISO range finds events stored as ISO -- bounds normalised",
                      "wrong count");
            }
            {   /* a floating time has no zone and is NOT UTC */
                static const char zFl[] =
                  "{\"@type\":\"Event\",\"uid\":\"f@p\",\"start\":\"2026-11-01T09:00:00\"}";
                viki_cal_ingest(zFl, "probe", &nAdd);
                { Cal c = calAll(0,"f@p");
                  check(c.n==1 && c.nFloating==1,
                        "K12 no timeZone means FLOATING, which is not UTC", c.zForm); }
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbK);
    }

    printf("\n%d passed, %d failed\n", nPass, nFail);
    sqlite3_close(dbA); sqlite3_close(dbB);
    return nFail ? 1 : 0;
}
