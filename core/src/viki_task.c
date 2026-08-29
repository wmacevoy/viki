/* viki_task.c -- tasks and the ledger.  See viki_task.h for the why.
**
** EVERY read and write here goes through viki_put() and viki_sql(), the same
** two doors viki_cal.c uses. This file names viki_assert in SQL and nothing
** else; it opens no connection of its own and holds no statement.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viki_core.h"
#include "viki_task.h"

static const VikiType vikiTaskType = { "task", 0, sizeof(VikiTask) };

/* An unkeyed task competes only with itself, exactly as an unkeyed note does:
** key() is called once before zId is filled (yielding "") and again at bind
** time (yielding the id). That asymmetry is viki_put's, not ours -- mirroring
** it is what keeps a task's identity computed the same way as a note's. */
static const char *tkKey (const VikiAssert *p){
    const VikiTask *t = (const VikiTask*)p;
    return (t->zKey && t->zKey[0]) ? t->zKey : p->zId;
}
static const char *tkRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *tkText (const VikiAssert *p){
    const VikiTask *t = (const VikiTask*)p;
    return t->zCompose ? t->zCompose : (t->zText ? t->zText : "");
}
/* canon() is the JSON, so identity is the STRUCTURED content. Two tasks with
** the same words but different owners are different assertions -- which is
** the whole reason the ledger can tell yours from theirs. */
static const char *tkCanon(const VikiAssert *p){
    const VikiTask *t = (const VikiTask*)p;
    return t->zJson ? t->zJson : "";
}
const struct VikiAssertVftbl vikiTaskVftbl = {
    &vikiTaskType, tkKey, tkRank, tkText, tkCanon
};

/* ---- one SQL round trip: validate the due date and build the body ----- */
struct buildCtx { char *zJson; int bDueOk; int nSeen; };

static int buildRow(void *pApp, int nCol, const char *const *azVal){
    struct buildCtx *c = (struct buildCtx*)pApp;
    if( nCol < 2 ) return 0;
    c->nSeen++;
    c->bDueOk = (azVal[0] && azVal[0][0]=='1');
    if( azVal[1] ){
        size_t n = strlen(azVal[1]);
        c->zJson = (char*)malloc(n+1);
        if( c->zJson ) memcpy(c->zJson, azVal[1], n+1);
    }
    return 0;
}

VikiStatus viki_task(VikiTask *p, char *zIdOut){
    struct buildCtx ctx;
    char *zSql;
    VikiStatus rc;
    const char *zText, *zWho, *zDue, *zPlace, *zState, *zChan;

    if( !p ) return VIKI_EINVAL;
    if( !p->zText || !p->zText[0] ) return VIKI_EINVAL;
    p->vftbl = &vikiTaskVftbl;
    p->zJson = 0; p->zCompose = 0;

    zText  = p->zText;
    zWho   = p->zWho     ? p->zWho     : "";
    zDue   = p->zDue     ? p->zDue     : "";
    zPlace = p->zPlace   ? p->zPlace   : "";
    zState = p->zState   ? p->zState   : "";
    zChan  = p->zChannel ? p->zChannel : "";

    /* SQLite VALIDATES and SQLite BUILDS. %Q makes each value a safe SQL
    ** literal; json_object() then does the JSON escaping. Neither step is a
    ** parser of ours, which is what core-probe C7 is protecting.
    **
    ** The GLOBs accept a bare date and a full ISO-8601 stamp and nothing
    ** else. A bare date means end of day; that is the reader's rule, not a
    ** fact this file should encode. */
    zSql = sqlite3_mprintf(
      "WITH t(due) AS (SELECT %Q) SELECT"
      " CASE WHEN (SELECT due FROM t)='' THEN 1"
      "      WHEN (SELECT due FROM t) GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]' THEN 1"
      "      WHEN (SELECT due FROM t) GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T*' THEN 1"
      "      ELSE 0 END,"
      " json_object('kind','task','text',%Q,'who',%Q,'due',(SELECT due FROM t),"
      "             'place',%Q,'state',%Q,'channel',%Q)",
      zDue, zText, zWho, zPlace, zState, zChan);
    if( !zSql ) return VIKI_ENOMEM;

    memset(&ctx, 0, sizeof(ctx));
    rc = viki_sql(zSql, buildRow, &ctx);
    sqlite3_free(zSql);
    if( rc!=VIKI_OK ){ free(ctx.zJson); return rc; }
    if( ctx.nSeen==0 || !ctx.zJson ){ free(ctx.zJson); return VIKI_ENOMEM; }
    if( !ctx.bDueOk ){
        free(ctx.zJson);
        return VIKI_EINVAL;      /* the refusal viki_task.h promises */
    }
    p->zJson = ctx.zJson;

    /* text() is what gets chunked, so the owner, the date and the place are
    ** IN it. A ledger row that retrieval cannot find is half a memory. */
    p->zCompose = sqlite3_mprintf("%s%s%s%s%s%s%s",
        zText,
        zWho[0]   ? "\nowed by: "  : "", zWho,
        zDue[0]   ? "\ndue: "      : "", zDue,
        zPlace[0] ? "\nplace: "    : "", zPlace);
    if( !p->zCompose ){ free(p->zJson); p->zJson=0; return VIKI_ENOMEM; }

    rc = viki_put((VikiAssert*)p);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, p->zId, VIKI_ID_HEX+1);
    free(p->zJson);            p->zJson = 0;
    sqlite3_free(p->zCompose); p->zCompose = 0;
    return rc;
}

/* ---- the ledger ------------------------------------------------------ */
struct ledgerCtx { const char *zMe; viki_task_row xRow; void *pApp; int rcStop; };

static int ledgerRow(void *pApp, int nCol, const char *const *azVal){
    struct ledgerCtx *c = (struct ledgerCtx*)pApp;
    VikiTaskRow r;
    if( c->rcStop ) return 0;
    if( nCol < 7 ) return 0;
    r.zId = azVal[0]; r.zTs = azVal[1]; r.zWho = azVal[2]; r.zDue = azVal[3];
    r.zPlace = azVal[4]; r.zState = azVal[5]; r.zText = azVal[6];
    /* "MINE" IS UNOWNED OR ME. A commitment nobody claimed is one the reader
    ** is still carrying -- the predecessor settled that and it was right. */
    r.bMine = (!r.zWho || !r.zWho[0]
               || (c->zMe && c->zMe[0] && strcmp(r.zWho, c->zMe)==0));
    if( c->xRow && c->xRow(c->pApp, &r) ) c->rcStop = 1;
    return 0;
}

VikiStatus viki_ledger(const char *zMe, viki_task_row xRow, void *pApp){
    struct ledgerCtx c;
    /* LIVE ONLY. A task another assertion supersedes has been structured
    ** further, retired, or answered, and must LEAVE -- a promise retired last
    ** month still reading as owed makes the brief wrong in the direction of
    ** anxiety, which is what this whole thing exists to remove.
    **
    ** ORDER BY DUE, NOT BY ts. Write order is not obligation order. Undated
    ** last rather than first: an undated task is a question ("is this a
    ** promise, or a note?"), not an emergency. */
    static const char zSql[] =
      "SELECT a.id, a.ts,"
      " coalesce(json_extract(a.body,'$.who'),''),"
      " coalesce(json_extract(a.body,'$.due'),''),"
      " coalesce(json_extract(a.body,'$.place'),''),"
      " coalesce(json_extract(a.body,'$.state'),''),"
      " coalesce(json_extract(a.body,'$.text'),'')"
      " FROM viki_assert a"
      " WHERE a.kind='task'"
      "   AND NOT EXISTS (SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
      " ORDER BY (coalesce(json_extract(a.body,'$.due'),'')=''),"
      "          json_extract(a.body,'$.due'), a.ts";
    memset(&c, 0, sizeof(c));
    c.zMe = zMe; c.xRow = xRow; c.pApp = pApp;
    return viki_sql(zSql, ledgerRow, &c);
}
