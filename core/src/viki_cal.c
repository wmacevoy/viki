/*
** viki_cal.c -- calendar assertions.
**
** TIME IS VIKI'S SECOND NEW THING (retrieval is the first); everything else
** in core exists to store and merge assertions. So this file is deliberately
** thin: it is the RFC 5545 parser, ported, plus a VikiAssert subtype. The
** grow-only store, the content addressing, the resolution rule and the merge
** are already core's and are not restated here.
**
** THE PORT IS THE POINT. cal_event in the predecessor was a second, parallel
** implementation of "grow-only rows on an identity key, resolved at read
** time, losers retained" -- the same structure as viki_note, written twice
** with different column names and different SQL. Here it is one subtype:
**
**     key()   uid US recurrence_id          RFC 5546's identity
**     rank()  zero-padded SEQUENCE + DTSTAMP  RFC 5546's precedence, verbatim
**     canon() the raw component text        so identity is the bytes received
**     text()  SUMMARY + DESCRIPTION         what retrieval should see
**
** and viki_current() -- ONE statement, shared with notes -- is then exactly
** RFC 5546 SS 3.2's "highest SEQUENCE, then latest DTSTAMP wins". Superseded
** assertions stay, because the store is grow-only; that was a deliberate
** choice there and is a property here.
**
** DELIBERATELY ABSENT, and this is unchanged from the predecessor's reasoning:
** occurrence expansion. It depends on the viewer's zone, the query window and
** the tzdata version, so it is not a shareable fact and does not belong in a
** grow-only store. Times are kept AS WRITTEN with their IANA TZID and NEVER
** as an offset.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "viki_core.h"
#include "viki_cal.h"

/* ---- RFC 5545 lexing, ported unchanged in behaviour ------------------ */

/* THE FOLD IS CRLF + SPACE AND BOTH GO. Keeping the space corrupts every
** long SUMMARY into a plausible string, which is worse than failing. */
static char *ics_unfold(const char *z, size_t n){
    char *out = (char*)malloc(n + 1);
    size_t i = 0, j = 0;
    if( !out ) return 0;
    while( i < n ){
        if( z[i]=='\r' && i+1<n && z[i+1]=='\n' ){
            if( i+2<n && (z[i+2]==' '||z[i+2]=='\t') ){ i += 3; continue; }
            out[j++] = '\n'; i += 2; continue;
        }
        if( z[i]=='\n' ){
            if( i+1<n && (z[i+1]==' '||z[i+1]=='\t') ){ i += 2; continue; }
            out[j++] = '\n'; i++; continue;
        }
        out[j++] = z[i++];
    }
    out[j] = 0;
    return out;
}

/* RFC 5545 SS 3.3.11 TEXT escaping, undone in place. Only these four
** sequences are defined; a backslash before anything else is literal. */
static void ics_unescape(char *z){
    char *r = z, *w = z;
    while( *r ){
        if( *r=='\\' && r[1] ){
            switch( r[1] ){
                case 'n': case 'N': *w++='\n'; r+=2; continue;
                case ',':           *w++=',';  r+=2; continue;
                case ';':           *w++=';';  r+=2; continue;
                case '\\':          *w++='\\'; r+=2; continue;
                default:            *w++=*r++; continue;
            }
        }
        *w++ = *r++;
    }
    *w = 0;
}

static void upper_in_place(char *z){
    for(; *z; z++) if( *z>='a' && *z<='z' ) *z = (char)(*z - 'a' + 'A');
}

typedef struct { char *zName, *zParams, *zValue; } IcsProp;

/* THE COLON THAT ENDS THE PARAMETERS IS NOT THE FIRST COLON. A quoted CN may
** contain one and every mailto: does, so quote state has to be tracked or a
** line like ORGANIZER;CN="A:B":mailto:x@y splits in the wrong place. */
static int ics_split_line(char *zLine, IcsProp *p){
    char *q = zLine; int inQuote = 0;
    char *colon = 0, *semi = 0;
    for( ; *q; q++ ){
        if( *q=='"' ){ inQuote = !inQuote; continue; }
        if( inQuote ) continue;
        if( *q==';' && !semi ) semi = q;
        if( *q==':' ){ colon = q; break; }
    }
    if( !colon ) return 1;
    *colon = 0;
    p->zValue = colon + 1;
    if( semi && semi < colon ){ *semi = 0; p->zParams = semi + 1; }
    else                      { p->zParams = (char*)""; }
    p->zName = zLine;
    upper_in_place(p->zName);
    return 0;
}

/* One parameter's value, quotes stripped, case-insensitive name. Caller frees. */
static char *ics_param(const char *zParams, const char *zWant){
    size_t nWant = strlen(zWant);
    const char *p = zParams;
    while( p && *p ){
        const char *eq = strchr(p, '=');
        const char *end;
        int inQ = 0;
        if( !eq ) break;
        for( end = eq+1; *end; end++ ){
            if( *end=='"' ){ inQ = !inQ; continue; }
            if( *end==';' && !inQ ) break;
        }
        if( (size_t)(eq-p)==nWant && strncasecmp(p, zWant, nWant)==0 ){
            const char *v = eq+1; size_t n = (size_t)(end-v);
            char *z;
            if( n>=2 && v[0]=='"' && v[n-1]=='"' ){ v++; n -= 2; }
            z = (char*)malloc(n+1);
            if( !z ) return 0;
            memcpy(z, v, n); z[n] = 0;
            return z;
        }
        p = *end ? end+1 : end;
    }
    return 0;
}

/* The four forms. A value ending Z is UTC; a TZID makes it zoned; VALUE=DATE
** is a date; anything else floats -- and a floating time is NOT a UTC time,
** which is why no offset is ever stored. */
static const char *ics_form(const char *zValue, const char *zTzid, const char *zVtype){
    size_t n = zValue ? strlen(zValue) : 0;
    if( zVtype && strcasecmp(zVtype, "DATE")==0 ) return "date";
    if( n && zValue[n-1]=='Z' ) return "utc";
    if( zTzid && zTzid[0] ) return "zoned";
    return "floating";
}

/* ---- the assertion subtype ------------------------------------------- */
static const VikiType vikiEventType = { "event", 0, sizeof(VikiEvent) };

static const char *evKey (const VikiAssert *p){ return ((const VikiEvent*)p)->zKeyBuf; }
static const char *evRank(const VikiAssert *p){ return ((const VikiEvent*)p)->zRankBuf; }
static const char *evText(const VikiAssert *p){
    const VikiEvent *e = (const VikiEvent*)p;
    return e->zText ? e->zText : "";
}
static const char *evCanon(const VikiAssert *p){
    const VikiEvent *e = (const VikiEvent*)p;
    return e->zRaw ? e->zRaw : "";
}
const struct VikiAssertVftbl vikiEventVftbl = {
    &vikiEventType, evKey, evRank, evText, evCanon
};

/* RFC 5546 SS 3.2 PRECEDENCE, AS A LEXICAL SORT KEY.
**
** "highest SEQUENCE wins; DTSTAMP breaks the tie" becomes one comparable
** string, so core's single viki_current() statement -- shared with notes --
** implements the calendar rule without knowing it exists. SEQUENCE is
** zero-padded to ten digits because it is an unbounded integer in the RFC
** and "10" must not sort below "9". */
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
  "  id TEXT PRIMARY KEY,"          /* the assertion id                     */
  "  uid TEXT NOT NULL, recurrence_id TEXT NOT NULL,"
  "  sequence INTEGER NOT NULL, dtstamp TEXT,"
  "  component TEXT, summary TEXT, status TEXT, transp TEXT, organizer TEXT,"
  "  dtstart TEXT, dtstart_tzid TEXT, dtstart_form TEXT,"
  "  dtend TEXT, dtend_tzid TEXT, dtend_form TEXT,"
  "  due TEXT, due_tzid TEXT, due_form TEXT,"
  "  duration TEXT, rrule TEXT, source TEXT,"
  /* NORMALISED start, for range queries only. Never displayed, never
  ** synced, and never mistaken for the stored time -- see cal_norm_bound(). */
  "  sortkey TEXT"
  ") WITHOUT ROWID;"
  "CREATE INDEX IF NOT EXISTS viki_event_sort ON viki_event(sortkey);"
  "CREATE INDEX IF NOT EXISTS viki_event_uid  ON viki_event(uid, recurrence_id);";

VikiStatus viki_cal_attach(sqlite3 *db){
    char *zErr = 0;
    if( !db ) return VIKI_EINVAL;
    if( sqlite3_exec(db, zCalSchema, 0, 0, &zErr)!=SQLITE_OK ){
        sqlite3_free(zErr);
        return VIKI_ESQL;
    }
    return VIKI_OK;
}

/* RANGE BOUNDS MUST BE COMPARED IN ONE FORM.
**
** Stored times are RFC 5545 basic ("20260901T150000"); a caller naturally
** writes ISO ("2026-09-01T15:00"). '-' sorts below every digit, so comparing
** them directly returns NOTHING for a window that plainly contains events --
** and an empty day looks like a quiet day, which is the failure mode a
** calendar can least afford. Strip separators, and widen a bare date to cover
** the whole day rather than its first instant. */
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

static void ev_project(sqlite3 *db, const VikiEvent *e){
    sqlite3_stmt *st = 0;
    char zSort[64];
    const char *zStart = e->zDtstart ? e->zDtstart : (e->zDue ? e->zDue : "");
    cal_norm_bound(zStart, 0, zSort, sizeof(zSort));
    if( sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO viki_event(id,uid,recurrence_id,sequence,dtstamp,"
        " component,summary,status,transp,organizer,"
        " dtstart,dtstart_tzid,dtstart_form,dtend,dtend_tzid,dtend_form,"
        " due,due_tzid,due_form,duration,rrule,source,sortkey)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,"
        "        ?17,?18,?19,?20,?21,?22,?23)", -1, &st, 0)!=SQLITE_OK ) return;
    #define B(i,z) sqlite3_bind_text(st,(i),(z)?(z):"",-1,SQLITE_TRANSIENT)
    B(1,e->zId); B(2,e->zUid); B(3,e->zRecurrenceId);
    sqlite3_bind_int(st,4,e->iSequence);
    B(5,e->zDtstamp); B(6,e->zComponent); B(7,e->zSummary); B(8,e->zStatus);
    B(9,e->zTransp); B(10,e->zOrganizer);
    B(11,e->zDtstart); B(12,e->zDtstartTzid); B(13,e->zDtstartForm);
    B(14,e->zDtend);   B(15,e->zDtendTzid);   B(16,e->zDtendForm);
    B(17,e->zDue);     B(18,e->zDueTzid);     B(19,e->zDueForm);
    B(20,e->zDuration); B(21,e->zRrule); B(22,e->zSource); B(23,zSort);
    #undef B
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* ---- the shredder ---------------------------------------------------- */
static sqlite3 *cal_db(void){
    const VikiStore *p;
    if( !RETAINED(VikiStore) ) return 0;
    p = RECALL(VikiStore);
    return (p && p->db) ? p->db : 0;
}

typedef struct {
    char *zUid, *zRecurrenceId, *zDtstamp, *zSummary, *zDescription;
    char *zStatus, *zTransp, *zOrganizer, *zRrule, *zDuration;
    char *zDtstart, *zDtstartTzid, *zDtend, *zDtendTzid, *zDue, *zDueTzid;
    const char *zDtstartForm, *zDtendForm, *zDueForm;
    int   iSequence;
} Acc;

static void acc_free(Acc *a){
    free(a->zUid); free(a->zRecurrenceId); free(a->zDtstamp); free(a->zSummary);
    free(a->zDescription); free(a->zStatus); free(a->zTransp); free(a->zOrganizer);
    free(a->zRrule); free(a->zDuration);
    free(a->zDtstart); free(a->zDtstartTzid);
    free(a->zDtend); free(a->zDtendTzid); free(a->zDue); free(a->zDueTzid);
    memset(a, 0, sizeof(*a));
    a->iSequence = 0;
}
static char *dupz(const char *z){ char *r; if(!z) return 0; r=(char*)malloc(strlen(z)+1); if(r) strcpy(r,z); return r; }

VikiStatus viki_cal_shred(const char *zIcs, size_t nIcs,
                          const char *zSource, int *pnAdded){
    sqlite3 *db = cal_db();
    char *zAll, *zLine, *zSave = 0, *zRawStart = 0;
    int nAdded = 0, nSkip = 0, inComp = 0;
    Acc acc;
    char *zComponent = 0;

    if( pnAdded ) *pnAdded = 0;
    if( !db ) return VIKI_ENOCTX;
    if( !zIcs ) return VIKI_EINVAL;
    /* AN ADAPTER THAT FETCHED AN HTML ERROR PAGE MUST NOT READ AS A QUIET DAY.
    ** Refusing input with no BEGIN:VCALENDAR is the difference between "your
    ** calendar is empty" and "I could not read your calendar", and only the
    ** second is true. */
    if( !strstr(zIcs, "BEGIN:VCALENDAR") && !strstr(zIcs, "begin:vcalendar") )
        return VIKI_EINVAL;

    zAll = ics_unfold(zIcs, nIcs);
    if( !zAll ) return VIKI_ENOMEM;
    memset(&acc, 0, sizeof(acc));

    for( zLine = strtok_r(zAll, "\n", &zSave); zLine;
         zLine = strtok_r(0, "\n", &zSave) ){
        IcsProp pr;
        char *zCopy;
        size_t nL = strlen(zLine);
        while( nL && (zLine[nL-1]=='\r' || zLine[nL-1]==' ') ) zLine[--nL] = 0;
        if( !nL ) continue;
        zCopy = dupz(zLine);
        if( !zCopy ) continue;
        if( ics_split_line(zCopy, &pr) ){ free(zCopy); continue; }

        if( strcmp(pr.zName, "BEGIN")==0 ){
            upper_in_place(pr.zValue);
            if( inComp ){
                /* NESTED COMPONENT -- a VALARM's DURATION and ATTENDEE are the
                ** ALARM's, not the meeting's. Without this a one-hour
                ** appointment reads as fifteen minutes. */
                nSkip++;
            }else if( strcmp(pr.zValue,"VEVENT")==0 || strcmp(pr.zValue,"VTODO")==0 ){
                inComp = 1;
                free(zComponent); zComponent = dupz(pr.zValue);
                acc_free(&acc);
                zRawStart = zSave;    /* body begins after this line */
            }
            free(zCopy); continue;
        }
        if( strcmp(pr.zName, "END")==0 ){
            if( nSkip > 0 ){ nSkip--; free(zCopy); continue; }
            if( inComp ){
                VikiEvent e;
                memset(&e, 0, sizeof(e));
                e.vftbl = &vikiEventVftbl;
                e.zUid = acc.zUid ? acc.zUid : "";
                e.zRecurrenceId = acc.zRecurrenceId ? acc.zRecurrenceId : "";
                e.iSequence = acc.iSequence;
                e.zDtstamp = acc.zDtstamp; e.zComponent = zComponent;
                e.zSummary = acc.zSummary; e.zStatus = acc.zStatus;
                e.zTransp = acc.zTransp;   e.zOrganizer = acc.zOrganizer;
                e.zDtstart = acc.zDtstart; e.zDtstartTzid = acc.zDtstartTzid;
                e.zDtstartForm = acc.zDtstartForm;
                e.zDtend = acc.zDtend; e.zDtendTzid = acc.zDtendTzid;
                e.zDtendForm = acc.zDtendForm;
                e.zDue = acc.zDue; e.zDueTzid = acc.zDueTzid; e.zDueForm = acc.zDueForm;
                e.zDuration = acc.zDuration; e.zRrule = acc.zRrule;
                e.zSource = zSource;
                e.zTs = acc.zDtstamp;
                /* canon() is the SUMMARY+identity tuple rather than the raw
                ** bytes: two feeds emitting the same event with different
                ** property ORDER are the same assertion, and identity that
                ** depended on byte order would store both. */
                {
                    char *zRaw = sqlite3_mprintf(
                        "%s\x1f%s\x1f%d\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s",
                        e.zUid, e.zRecurrenceId, e.iSequence,
                        e.zDtstamp?e.zDtstamp:"", e.zComponent?e.zComponent:"",
                        e.zSummary?e.zSummary:"", e.zDtstart?e.zDtstart:"",
                        e.zDtstartTzid?e.zDtstartTzid:"",
                        e.zDtend?e.zDtend:"", e.zRrule?e.zRrule:"");
                    char *zTx = sqlite3_mprintf("%s%s%s",
                        e.zSummary?e.zSummary:"",
                        (e.zSummary&&acc.zDescription)?"\n\n":"",
                        acc.zDescription?acc.zDescription:"");
                    e.zRaw = zRaw; e.zText = zTx;
                    ev_key(&e); ev_rank(&e);
                    if( viki_put((VikiAssert*)&e)==VIKI_OK ){
                        ev_project(db, &e);
                        nAdded++;
                    }
                    sqlite3_free(zRaw); sqlite3_free(zTx);
                }
                inComp = 0;
                acc_free(&acc);
            }
            free(zCopy); continue;
        }
        if( !inComp || nSkip > 0 ){ free(zCopy); continue; }

        {   /* a property of the component itself */
            char *zTzid = ics_param(pr.zParams, "TZID");
            char *zVal  = ics_param(pr.zParams, "VALUE");
            ics_unescape(pr.zValue);
            if     ( !strcmp(pr.zName,"UID")          ){ free(acc.zUid); acc.zUid = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"RECURRENCE-ID")){ free(acc.zRecurrenceId); acc.zRecurrenceId = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"DTSTAMP")      ){ free(acc.zDtstamp); acc.zDtstamp = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"SUMMARY")      ){ free(acc.zSummary); acc.zSummary = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"DESCRIPTION")  ){ free(acc.zDescription); acc.zDescription = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"STATUS")       ){ free(acc.zStatus); acc.zStatus = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"TRANSP")       ){ free(acc.zTransp); acc.zTransp = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"ORGANIZER")    ){ free(acc.zOrganizer); acc.zOrganizer = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"RRULE")        ){ free(acc.zRrule); acc.zRrule = dupz(pr.zValue); }
            else if( !strcmp(pr.zName,"DURATION")     ){ free(acc.zDuration); acc.zDuration = dupz(pr.zValue); }
            /* SEQUENCE via strtol, not atoi: a malformed value must not
            ** silently become 0 and win a precedence contest it should lose. */
            else if( !strcmp(pr.zName,"SEQUENCE")     ){
                char *zEnd = 0; long v = strtol(pr.zValue, &zEnd, 10);
                if( zEnd && zEnd!=pr.zValue && v>=0 ) acc.iSequence = (int)v;
            }
            else if( !strcmp(pr.zName,"DTSTART") ){
                free(acc.zDtstart); acc.zDtstart = dupz(pr.zValue);
                free(acc.zDtstartTzid); acc.zDtstartTzid = dupz(zTzid?zTzid:"");
                acc.zDtstartForm = ics_form(pr.zValue, zTzid, zVal);
            }
            else if( !strcmp(pr.zName,"DTEND") ){
                free(acc.zDtend); acc.zDtend = dupz(pr.zValue);
                free(acc.zDtendTzid); acc.zDtendTzid = dupz(zTzid?zTzid:"");
                acc.zDtendForm = ics_form(pr.zValue, zTzid, zVal);
            }
            else if( !strcmp(pr.zName,"DUE") ){
                free(acc.zDue); acc.zDue = dupz(pr.zValue);
                free(acc.zDueTzid); acc.zDueTzid = dupz(zTzid?zTzid:"");
                acc.zDueForm = ics_form(pr.zValue, zTzid, zVal);
            }
            free(zTzid); free(zVal);
        }
        free(zCopy);
    }
    (void)zRawStart;
    acc_free(&acc);
    free(zComponent);
    free(zAll);
    if( pnAdded ) *pnAdded = nAdded;
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
    /* RESOLUTION HAPPENS HERE, NOT IN STORAGE. One row per (uid,
    ** recurrence_id): the winner is the highest arank among assertions
    ** nothing supersedes -- which is RFC 5546's rule, expressed once, in
    ** core, shared with notes. The losers stay in viki_assert. */
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
        e.zDtend=C(13); e.zDtendTzid=C(14); e.zDtendForm=C(15);
        e.zDue=C(16); e.zDueTzid=C(17); e.zDueForm=C(18);
        e.zDuration=C(19); e.zRrule=C(20); e.zSource=C(21);
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
    /* The projection is DERIVED, so rebuilding is dropping and re-deriving.
    ** Nothing here reads viki_event -- only viki_assert, which is truth. */
    sqlite3_exec(db, "DELETE FROM viki_event", 0, 0, 0);
    if( sqlite3_prepare_v2(db,
        "SELECT id, akey, arank, ts, body FROM viki_assert WHERE kind='event'",
        -1, &st, 0)!=SQLITE_OK ) return VIKI_ESQL;
    while( sqlite3_step(st)==SQLITE_ROW ){
        /* body is the canonical tuple written by the shredder. Re-splitting it
        ** here rather than re-parsing ICS is what keeps the projection
        ** rebuildable from the store ALONE, with no feed to re-fetch. */
        VikiEvent e; char *zB = dupz((const char*)sqlite3_column_text(st,4));
        char *p, *aF[10]; int i = 0;
        if( !zB ) continue;
        for( p = zB; i<10; i++ ){
            char *q = strchr(p, '\x1f');
            aF[i] = p;
            if( !q ) { i++; break; }
            *q = 0; p = q+1;
        }
        while( i<10 ) aF[i++] = (char*)"";
        memset(&e, 0, sizeof(e));
        e.vftbl = &vikiEventVftbl;
        snprintf(e.zId, sizeof(e.zId), "%s", (const char*)sqlite3_column_text(st,0));
        e.zUid=aF[0]; e.zRecurrenceId=aF[1]; e.iSequence=atoi(aF[2]);
        e.zDtstamp=aF[3]; e.zComponent=aF[4]; e.zSummary=aF[5];
        e.zDtstart=aF[6]; e.zDtstartTzid=aF[7]; e.zDtend=aF[8]; e.zRrule=aF[9];
        ev_key(&e); ev_rank(&e);
        ev_project(db, &e);
        free(zB);
        n++;
    }
    sqlite3_finalize(st);
    if( pnRows ) *pnRows = n;
    return VIKI_OK;
}
