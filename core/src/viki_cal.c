/*
** viki_cal.c -- calendar assertions. NO PARSER.
**
** THE CONTRACT IS SQLITE, SO THE PARSER IS SQLITE'S. Input is jsCalendar
** (RFC 8984) and every field is reached with json_extract()/json_each(). The
** ~180 lines of hand-written iCalendar lexing this file used to carry --
** unfolding, the params/value split, quote tracking, TEXT unescaping, the
** four time forms -- are gone, and with them every place an ICS parser is
** silently wrong rather than loudly broken.
**
** WHY jsCalendar AND NOT jCal (RFC 7265). Both parse in pure SQL; measured.
** jCal is the mechanical transform of iCalendar into positional arrays --
** ["dtstart",{"tzid":"..."},"date-time","2026-09-01T15:00:00"] -- so every
** access is `$[3]` and the shape is iCalendar's problems in JSON clothing.
** jsCalendar is object-based, so a field is `$.start`, and three of its
** design decisions are exactly viki's:
**
**   - `start` is LOCAL AS WRITTEN and `timeZone` is an IANA NAME. No offset
**     appears anywhere, which is the rule the predecessor had to enforce by
**     hand and here is simply the format.
**   - `recurrenceOverrides` is an OBJECT KEYED BY RECURRENCE-ID, so
**     json_each() over it yields exactly the (UID, RECURRENCE-ID) identity
**     that RFC 5546 resolution needs.
**   - `sequence` and `updated` are named fields, so RFC 5546 precedence is
**     two json_extract() calls.
**
** AND IT IS WHAT REAL SOURCES ALREADY EMIT: JMAP is jsCalendar natively,
** Google Calendar and Microsoft Graph return JSON, and EventKit on iOS hands
** you objects. iCalendar text is the interchange format for .ics FILES --
** the narrowest ingress path -- and converting it is a connector's job
** (SCOPES L3), not core's.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viki_core.h"
#include "viki_cal.h"

/* ---- the assertion subtype ------------------------------------------- */
static const VikiType vikiEventType = { "event", 0, sizeof(VikiEvent) };

static const char *evKey (const VikiAssert *p){ return ((const VikiEvent*)p)->zKeyBuf; }
static const char *evRank(const VikiAssert *p){ return ((const VikiEvent*)p)->zRankBuf; }
static const char *evText(const VikiAssert *p){
    const VikiEvent *e = (const VikiEvent*)p; return e->zText ? e->zText : "";
}
static const char *evCanon(const VikiAssert *p){
    const VikiEvent *e = (const VikiEvent*)p; return e->zRaw ? e->zRaw : "";
}
const struct VikiAssertVftbl vikiEventVftbl = {
    &vikiEventType, evKey, evRank, evText, evCanon
};

/* RFC 8984 SS 4.1.4 / RFC 5546 SS 3.2 PRECEDENCE, AS A LEXICAL SORT KEY.
** "highest sequence wins; updated breaks the tie" becomes one comparable
** string, so core's single viki_current() -- shared with notes -- implements
** the calendar rule without knowing it exists. Zero-padded to ten digits
** because sequence is an unbounded integer and "10" must not sort below "9". */
static void ev_rank(VikiEvent *e){
    long seq = e->iSequence < 0 ? 0 : e->iSequence;
    snprintf(e->zRankBuf, sizeof(e->zRankBuf), "%010ld\x1f%s",
             seq, e->zDtstamp ? e->zDtstamp : "");
}
static void ev_key(VikiEvent *e){
    snprintf(e->zKeyBuf, sizeof(e->zKeyBuf), "%s\x1f%s",
             e->zUid ? e->zUid : "", e->zRecurrenceId ? e->zRecurrenceId : "");
}

/* ---- the projection -------------------------------------------------- */
static const char zCalSchema[] =
  "CREATE TABLE IF NOT EXISTS viki_event("
  "  id TEXT PRIMARY KEY,"
  "  uid TEXT NOT NULL, recurrence_id TEXT NOT NULL,"
  "  sequence INTEGER NOT NULL, updated TEXT,"
  "  component TEXT, summary TEXT, status TEXT, free_busy TEXT, organizer TEXT,"
  "  start TEXT, tzid TEXT, form TEXT,"
  "  duration TEXT, rrule TEXT, source TEXT,"
  /* NORMALISED start, for range queries only. Never displayed, never synced,
  ** and never mistaken for the stored time -- see cal_norm_bound(). */
  "  sortkey TEXT"
  ") WITHOUT ROWID;"
  "CREATE INDEX IF NOT EXISTS viki_event_sort ON viki_event(sortkey);"
  "CREATE INDEX IF NOT EXISTS viki_event_uid  ON viki_event(uid, recurrence_id);";

VikiStatus viki_cal_attach(sqlite3 *db){
    char *zErr = 0;
    if( !db ) return VIKI_EINVAL;
    /* json_extract() is required, not optional: it IS the parser. Fail here
    ** rather than silently ingesting nothing. */
    if( sqlite3_exec(db, "SELECT json_valid('{}')", 0, 0, &zErr)!=SQLITE_OK ){
        sqlite3_free(zErr);
        return VIKI_ESQL;
    }
    if( sqlite3_exec(db, zCalSchema, 0, 0, &zErr)!=SQLITE_OK ){
        sqlite3_free(zErr); return VIKI_ESQL;
    }
    return VIKI_OK;
}

/* RANGE BOUNDS MUST BE COMPARED IN ONE FORM. jsCalendar times are extended
** ISO ("2026-09-01T15:00:00") and a caller may write anything; strip the
** separators from both so a comparison is digits against digits, and widen a
** bare date to cover the whole day rather than its first instant. An empty
** day looks like a quiet day, so a silently empty range is the worst
** available answer. */
static void cal_norm_bound(const char *z, int bEnd, char *zOut, size_t nOut){
    size_t j = 0; int nDigit = 0;
    zOut[0] = 0;
    if( !z || !z[0] ) return;
    for( ; *z && j+1<nOut; z++ ){
        if( *z=='-' || *z==':' ) continue;
        zOut[j++] = *z;
        if( *z>='0' && *z<='9' ) nDigit++;
    }
    zOut[j] = 0;
    if( nDigit==8 && j+7<nOut ){
        memcpy(zOut+j, bEnd ? "T235959" : "T000000", 7);
        zOut[j+7] = 0;
    }
}

/* RFC 8984 SS 4.3: `timeZone` null means FLOATING, "Etc/UTC" means UTC, and
** `showWithoutTime` means the date alone is significant. The four forms the
** predecessor derived from iCalendar syntax are stated outright here. */
static const char *cal_form(const char *zTz, int bNoTime){
    if( bNoTime ) return "date";
    if( !zTz || !zTz[0] ) return "floating";
    if( strcmp(zTz,"Etc/UTC")==0 || strcmp(zTz,"UTC")==0 ) return "utc";
    return "zoned";
}

static sqlite3 *cal_db(void){
    const VikiStore *p;
    if( !RETAINED(VikiStore) ) return 0;
    p = RECALL(VikiStore);
    return (p && p->db) ? p->db : 0;
}
static char *dupz(const char *z){ char *r; if(!z) return 0; r=(char*)malloc(strlen(z)+1); if(r) strcpy(r,z); return r; }

static void ev_project(sqlite3 *db, const VikiEvent *e){
    sqlite3_stmt *st = 0;
    char zSort[64];
    cal_norm_bound(e->zDtstart, 0, zSort, sizeof(zSort));
    if( sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO viki_event(id,uid,recurrence_id,sequence,updated,"
        " component,summary,status,free_busy,organizer,start,tzid,form,"
        " duration,rrule,source,sortkey)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)",
        -1, &st, 0)!=SQLITE_OK ) return;
    #define B(i,z) sqlite3_bind_text(st,(i),(z)?(z):"",-1,SQLITE_TRANSIENT)
    B(1,e->zId); B(2,e->zUid); B(3,e->zRecurrenceId);
    sqlite3_bind_int(st,4,e->iSequence);
    B(5,e->zDtstamp); B(6,e->zComponent); B(7,e->zSummary); B(8,e->zStatus);
    B(9,e->zTransp); B(10,e->zOrganizer);
    B(11,e->zDtstart); B(12,e->zDtstartTzid); B(13,e->zDtstartForm);
    B(14,e->zDuration); B(15,e->zRrule); B(16,e->zSource); B(17,zSort);
    #undef B
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* ---- ingest: ONE SELECT is the whole parser -------------------------- */

/* Accepts a single jsCalendar object, a JSON array of them, or a JMAP-shaped
** {"events":{...}} / {"list":[...]} envelope. json_each() flattens all three,
** so the caller is not required to know which its source produced. */
static const char *zEventSel =
  "WITH src(j) AS ("
  "  SELECT CASE"
  "    WHEN json_type(?1)='array'  THEN ?1"
  "    WHEN json_type(?1,'$.events')IS NOT NULL THEN json_extract(?1,'$.events')"
  "    WHEN json_type(?1,'$.list')  IS NOT NULL THEN json_extract(?1,'$.list')"
  "    ELSE json_array(json(?1)) END"
  "),"
  "ev(o) AS (SELECT value FROM src, json_each(src.j))"
  "SELECT json_extract(o,'$.uid'),"                    /* 0 uid              */
  "       coalesce(json_extract(o,'$.recurrenceId'),''),"/*1 recurrence-id   */
  "       coalesce(json_extract(o,'$.sequence'),0),"   /* 2 sequence         */
  "       coalesce(json_extract(o,'$.updated'),''),"   /* 3 updated (DTSTAMP)*/
  "       coalesce(json_extract(o,'$.@type'),'Event')," /*4 Event|Task       */
  "       coalesce(json_extract(o,'$.title'),''),"     /* 5 summary          */
  "       coalesce(json_extract(o,'$.description'),'')," /*6 description     */
  "       coalesce(json_extract(o,'$.status'),''),"    /* 7 status           */
  "       coalesce(json_extract(o,'$.freeBusyStatus'),'')," /*8 transp       */
  "       coalesce(json_extract(o,'$.replyTo.imip'),'')," /*9 organizer      */
  "       coalesce(json_extract(o,'$.start'),json_extract(o,'$.due'),'')," /*10*/
  "       coalesce(json_extract(o,'$.timeZone'),''),"  /* 11 IANA name       */
  "       coalesce(json_extract(o,'$.showWithoutTime'),0)," /*12 date-only   */
  "       coalesce(json_extract(o,'$.duration'),''),"  /* 13 duration        */
  "       coalesce(json_extract(o,'$.recurrenceRules[0].frequency'),'')," /*14*/
  "       o"                                            /* 15 the object      */
  "  FROM ev WHERE json_extract(o,'$.uid') IS NOT NULL";

/* recurrenceOverrides is an OBJECT KEYED BY RECURRENCE-ID, which is exactly
** the identity RFC 5546 resolution needs -- so an override becomes its own
** assertion on its own key, with no special case anywhere in core. */
static const char *zOverrideSel =
  "SELECT key, value FROM json_each(?1,'$.recurrenceOverrides')";

static void put_one(sqlite3 *db, VikiEvent *e, int *pn){
    ev_key(e); ev_rank(e);
    if( viki_put((VikiAssert*)e)==VIKI_OK ){ ev_project(db, e); (*pn)++; }
}

VikiStatus viki_cal_ingest(const char *zJson, const char *zSource, int *pnAdded){
    sqlite3 *db = cal_db();
    sqlite3_stmt *st = 0;
    int n = 0;
    if( pnAdded ) *pnAdded = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zJson ) return VIKI_EINVAL;
    /* MALFORMED INPUT IS REFUSED, NOT REPORTED AS AN EMPTY CALENDAR. An
    ** adapter that fetched an HTML error page must not read as a quiet day,
    ** and json_valid() is the whole check now that SQLite is the parser. */
    {
        sqlite3_stmt *v = 0; int bOk = 0;
        if( sqlite3_prepare_v2(db, "SELECT json_valid(?1)", -1, &v, 0)==SQLITE_OK ){
            sqlite3_bind_text(v, 1, zJson, -1, SQLITE_STATIC);
            if( sqlite3_step(v)==SQLITE_ROW ) bOk = sqlite3_column_int(v, 0);
            sqlite3_finalize(v);
        }
        if( !bOk ) return VIKI_EINVAL;
    }
    if( sqlite3_prepare_v2(db, zEventSel, -1, &st, 0)!=SQLITE_OK ) return VIKI_ESQL;
    sqlite3_bind_text(st, 1, zJson, -1, SQLITE_STATIC);
    while( sqlite3_step(st)==SQLITE_ROW ){
        VikiEvent e;
        char *zDesc, *zObj, *zRaw = 0, *zTx = 0;
        #define C(i) ((const char*)sqlite3_column_text(st,(i)))
        memset(&e, 0, sizeof(e));
        e.vftbl = &vikiEventVftbl;
        e.zUid = C(0); e.zRecurrenceId = C(1);
        e.iSequence = sqlite3_column_int(st, 2);
        e.zDtstamp = C(3); e.zComponent = C(4);
        e.zSummary = C(5); zDesc = dupz(C(6));
        e.zStatus = C(7); e.zTransp = C(8); e.zOrganizer = C(9);
        e.zDtstart = C(10); e.zDtstartTzid = C(11);
        e.zDtstartForm = cal_form(C(11), sqlite3_column_int(st, 12));
        e.zDuration = C(13); e.zRrule = C(14);
        zObj = dupz(C(15));
        #undef C
        e.zSource = zSource;
        e.zTs = e.zDtstamp;
        /* canon() is the identity tuple, not the raw object: two sources
        ** emitting the same event with different KEY ORDER or extra vendor
        ** fields are ONE assertion, and identity that depended on either
        ** would store both. */
        zRaw = sqlite3_mprintf("%s\x1f%s\x1f%d\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s",
                 e.zUid, e.zRecurrenceId, e.iSequence, e.zDtstamp, e.zComponent,
                 e.zSummary, e.zDtstart, e.zDtstartTzid, e.zDuration, e.zRrule);
        zTx  = sqlite3_mprintf("%s%s%s", e.zSummary,
                 (e.zSummary[0] && zDesc && zDesc[0]) ? "\n\n" : "",
                 zDesc ? zDesc : "");
        e.zRaw = zRaw; e.zText = zTx;
        put_one(db, &e, &n);
        sqlite3_free(zRaw); sqlite3_free(zTx);

        /* overrides, each on its own (uid, recurrenceId) key */
        if( zObj ){
            sqlite3_stmt *ov = 0;
            if( sqlite3_prepare_v2(db, zOverrideSel, -1, &ov, 0)==SQLITE_OK ){
                sqlite3_bind_text(ov, 1, zObj, -1, SQLITE_STATIC);
                while( sqlite3_step(ov)==SQLITE_ROW ){
                    VikiEvent o = e;
                    const char *zK = (const char*)sqlite3_column_text(ov, 0);
                    const char *zV = (const char*)sqlite3_column_text(ov, 1);
                    char *zT2 = 0, *zR2 = 0;
                    sqlite3_stmt *t = 0;
                    o.zRecurrenceId = zK;
                    o.zDtstart = zK;      /* the override's start IS its key   */
                    if( sqlite3_prepare_v2(db,
                        "SELECT coalesce(json_extract(?1,'$.title'),?2)", -1, &t, 0)==SQLITE_OK ){
                        sqlite3_bind_text(t, 1, zV, -1, SQLITE_STATIC);
                        sqlite3_bind_text(t, 2, e.zSummary, -1, SQLITE_STATIC);
                        if( sqlite3_step(t)==SQLITE_ROW ) zT2 = dupz((const char*)sqlite3_column_text(t,0));
                        sqlite3_finalize(t);
                    }
                    o.zSummary = zT2 ? zT2 : e.zSummary;
                    zR2 = sqlite3_mprintf("%s\x1f%s\x1f%d\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s",
                            o.zUid, o.zRecurrenceId, o.iSequence, o.zDtstamp,
                            o.zComponent, o.zSummary, o.zDtstart, o.zDtstartTzid,
                            o.zDuration, "");
                    o.zRaw = zR2; o.zText = o.zSummary; o.zRrule = "";
                    put_one(db, &o, &n);
                    sqlite3_free(zR2); free(zT2);
                }
                sqlite3_finalize(ov);
            }
            free(zObj);
        }
        free(zDesc);
    }
    sqlite3_finalize(st);
    if( pnAdded ) *pnAdded = n;
    return VIKI_OK;
}

/* ---- reading the resolved tier --------------------------------------- */
VikiStatus viki_cal_events(const char *zFrom, const char *zTo,
                           viki_cal_row x, void *pApp){
    sqlite3 *db = cal_db();
    sqlite3_stmt *st = 0;
    char zF[64], zT[64];
    if( !db ) return VIKI_ENOCTX;
    cal_norm_bound(zFrom, 0, zF, sizeof(zF));
    cal_norm_bound(zTo,   1, zT, sizeof(zT));
    /* RESOLUTION HAPPENS HERE, NOT IN STORAGE: one row per (uid,
    ** recurrence_id), the winner being the highest arank among assertions
    ** nothing supersedes. That is RFC 5546's rule, expressed once, in core,
    ** shared with notes. The losers stay in viki_assert. */
    if( sqlite3_prepare_v2(db,
        "SELECT e.* FROM viki_event e JOIN viki_assert a ON a.id=e.id"
        " WHERE NOT EXISTS(SELECT 1 FROM viki_assert s WHERE s.supersedes=a.id)"
        "   AND a.arank = (SELECT max(a2.arank) FROM viki_assert a2"
        "                   WHERE a2.akey=a.akey"
        "                     AND NOT EXISTS(SELECT 1 FROM viki_assert s2"
        "                                     WHERE s2.supersedes=a2.id))"
        "   AND (?1='' OR e.sortkey>=?1) AND (?2='' OR e.sortkey<=?2)"
        " ORDER BY e.sortkey", -1, &st, 0)!=SQLITE_OK ) return VIKI_ESQL;
    sqlite3_bind_text(st, 1, zF, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, zT, -1, SQLITE_STATIC);
    while( sqlite3_step(st)==SQLITE_ROW ){
        VikiEvent e;
        memset(&e, 0, sizeof(e));
        e.vftbl = &vikiEventVftbl;
        #define C(i) ((const char*)sqlite3_column_text(st,(i)))
        snprintf(e.zId, sizeof(e.zId), "%s", C(0));
        e.zUid=C(1); e.zRecurrenceId=C(2);
        e.iSequence=sqlite3_column_int(st,3); e.zDtstamp=C(4);
        e.zComponent=C(5); e.zSummary=C(6); e.zStatus=C(7);
        e.zTransp=C(8); e.zOrganizer=C(9);
        e.zDtstart=C(10); e.zDtstartTzid=C(11); e.zDtstartForm=C(12);
        e.zDuration=C(13); e.zRrule=C(14); e.zSource=C(15);
        #undef C
        if( x && x(pApp, &e) ) break;
    }
    sqlite3_finalize(st);
    return VIKI_OK;
}

VikiStatus viki_cal_reproject(int *pnRows){
    sqlite3 *db = cal_db();
    sqlite3_stmt *st = 0; int n = 0;
    if( !db ) return VIKI_ENOCTX;
    if( pnRows ) *pnRows = 0;
    sqlite3_exec(db, "DELETE FROM viki_event", 0, 0, 0);
    if( sqlite3_prepare_v2(db,
        "SELECT id, body FROM viki_assert WHERE kind='event'", -1, &st, 0)!=SQLITE_OK )
        return VIKI_ESQL;
    while( sqlite3_step(st)==SQLITE_ROW ){
        VikiEvent e; char *zB = dupz((const char*)sqlite3_column_text(st,1));
        char *p, *aF[10]; int i = 0;
        if( !zB ) continue;
        for( p = zB; i<10; i++ ){
            char *q = strchr(p, '\x1f');
            aF[i] = p;
            if( !q ){ i++; break; }
            *q = 0; p = q+1;
        }
        while( i<10 ) aF[i++] = (char*)"";
        memset(&e, 0, sizeof(e));
        e.vftbl = &vikiEventVftbl;
        snprintf(e.zId, sizeof(e.zId), "%s", (const char*)sqlite3_column_text(st,0));
        e.zUid=aF[0]; e.zRecurrenceId=aF[1]; e.iSequence=atoi(aF[2]);
        e.zDtstamp=aF[3]; e.zComponent=aF[4]; e.zSummary=aF[5];
        e.zDtstart=aF[6]; e.zDtstartTzid=aF[7]; e.zDuration=aF[8]; e.zRrule=aF[9];
        e.zDtstartForm = cal_form(e.zDtstartTzid, 0);
        ev_key(&e); ev_rank(&e);
        ev_project(db, &e);
        free(zB);
        n++;
    }
    sqlite3_finalize(st);
    if( pnRows ) *pnRows = n;
    return VIKI_OK;
}
