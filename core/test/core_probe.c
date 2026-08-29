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

static int count(sqlite3 *db, const char *zSql){
    sqlite3_stmt *st = 0; int n = -1;
    if( sqlite3_prepare_v2(db, zSql, -1, &st, 0)==SQLITE_OK
     && sqlite3_step(st)==SQLITE_ROW ) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st); return n;
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
        check(count(dbA,"SELECT count(*) FROM viki_assert")==2,
              "A3 a note four frames down, through a callback, was stored", "count != 2");

        viki_noteid("identical bytes", id1);
        viki_noteid("identical bytes", id2);
        check(strcmp(id1,id2)==0 && count(dbA,"SELECT count(*) FROM viki_assert")==3,
              "A4 identity IS the content hash: same bytes, same id, one row", "differed");

        {   /* a nested store, then the outer restored */
            RETAIN_BEGIN(VikiStore, &sB, gB);
            viki_note("this belongs to store B");
            RETAIN_END(gB);
        }
        check(count(dbB,"SELECT count(*) FROM viki_assert")==1
           && count(dbA,"SELECT count(*) FROM viki_assert")==3,
              "A5 a nested store is separate, and the outer one is restored", "leaked");
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
            check(count(dbA,"SELECT count(*) FROM viki_assert WHERE akey='plan'")==3,
                  "S3 CONTROL: the superseded row is RETAINED, not deleted", "row vanished");
        }
        RETAIN_END(g);
    }

    printf("\n== R: retrieval ==\n");
    {
        RETAIN_BEGIN(VikiStore, &sA, g);
        viki_note("the veterinary appointment for the puppy is on Tuesday");
        viki_note("kernel pointer arithmetic in the linker is subtle");
        viki_reindex(&n);
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

    printf("\n== E: the vector leg ==\n");
    {
        RETAIN_BEGIN(VikiStore, &sA, g);
        emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zEpoch = "stub/c40o10";
        /* CONTROL FIRST: with no embedder, a query sharing no word with the
        ** target cannot find it. If this passes, E2 below proves nothing. */
        check(viki_ask("boundary fastener hinge", 5, &h)==VIKI_OK
              && (h->n==0 || !strstr(h->a[0].zText,"gate latch")),
              "E1 CONTROL: without vectors, a no-shared-word query MISSES",
              "it found it anyway -- E2 would be vacuous");
        viki_hits_free(h); h=0;

        {
            RETAIN_BEGIN(VikiEmbed, &emb, ge);
            viki_reindex(&n);
            check(count(dbA,"SELECT count(*) FROM viki_chunk WHERE vec IS NOT NULL")>0,
                  "E2 reindex at an embed epoch stored vectors", "no vectors");
            rank_gate = -1;
            if( viki_ask("boundary fastener hinge", 5, &h)==VIKI_OK ){
                for(i=0;i<h->n;i++)
                    if( strstr(h->a[i].zText,"gate latch") ){ rank_gate = i; break; }
            }
            check(rank_gate==0,
                  "E3 the vector leg finds a chunk sharing NO WORD with the query",
                  "not at rank 1");
            check(h && h->bDegraded==0,
                  "E4 with an embedder retained, hits are NOT degraded", "still degraded");
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
        emb.xEmbed = stubEmbed; emb.pApp = 0; emb.nDim = DIM; emb.zEpoch = "stub/c40o10";
        {
            RETAIN_BEGIN(VikiStore, &sG, g);
            /* ORDER IS THE WHOLE POINT: index ONLY at the embed epoch. The
            ** original probe reindexed degraded first, so epoch '' chunks
            ** existed and hid this entirely. */
            {
                RETAIN_BEGIN(VikiEmbed, &emb, ge);
                viki_note("the gate latch sticks below freezing");
                viki_reindex(&n);
                RETAIN_END(ge);
            }
            check(viki_ask("gate latch freezing", 5, &h)==VIKI_OK && h && h->n>0,
                  "G1 degraded ask still answers over a corpus indexed WITH an embedder",
                  "zero hits -- the required path returned 'nothing is known'");
            viki_hits_free(h); h=0;

            {   /* identity must cover POSITION, not just content */
                VikiNote a, b;
                int before = count(dbG,"SELECT count(*) FROM viki_assert");
                memset(&a,0,sizeof a); a.vftbl=&vikiNoteVftbl;
                a.zText="same words"; a.zKey="k1"; a.zTs="2026-01-01T00:00:00Z";
                memset(&b,0,sizeof b); b.vftbl=&vikiNoteVftbl;
                b.zText="same words"; b.zKey="k2"; b.zTs="2026-01-01T00:00:00Z";
                viki_put((VikiAssert*)&a); viki_put((VikiAssert*)&b);
                check(count(dbG,"SELECT count(*) FROM viki_assert")==before+2
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
                sqlite3_exec(dbG, "BEGIN", 0, 0, 0);
                viki_note("written inside the CALLER's transaction");
                viki_reindex(&n);                 /* used to COMMIT it */
                sqlite3_exec(dbG, "ROLLBACK", 0, 0, 0);
                after = count(dbG,
                  "SELECT count(*) FROM viki_assert WHERE body LIKE '%CALLER%'");
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
                check(count(dbG,"SELECT count(*) FROM sqlite_master WHERE name IN ('g5a','g5b')")==2,
                      "G5 viki_sql runs EVERY statement, not just the first",
                      "the tail was dropped");
            }
            check(viki_get("nope", 0)==VIKI_ENOTFOUND,
                  "G6 viki_get(id, NULL) is legal -- an existence check", "it crashed or errored");
            {   /* a reserved/empty epoch must be refused, not silently no-op */
                VikiEmbed bad = { stubEmbed, 0, DIM, "" };
                RETAIN_BEGIN(VikiEmbed, &bad, gb);
                check(viki_reindex(&n)==VIKI_EINVAL,
                      "G7 an empty zEpoch is refused (it collides with 'unembedded')",
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

    printf("\n== K: calendar assertions ==\n");
    {
        /* These are ported from the predecessor's cal-probe, and every one of
        ** them corresponds to a place an ICS parser is silently WRONG rather
        ** than loudly broken. */
        sqlite3 *dbK = 0; VikiStore sK; int nAdd = 0;
        sqlite3_open(":memory:", &dbK); sK.db = dbK;
        viki_attach(dbK); viki_cal_attach(dbK);
        {
            RETAIN_BEGIN(VikiStore, &sK, g);
            check(viki_cal_shred("not a calendar at all", 21, "probe", &nAdd)==VIKI_EINVAL,
                  "K1 CONTROL: input with no BEGIN:VCALENDAR is REFUSED",
                  "an HTML error page would read as a quiet day");

            {   /* fold, quoted-colon params, VALARM nesting, four time forms */
                static const char zIcs[] =
                  "BEGIN:VCALENDAR\r\n"
                  "BEGIN:VEVENT\r\n"
                  "UID:ev1@probe\r\n"
                  "DTSTAMP:20260801T120000Z\r\n"
                  "SEQUENCE:0\r\n"
                  "SUMMARY:Quarterly review with the whole tea\r\n m about budget\r\n"
                  "ORGANIZER;CN=\"Doe, J:R\":mailto:j@example.com\r\n"
                  "DTSTART;TZID=America/Denver:20260901T150000\r\n"
                  "DTEND;TZID=America/Denver:20260901T160000\r\n"
                  "BEGIN:VALARM\r\n"
                  "ACTION:DISPLAY\r\n"
                  "DURATION:-PT15M\r\n"
                  "END:VALARM\r\n"
                  "END:VEVENT\r\n"
                  "END:VCALENDAR\r\n";
                char *z = 0;
                viki_cal_shred(zIcs, sizeof(zIcs)-1, "probe", &nAdd);
                check(nAdd==1, "K2 one VEVENT shredded", "wrong count");
                viki_sql("SELECT summary FROM viki_event", 0, 0);
                z = 0;
                {   sqlite3_stmt *q=0;
                    sqlite3_prepare_v2(dbK,"SELECT summary FROM viki_event",-1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW) z=strdup((const char*)sqlite3_column_text(q,0));
                    sqlite3_finalize(q);
                }
                /* THE FOLD IS CRLF + SPACE AND BOTH GO. Keeping the space
                ** corrupts every long SUMMARY into a plausible string. */
                check(z && strcmp(z,"Quarterly review with the whole team about budget")==0,
                      "K3 a folded SUMMARY is rejoined with NO stray space", z?z:"none");
                free(z);
                {   sqlite3_stmt *q=0; char *o=0;
                    sqlite3_prepare_v2(dbK,"SELECT organizer FROM viki_event",-1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW) o=strdup((const char*)sqlite3_column_text(q,0));
                    sqlite3_finalize(q);
                    /* the colon ending the params is NOT the first colon */
                    check(o && strcmp(o,"mailto:j@example.com")==0,
                          "K4 a quoted CN containing a colon does not split the line", o?o:"none");
                    free(o);
                }
                {   sqlite3_stmt *q=0; char *d=0;
                    sqlite3_prepare_v2(dbK,"SELECT coalesce(duration,'') FROM viki_event",-1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW) d=strdup((const char*)sqlite3_column_text(q,0));
                    sqlite3_finalize(q);
                    /* a VALARM's DURATION is the ALARM's: without the nesting
                    ** skip a one-hour appointment reads as fifteen minutes */
                    check(d && d[0]==0,
                          "K5 a VALARM's DURATION does NOT become the meeting's", d?d:"none");
                    free(d);
                }
                {   sqlite3_stmt *q=0; char *f=0,*t=0;
                    sqlite3_prepare_v2(dbK,"SELECT dtstart_form, dtstart_tzid FROM viki_event",-1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW){
                        f=strdup((const char*)sqlite3_column_text(q,0));
                        t=strdup((const char*)sqlite3_column_text(q,1));
                    }
                    sqlite3_finalize(q);
                    check(f && strcmp(f,"zoned")==0 && t && strcmp(t,"America/Denver")==0,
                          "K6 a TZID time is 'zoned' and keeps its IANA name", f?f:"none");
                    free(f); free(t);
                }
                check(count(dbK,"SELECT count(*) FROM viki_event WHERE dtstart LIKE '%+%' OR dtstart LIKE '%-0%'")==0,
                      "K7 CONTROL: no UTC OFFSET is ever stored -- only the TZID", "an offset leaked in");
            }

            {   /* RFC 5546 precedence, through core's shared resolver */
                static const char zUpd[] =
                  "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
                  "UID:ev1@probe\r\nDTSTAMP:20260802T120000Z\r\nSEQUENCE:1\r\n"
                  "SUMMARY:Quarterly review MOVED\r\n"
                  "DTSTART;TZID=America/Denver:20260902T150000\r\n"
                  "END:VEVENT\r\nEND:VCALENDAR\r\n";
                int seen = 0;
                viki_cal_shred(zUpd, sizeof(zUpd)-1, "probe", &nAdd);
                viki_cal_reproject(&n);
                {   sqlite3_stmt *q=0; char *sm=0;
                    sqlite3_prepare_v2(dbK,
                      "SELECT summary FROM viki_event e JOIN viki_assert a ON a.id=e.id"
                      " WHERE a.arank=(SELECT max(arank) FROM viki_assert WHERE akey=a.akey)",
                      -1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW) sm=strdup((const char*)sqlite3_column_text(q,0));
                    sqlite3_finalize(q);
                    check(sm && strstr(sm,"MOVED")!=0,
                          "K8 a higher SEQUENCE wins -- RFC 5546, via core's ONE resolver",
                          sm?sm:"none");
                    free(sm);
                }
                check(count(dbK,"SELECT count(*) FROM viki_assert WHERE kind='event'")==2,
                      "K9 CONTROL: the superseded assertion STAYS in the store", "it was replaced");
                (void)seen;
            }

            {   /* the date-bound bug: ISO in, RFC 5545 basic stored */
                int nHit = 0;
                viki_sql("SELECT 1", 0, 0);
                {   sqlite3_stmt *q=0;
                    sqlite3_prepare_v2(dbK,
                      "SELECT count(*) FROM viki_event WHERE sortkey>='20260901T000000'"
                      " AND sortkey<='20260903T235959'", -1,&q,0);
                    if(sqlite3_step(q)==SQLITE_ROW) nHit=sqlite3_column_int(q,0);
                    sqlite3_finalize(q);
                }
                check(nHit>0, "K10 events are findable by a normalised range", "none in range");
            }
            RETAIN_END(g);
        }
        sqlite3_close(dbK);
    }

    printf("\n%d passed, %d failed\n", nPass, nFail);
    sqlite3_close(dbA); sqlite3_close(dbB);
    return nFail ? 1 : 0;
}
