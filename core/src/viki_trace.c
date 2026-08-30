/* viki_trace.c -- claims and the supersession chain. See viki_trace.h.
**
** Reads and writes ONLY through viki_put() and viki_sql(), the two doors
** viki_cal.c and viki_task.c use. Names viki_assert in SQL and nothing else.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viki_core.h"
#include "viki_trace.h"

static const VikiType vikiClaimType = { "claim", 0, sizeof(VikiClaim) };

static const char *clKey (const VikiAssert *p){
    const VikiClaim *c = (const VikiClaim*)p;
    return (c->zKey && c->zKey[0]) ? c->zKey : p->zId;
}
static const char *clRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *clText (const VikiAssert *p){
    const VikiClaim *c = (const VikiClaim*)p;
    return c->zCompose ? c->zCompose : (c->zText ? c->zText : "");
}
static const char *clCanon(const VikiAssert *p){
    const VikiClaim *c = (const VikiClaim*)p;
    return c->zJson ? c->zJson : "";
}
const struct VikiAssertVftbl vikiClaimVftbl = {
    &vikiClaimType, clKey, clRank, clText, clCanon
};

struct buildCtx { char *zJson; int bOk; int nSeen; };
static int buildRow(void *pApp, int nCol, const char *const *azVal){
    struct buildCtx *c = (struct buildCtx*)pApp;
    if( nCol < 2 ) return 0;
    c->nSeen++;
    c->bOk = (azVal[0] && azVal[0][0]=='1');
    if( azVal[1] ){
        size_t n = strlen(azVal[1]);
        c->zJson = (char*)malloc(n+1);
        if( c->zJson ) memcpy(c->zJson, azVal[1], n+1);
    }
    return 0;
}

VikiStatus viki_claim(VikiClaim *p, char *zIdOut){
    struct buildCtx ctx;
    char *zSql;
    VikiStatus rc;
    const char *zText, *zStatus, *zFals, *zBec, *zBy;

    if( !p || !p->zText || !p->zText[0] ) return VIKI_EINVAL;
    p->vftbl = &vikiClaimVftbl;
    p->zJson = 0; p->zCompose = 0;

    zText   = p->zText;
    zStatus = p->zStatus    ? p->zStatus    : "";
    zFals   = p->zFalsifier ? p->zFalsifier : "";
    zBec    = p->zBecause   ? p->zBecause   : "";
    zBy     = p->zBy        ? p->zBy        : "";

    /* SQLite validates and SQLite builds -- %Q makes each value a safe SQL
    ** literal, json_object() does the JSON escaping, and no escaper of ours
    ** exists to be wrong (core-probe C7).
    **
    ** TWO REFUSALS, both at write time:
    **   - a status outside k0..k4 is not a status.
    **   - a k0 with no falsifier is not knowledge. "It was checked" without
    **     "and here is what would have shown it wrong" is the exact shape of
    **     the confident error this file exists to make visible, so the one
    **     claim class that asserts correctness has to say how it could fail.
    **     Every other level may leave it empty: a k1 IS the admission, a k3
    **     is already known false, a k4 is held knowingly.
    **   - AN UNATTRIBUTED CLAIM IS REFUSED, and this one is not symmetry
    **     with the others -- it is a measurement. The compaction boundary
    **     that carries a trace's own account across the gap stores it as
    **     `type: "user"` with `model: null` (measured 2026-08-29 in this
    **     session's own transcript: five boundaries, 4,521,748 tokens
    **     dropped, every summary unattributed). So a trace's conclusions
    **     arrive at its successor wearing the HUMAN'S voice, which is why
    **     "I decided X" gets said about things the human decided.
    **
    **     THE ENVELOPE LOSES THE SENDER. Therefore the content must carry
    **     it: attribution that lives in metadata is attribution that will
    **     be stripped. `--by` is not bookkeeping, it is the field that
    **     survives the transition. */
    zSql = sqlite3_mprintf(
      "WITH c(st,fa,by) AS (SELECT %Q,%Q,%Q) SELECT"
      " CASE WHEN (SELECT st FROM c) NOT IN ('k0','k1','k2','k3','k4') THEN 0"
      "      WHEN (SELECT st FROM c)='k0' AND (SELECT fa FROM c)='' THEN 0"
      "      WHEN (SELECT by FROM c)='' THEN 0"
      "      ELSE 1 END,"
      " json_object('kind','claim','text',%Q,'status',(SELECT st FROM c),"
      "             'falsified_by',(SELECT fa FROM c),'because',%Q,'by',%Q)",
      zStatus, zFals, zBy, zText, zBec, zBy);
    if( !zSql ) return VIKI_ENOMEM;

    memset(&ctx, 0, sizeof(ctx));
    rc = viki_sql(zSql, buildRow, &ctx);
    sqlite3_free(zSql);
    if( rc!=VIKI_OK ){ free(ctx.zJson); return rc; }
    if( ctx.nSeen==0 || !ctx.zJson ){ free(ctx.zJson); return VIKI_ENOMEM; }
    if( !ctx.bOk ){ free(ctx.zJson); return VIKI_EINVAL; }
    p->zJson = ctx.zJson;

    /* The status and the falsifier are IN the embedded text: a claim you
    ** cannot retrieve is a claim the next trace will re-derive from scratch. */
    p->zCompose = sqlite3_mprintf("%s%s%s%s%s%s%s",
        zText,
        zStatus[0] ? "\nstatus: "       : "", zStatus,
        zFals[0]   ? "\nfalsified by: " : "", zFals,
        zBec[0]    ? "\nbecause: "      : "", zBec);
    if( !p->zCompose ){ free(p->zJson); p->zJson=0; return VIKI_ENOMEM; }

    rc = viki_put((VikiAssert*)p);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, p->zId, VIKI_ID_HEX+1);
    free(p->zJson);            p->zJson = 0;
    sqlite3_free(p->zCompose); p->zCompose = 0;
    return rc;
}

/* ---- the chain ------------------------------------------------------- */
struct chainCtx { viki_chain_row xRow; void *pApp; int nSeen; int rcStop; };

static int chainRow(void *pApp, int nCol, const char *const *az){
    struct chainCtx *c = (struct chainCtx*)pApp;
    VikiChainRow r;
    if( c->rcStop || nCol < 8 ) return 0;
    r.nDepth   = az[0] ? atoi(az[0]) : 0;
    r.bForward = az[1] && az[1][0]=='1';
    r.zId      = az[2]; r.zKind = az[3]; r.zTs = az[4];
    r.zText    = az[5]; r.zStatus = az[6]; r.zBecause = az[7];
    r.zBy      = nCol > 8 ? az[8] : "";
    c->nSeen++;
    if( c->xRow && c->xRow(c->pApp, &r) ) c->rcStop = 1;
    return 0;
}

VikiStatus viki_why(const char *zId, viki_chain_row xRow, void *pApp){
    struct chainCtx c;
    char *zSql;
    VikiStatus rc;
    if( !zId || !zId[0] ) return VIKI_EINVAL;

    /* TWO RECURSIVE WALKS AND THE SUBJECT, in one statement.
    **
    ** back: follow a.supersedes upward -- what this replaced, and what that
    **       replaced, as far as the chain goes.
    ** fwd:  follow s.supersedes = a.id downward -- what replaced this.
    **
    ** Ordered forward-first so a reader sees the CORRECTION before the thing
    ** corrected. That ordering is the entire user-visible point: a trace
    ** arriving at a retired claim must meet its retirement first, or it will
    ** act on the claim exactly the way the previous trace did.
    **
    ** No depth cap. The chain is a history of one thing being corrected, and
    ** a cap would silently hide the oldest error -- which is usually the one
    ** worth reading. SQLite terminates on the UNION's duplicate elimination
    ** if a cycle is ever written. */
    /* BODY IS NOT ALWAYS JSON, AND json_extract() THROWS RATHER THAN
    ** RETURNING NULL. A claim's body and a task's body are json_object()
    ** output; a NOTE's body is its raw text, because viki_note has no fields
    ** to encode. So `coalesce(json_extract(body,'$.text'), atext)` does not
    ** degrade -- it ABORTS the whole statement with "malformed JSON", and
    ** `viki why` worked only on a store where every assertion happened to be
    ** structured. Found by Y8, which walks a task retired by a plain note --
    ** the exact shape the ledger produces every time something arrives.
    **
    ** json_valid() guards each read, and the projection is factored into one
    ** CTE so the guard cannot be applied in two places and forgotten in a
    ** third. */
    zSql = sqlite3_mprintf(
      "WITH RECURSIVE"
      " back(id,depth) AS ("
      "   SELECT supersedes,1 FROM viki_assert WHERE id=%Q AND supersedes IS NOT NULL"
      "   UNION"
      "   SELECT a.supersedes,b.depth+1 FROM viki_assert a JOIN back b ON a.id=b.id"
      "    WHERE a.supersedes IS NOT NULL),"
      " fwd(id,depth) AS ("
      "   SELECT id,1 FROM viki_assert WHERE supersedes=%Q"
      "   UNION"
      "   SELECT a.id,f.depth+1 FROM viki_assert a JOIN fwd f ON a.supersedes=f.id),"
      " j(id,kind,ts,text,status,because,by) AS ("
      "   SELECT a.id,a.kind,a.ts,"
      "     CASE WHEN json_valid(a.body)"
      "          THEN coalesce(json_extract(a.body,'$.text'),a.atext) ELSE a.atext END,"
      "     CASE WHEN json_valid(a.body)"
      "          THEN coalesce(json_extract(a.body,'$.status'),'') ELSE '' END,"
      "     CASE WHEN json_valid(a.body)"
      "          THEN coalesce(json_extract(a.body,'$.because'),'') ELSE '' END,"
      "     CASE WHEN json_valid(a.body)"
      "          THEN coalesce(json_extract(a.body,'$.by'),'') ELSE '' END"
      "   FROM viki_assert a)"
      " SELECT depth,fwd,id,kind,ts,text,status,because,by FROM ("
      "   SELECT f.depth AS depth,1 AS fwd,j.id,j.kind,j.ts,j.text,j.status,j.because,j.by"
      "     FROM fwd f JOIN j ON j.id=f.id"
      "   UNION ALL"
      "   SELECT 0,0,j.id,j.kind,j.ts,j.text,j.status,j.because,j.by"
      "     FROM j WHERE j.id=%Q"
      "   UNION ALL"
      "   SELECT b.depth,0,j.id,j.kind,j.ts,j.text,j.status,j.because,j.by"
      "     FROM back b JOIN j ON j.id=b.id)"
      " ORDER BY fwd DESC, depth ASC", zId, zId, zId);
    if( !zSql ) return VIKI_ENOMEM;

    memset(&c, 0, sizeof(c));
    c.xRow = xRow; c.pApp = pApp;
    rc = viki_sql(zSql, chainRow, &c);
    sqlite3_free(zSql);
    if( rc!=VIKI_OK ) return rc;
    /* NOT FOUND AND NO-CHAIN MUST NOT LOOK THE SAME. An id that is not in
    ** this diary yields zero rows; an id with no chain still yields itself at
    ** depth 0. So zero rows means the subject is absent, and saying so is the
    ** difference between "nothing superseded it" and "I could not see it". */
    return c.nSeen ? VIKI_OK : VIKI_ENOTFOUND;
}
