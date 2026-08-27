/*
** viki_cal.c -- the ICS shredder. See viki_cal.h for what tier this builds
** and why it stops where it does.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include "viki_cal.h"

/* ------------------------------------------------------------ unfolding --
**
** RFC 5545 SS 3.1: a long content line is split by inserting CRLF followed by
** a single space or htab, and the folding is undone by deleting exactly that
** CRLF+whitespace. It is NOT a general "join continuation lines" rule -- the
** whitespace is part of the fold, not part of the value, and deleting the
** CRLF while keeping the space corrupts every wrapped SUMMARY.
**
** Bare LF is accepted alongside CRLF. Real exporters emit both, and a parser
** that only accepts CRLF fails on files a text editor has touched.
*/
static char *ics_unfold(const char *z, size_t n){
    char *out = malloc(n + 1);
    size_t i = 0, j = 0;
    if( !out ) return NULL;
    while( i < n ){
        if( z[i] == '\r' && i + 1 < n && z[i+1] == '\n' ){
            if( i + 2 < n && (z[i+2] == ' ' || z[i+2] == '\t') ){ i += 3; continue; }
            out[j++] = '\n'; i += 2; continue;
        }
        if( z[i] == '\n' ){
            if( i + 1 < n && (z[i+1] == ' ' || z[i+1] == '\t') ){ i += 2; continue; }
            out[j++] = '\n'; i++; continue;
        }
        out[j++] = z[i++];
    }
    out[j] = 0;
    return out;
}

/* RFC 5545 SS 3.3.11 TEXT escaping, undone in place. Only these four
** sequences are defined; a backslash before anything else is literal, so it
** is copied through rather than silently eaten. */
static void ics_unescape(char *z){
    char *r = z, *w = z;
    while( *r ){
        if( *r == '\\' && r[1] ){
            switch( r[1] ){
                case 'n': case 'N': *w++ = '\n'; r += 2; continue;
                case ',':           *w++ = ',';  r += 2; continue;
                case ';':           *w++ = ';';  r += 2; continue;
                case '\\':          *w++ = '\\'; r += 2; continue;
                default: break;
            }
        }
        *w++ = *r++;
    }
    *w = 0;
}

/* --------------------------------------------------------- one property --
**
** A content line is  NAME *(";" param) ":" value  -- and the colon that ends
** the name+params is NOT simply the first colon in the line: a param value may
** be a quoted string containing one ("ATTENDEE;CN=\"Smith, J: eng\":mailto:..."),
** and the VALUE routinely contains colons (every mailto:, every URL). So the
** scan tracks quoting and stops at the first UNQUOTED colon.
**
** Getting this wrong is not a parse error, it is a WRONG ANSWER: the value
** silently becomes a suffix of itself.
*/
typedef struct {
    char *zName;      /* upper-cased, e.g. "DTSTART" */
    char *zParams;    /* raw, without the leading ';' -- may be "" */
    char *zValue;     /* raw, still escaped */
} IcsProp;

static void upper_in_place(char *z){
    for( ; *z; z++ ) if( *z >= 'a' && *z <= 'z' ) *z = (char)(*z - 'a' + 'A');
}

static int ics_split_line(char *zLine, IcsProp *p){
    char *q = zLine;
    int inQuote = 0;
    char *colon = NULL, *semi = NULL;

    for( ; *q; q++ ){
        if( *q == '"' ){ inQuote = !inQuote; continue; }
        if( inQuote ) continue;
        if( *q == ';' && !semi ) semi = q;
        if( *q == ':' ){ colon = q; break; }
    }
    if( !colon ) return 1;                  /* not a content line */
    *colon = 0;
    p->zValue = colon + 1;
    if( semi && semi < colon ){
        *semi = 0;
        p->zParams = semi + 1;
    }else{
        p->zParams = (char*)"";
    }
    p->zName = zLine;
    upper_in_place(p->zName);
    return 0;
}

/* Value of one parameter, or NULL. Case-insensitive on the name, and the
** value is returned with surrounding quotes stripped. Caller must free. */
static char *ics_param(const char *zParams, const char *zWant){
    size_t nWant = strlen(zWant);
    const char *p = zParams;
    while( p && *p ){
        const char *eq = strchr(p, '=');
        const char *end;
        int inQuote = 0;
        if( !eq ) break;
        for( end = eq + 1; *end; end++ ){
            if( *end == '"' ){ inQuote = !inQuote; continue; }
            if( *end == ';' && !inQuote ) break;
        }
        if( (size_t)(eq - p) == nWant && strncasecmp(p, zWant, nWant) == 0 ){
            const char *v = eq + 1;
            size_t n;
            char *out;
            if( *v == '"' ){ v++; n = (size_t)(end - v); if( n && v[n-1] == '"' ) n--; }
            else n = (size_t)(end - v);
            out = malloc(n + 1);
            if( !out ) return NULL;
            memcpy(out, v, n); out[n] = 0;
            return out;
        }
        p = (*end == ';') ? end + 1 : end;
    }
    return NULL;
}

/* T3: the four forms, decided from the value and its params -- never by
** computing an offset (T2). A trailing Z is UTC; a TZID names a zone by
** reference (T1); VALUE=DATE is a date; anything else is floating. */
static const char *ics_form(const char *zValue, const char *zTzid, const char *zVtype){
    size_t n = zValue ? strlen(zValue) : 0;
    if( zVtype && strcasecmp(zVtype, "DATE") == 0 ) return "date";
    if( n && zValue[n-1] == 'Z' ) return "utc";
    if( zTzid && zTzid[0] ) return "zoned";
    return "floating";
}

static void iso_now(char *zOut, size_t n){
    time_t t = time(NULL);
    struct tm g;
#if defined(_WIN32)
    g = *gmtime(&t);
#else
    gmtime_r(&t, &g);
#endif
    strftime(zOut, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

/* ------------------------------------------------------------ the shred -- */

#define CAL_MAX_ATT 64

typedef struct {
    char *zUid, *zRecur, *zDtstamp, *zSummary, *zStatus, *zTransp, *zOrganizer;
    char *zRrule, *zDuration;
    char *zStart, *zStartTz; const char *zStartForm;
    char *zEnd,   *zEndTz;   const char *zEndForm;
    char *zDue,   *zDueTz;   const char *zDueForm;
    int   iSeq;
    char *azAtt[CAL_MAX_ATT];
    char *azPart[CAL_MAX_ATT];
    char *azRole[CAL_MAX_ATT];
    int   nAtt;
} CalComp;

static void comp_free(CalComp *c){
    int i;
    free(c->zUid); free(c->zRecur); free(c->zDtstamp); free(c->zSummary);
    free(c->zStatus); free(c->zTransp); free(c->zOrganizer);
    free(c->zRrule); free(c->zDuration);
    free(c->zStart); free(c->zStartTz);
    free(c->zEnd); free(c->zEndTz);
    free(c->zDue); free(c->zDueTz);
    for( i = 0; i < c->nAtt; i++ ){ free(c->azAtt[i]); free(c->azPart[i]); free(c->azRole[i]); }
    memset(c, 0, sizeof(*c));
}

static char *dupz(const char *z){
    size_t n = strlen(z) + 1;
    char *o = malloc(n);
    if( o ) memcpy(o, z, n);
    return o;
}

static void bind_or_null(sqlite3_stmt *st, int ix, const char *z){
    if( z && z[0] ) sqlite3_bind_text(st, ix, z, -1, SQLITE_TRANSIENT);
    else            sqlite3_bind_null(st, ix);
}

static int comp_write(sqlite3 *db, CalComp *c, const char *zComponent,
                      const char *zSource, const char *zNow,
                      int *pnEvents, int *pnAtt){
    static const char *zIns =
        "INSERT OR IGNORE INTO cal_event(uid,recurrence_id,sequence,dtstamp,component,"
        " dtstart,dtstart_tzid,dtstart_form,dtend,dtend_tzid,dtend_form,duration,"
        " due,due_tzid,due_form,rrule,summary,status,transp,organizer,source,ingested)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21,?22)";
    static const char *zAtt =
        "INSERT OR IGNORE INTO cal_attendee(uid,recurrence_id,sequence,dtstamp,"
        " attendee,partstat,role) VALUES(?1,?2,?3,?4,?5,?6,?7)";
    sqlite3_stmt *st = NULL;
    int i, rc;

    /* NO UID, NO ROW, and this is a refusal rather than a repair. RFC 5545
    ** requires UID on VEVENT and VTODO; without one there is no identity, so
    ** RFC 5546 resolution cannot run and a synthesised key would make two
    ** unrelated components collide or the same one duplicate on re-ingest. */
    if( !c->zUid || !c->zUid[0] ) return 0;

    if( sqlite3_prepare_v2(db, zIns, -1, &st, NULL) != SQLITE_OK ) return 1;
    sqlite3_bind_text(st, 1, c->zUid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c->zRecur ? c->zRecur : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 3, c->iSeq);
    sqlite3_bind_text(st, 4, c->zDtstamp ? c->zDtstamp : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, zComponent, -1, SQLITE_STATIC);
    bind_or_null(st, 6,  c->zStart);
    bind_or_null(st, 7,  c->zStartTz);
    bind_or_null(st, 8,  c->zStart ? c->zStartForm : NULL);
    bind_or_null(st, 9,  c->zEnd);
    bind_or_null(st, 10, c->zEndTz);
    bind_or_null(st, 11, c->zEnd ? c->zEndForm : NULL);
    bind_or_null(st, 12, c->zDuration);
    bind_or_null(st, 13, c->zDue);
    bind_or_null(st, 14, c->zDueTz);
    bind_or_null(st, 15, c->zDue ? c->zDueForm : NULL);
    bind_or_null(st, 16, c->zRrule);
    bind_or_null(st, 17, c->zSummary);
    bind_or_null(st, 18, c->zStatus);
    bind_or_null(st, 19, c->zTransp);
    bind_or_null(st, 20, c->zOrganizer);
    sqlite3_bind_text(st, 21, zSource, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 22, zNow, -1, SQLITE_STATIC);
    rc = sqlite3_step(st);
    if( rc == SQLITE_DONE && sqlite3_changes(db) > 0 && pnEvents ) (*pnEvents)++;
    sqlite3_finalize(st);
    if( rc != SQLITE_DONE ) return 1;

    for( i = 0; i < c->nAtt; i++ ){
        if( sqlite3_prepare_v2(db, zAtt, -1, &st, NULL) != SQLITE_OK ) continue;
        sqlite3_bind_text(st, 1, c->zUid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, c->zRecur ? c->zRecur : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st, 3, c->iSeq);
        sqlite3_bind_text(st, 4, c->zDtstamp ? c->zDtstamp : "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, c->azAtt[i], -1, SQLITE_TRANSIENT);
        bind_or_null(st, 6, c->azPart[i]);
        bind_or_null(st, 7, c->azRole[i]);
        if( sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(db) > 0 && pnAtt ) (*pnAtt)++;
        sqlite3_finalize(st);
    }
    return 0;
}

int viki_cal_shred(sqlite3 *db, const char *zText, size_t nText,
                   const char *zSource, int *pnEvents, int *pnAttendees){
    char *zFlat, *zLine, *zNext;
    char zNow[32];
    CalComp c;
    char zComponent[16];
    int inComp = 0, rc = 0;

    if( !zText ) return 1;
    zFlat = ics_unfold(zText, nText);
    if( !zFlat ) return 1;
    iso_now(zNow, sizeof(zNow));
    memset(&c, 0, sizeof(c));
    zComponent[0] = 0;

    for( zLine = zFlat; zLine && *zLine; zLine = zNext ){
        IcsProp p;
        zNext = strchr(zLine, '\n');
        if( zNext ){ *zNext = 0; zNext++; }
        while( *zLine == ' ' || *zLine == '\t' ) zLine++;
        if( !*zLine ) continue;
        if( ics_split_line(zLine, &p) != 0 ) continue;

        if( strcmp(p.zName, "BEGIN") == 0 ){
            upper_in_place(p.zValue);
            /* Only the two components SS 5 says want projections. VALARM,
            ** VTIMEZONE and VFREEBUSY are skipped rather than half-stored:
            ** VTIMEZONE especially, because T1 says zones travel by reference
            ** and a stored VTIMEZONE is the thing that rule exists to avoid. */
            if( strcmp(p.zValue, "VEVENT") == 0 || strcmp(p.zValue, "VTODO") == 0 ){
                comp_free(&c);
                snprintf(zComponent, sizeof(zComponent), "%s", p.zValue);
                inComp = 1;
            }
            continue;
        }
        if( strcmp(p.zName, "END") == 0 ){
            upper_in_place(p.zValue);
            if( inComp && strcmp(p.zValue, zComponent) == 0 ){
                if( comp_write(db, &c, zComponent, zSource, zNow, pnEvents, pnAttendees) ) rc = 1;
                comp_free(&c);
                inComp = 0;
            }
            continue;
        }
        if( !inComp ) continue;

        if( strcmp(p.zName, "UID") == 0 && !c.zUid ){
            c.zUid = dupz(p.zValue); if( c.zUid ) ics_unescape(c.zUid);
        }else if( strcmp(p.zName, "RECURRENCE-ID") == 0 && !c.zRecur ){
            c.zRecur = dupz(p.zValue);
        }else if( strcmp(p.zName, "SEQUENCE") == 0 ){
            c.iSeq = atoi(p.zValue);
        }else if( strcmp(p.zName, "DTSTAMP") == 0 && !c.zDtstamp ){
            c.zDtstamp = dupz(p.zValue);
        }else if( strcmp(p.zName, "SUMMARY") == 0 && !c.zSummary ){
            c.zSummary = dupz(p.zValue); if( c.zSummary ) ics_unescape(c.zSummary);
        }else if( strcmp(p.zName, "STATUS") == 0 && !c.zStatus ){
            c.zStatus = dupz(p.zValue); if( c.zStatus ) upper_in_place(c.zStatus);
        }else if( strcmp(p.zName, "TRANSP") == 0 && !c.zTransp ){
            c.zTransp = dupz(p.zValue); if( c.zTransp ) upper_in_place(c.zTransp);
        }else if( strcmp(p.zName, "ORGANIZER") == 0 && !c.zOrganizer ){
            c.zOrganizer = dupz(p.zValue);
        }else if( strcmp(p.zName, "RRULE") == 0 && !c.zRrule ){
            c.zRrule = dupz(p.zValue);
        }else if( strcmp(p.zName, "DURATION") == 0 && !c.zDuration ){
            c.zDuration = dupz(p.zValue);
        }else if( strcmp(p.zName, "DTSTART") == 0 && !c.zStart ){
            char *tz = ics_param(p.zParams, "TZID");
            char *vt = ics_param(p.zParams, "VALUE");
            c.zStart = dupz(p.zValue); c.zStartTz = tz;
            c.zStartForm = ics_form(p.zValue, tz, vt);
            free(vt);
        }else if( strcmp(p.zName, "DTEND") == 0 && !c.zEnd ){
            char *tz = ics_param(p.zParams, "TZID");
            char *vt = ics_param(p.zParams, "VALUE");
            c.zEnd = dupz(p.zValue); c.zEndTz = tz;
            c.zEndForm = ics_form(p.zValue, tz, vt);
            free(vt);
        }else if( strcmp(p.zName, "DUE") == 0 && !c.zDue ){
            char *tz = ics_param(p.zParams, "TZID");
            char *vt = ics_param(p.zParams, "VALUE");
            c.zDue = dupz(p.zValue); c.zDueTz = tz;
            c.zDueForm = ics_form(p.zValue, tz, vt);
            free(vt);
        }else if( strcmp(p.zName, "ATTENDEE") == 0 && c.nAtt < CAL_MAX_ATT ){
            c.azAtt[c.nAtt]  = dupz(p.zValue);
            c.azPart[c.nAtt] = ics_param(p.zParams, "PARTSTAT");
            c.azRole[c.nAtt] = ics_param(p.zParams, "ROLE");
            if( c.azPart[c.nAtt] ) upper_in_place(c.azPart[c.nAtt]);
            c.nAtt++;
        }
    }
    comp_free(&c);
    free(zFlat);
    return rc;
}

/* ------------------------------------------------------------- commands -- */

static char *read_all(const char *zPath, size_t *pn){
    FILE *f = fopen(zPath, "rb");
    char *z; long n;
    if( !f ) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if( n < 0 ){ fclose(f); return NULL; }
    z = malloc((size_t)n + 1);
    if( !z ){ fclose(f); return NULL; }
    *pn = fread(z, 1, (size_t)n, f);
    z[*pn] = 0;
    fclose(f);
    return z;
}

int viki_cmd_cal_shred(sqlite3 *db, int argc, char **argv){
    const char *zFile = NULL, *zSource = NULL;
    char *zText;
    size_t nText = 0;
    int i, nEv = 0, nAtt = 0, rc;

    for( i = 3; i < argc; i++ ){
        if( strcmp(argv[i], "--source") == 0 && i + 1 < argc ) zSource = argv[++i];
        else if( argv[i][0] == '-' && argv[i][1] == '-' ){
            fprintf(stderr, "viki calendar shred: unknown option '%s'\n", argv[i]);
            return 1;
        }
        else zFile = argv[i];
    }
    if( !zFile ){
        fprintf(stderr, "usage: viki calendar shred <file.ics> [--source NAME]\n");
        return 1;
    }
    zText = read_all(zFile, &nText);
    if( !zText ){
        fprintf(stderr, "viki calendar shred: cannot read '%s'\n", zFile);
        return 1;
    }
    /* A file that is not iCalendar is REFUSED, not shredded into zero rows.
    ** "0 events" from an HTML error page an adapter fetched instead of a feed
    ** is indistinguishable from an empty calendar, and the empty-calendar
    ** reading is the one that produces a quiet, wrong day. */
    if( !strstr(zText, "BEGIN:VCALENDAR") && !strstr(zText, "begin:vcalendar") ){
        fprintf(stderr,
            "viki calendar shred: '%s' has no BEGIN:VCALENDAR -- refusing.\n"
            "viki calendar shred:   An empty calendar and a fetch that returned\n"
            "viki calendar shred:   something else must not read the same.\n", zFile);
        free(zText);
        return 1;
    }
    rc = viki_cal_shred(db, zText, nText, zSource ? zSource : zFile, &nEv, &nAtt);
    free(zText);
    if( rc ){
        fprintf(stderr, "viki calendar shred: failed while writing assertions\n");
        return 1;
    }
    fprintf(stderr, "viki calendar shred: %d new assertion(s), %d attendee row(s) from '%s'\n",
            nEv, nAtt, zSource ? zSource : zFile);
    if( nEv == 0 )
        fprintf(stderr, "viki calendar shred:   0 new -- every component was already asserted,\n"
                        "viki calendar shred:   or the calendar holds no VEVENT/VTODO.\n");
    return 0;
}

int viki_cmd_cal_events(sqlite3 *db, int argc, char **argv){
    /* RFC 5546 RESOLUTION AS A READ-TIME PROJECTION. A row wins its
    ** (uid, recurrence_id) when nothing else with the same identity has a
    ** higher SEQUENCE, or an equal SEQUENCE and a later DTSTAMP. Superseded
    ** assertions stay in the table -- the same shape as viki_note, and what
    ** keeps this grow-only rather than latest-wins (SYNC.md SS 2). */
    static const char *zSel =
        /* coalesce for DISPLAY as well as for ordering: SS 5 says a VTODO with
        ** DUE and no DTSTART is a POINT, not an interval, so its DUE is its
        ** time. Printing "(no start)" for a deadline that has one is the
        ** display telling a reader the row is empty when it is not. The
        ** `is_due` column keeps the two distinguishable rather than blurring
        ** an interval's start into a deadline. */
        "SELECT coalesce(e.dtstart, e.due) AS t,"
        "       coalesce(e.dtstart_tzid, e.due_tzid),"
        "       coalesce(e.dtstart_form, e.due_form),"
        "       (e.dtstart IS NULL AND e.due IS NOT NULL) AS is_due,"
        "       e.duration,"
        "       e.summary, e.status, e.transp, e.component, e.uid, e.recurrence_id,"
        "       e.sequence, e.source"
        "  FROM cal_event e"
        " WHERE NOT EXISTS ("
        "   SELECT 1 FROM cal_event b"
        "    WHERE b.uid = e.uid AND b.recurrence_id = e.recurrence_id"
        "      AND (b.sequence > e.sequence"
        "        OR (b.sequence = e.sequence AND b.dtstamp > e.dtstamp)))"
        "   AND (?1 = '' OR coalesce(e.dtstart, e.due) >= ?1)"
        "   AND (?2 = '' OR coalesce(e.dtstart, e.due) <= ?2)"
        " ORDER BY coalesce(e.dtstart, e.due, '~'), e.summary";
    sqlite3_stmt *st;
    const char *zFrom = "", *zTo = "";
    int i, bJson = 0, n = 0, nSuperseded = 0;

    for( i = 3; i < argc; i++ ){
        if( strcmp(argv[i], "--from") == 0 && i + 1 < argc ) zFrom = argv[++i];
        else if( strcmp(argv[i], "--to") == 0 && i + 1 < argc ) zTo = argv[++i];
        else if( strcmp(argv[i], "--json") == 0 ) bJson = 1;
        else if( strcmp(argv[i], "--all") == 0 ) { zFrom = ""; zTo = ""; }
        else { fprintf(stderr, "viki calendar events: unknown option '%s'\n", argv[i]); return 1; }
    }
    if( sqlite3_prepare_v2(db, zSel, -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki calendar events: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(st, 1, zFrom, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, zTo, -1, SQLITE_STATIC);

    if( bJson ) printf("[");
    while( sqlite3_step(st) == SQLITE_ROW ){
        const unsigned char *zStart = sqlite3_column_text(st, 0);
        const unsigned char *zTz    = sqlite3_column_text(st, 1);
        const unsigned char *zForm  = sqlite3_column_text(st, 2);
        int bDue                    = sqlite3_column_int(st, 3);
        const unsigned char *zSum   = sqlite3_column_text(st, 5);
        const unsigned char *zStat  = sqlite3_column_text(st, 6);
        const unsigned char *zTr    = sqlite3_column_text(st, 7);
        const unsigned char *zComp  = sqlite3_column_text(st, 8);
        if( bJson ){
            printf("%s{\"component\":\"%s\",\"uid\":\"%s\",\"start\":\"%s\","
                   "\"tzid\":\"%s\",\"form\":\"%s\",\"summary\":\"%s\","
                   "\"status\":\"%s\",\"transp\":\"%s\"}",
                   n ? "," : "", zComp ? (const char*)zComp : "",
                   (const char*)sqlite3_column_text(st, 9),
                   zStart ? (const char*)zStart : "", zTz ? (const char*)zTz : "",
                   zForm ? (const char*)zForm : "", zSum ? (const char*)zSum : "",
                   zStat ? (const char*)zStat : "", zTr ? (const char*)zTr : "");
        }else{
            /* The TZID is printed and never resolved. T1/T2: the zone is the
            ** fact; an offset is a computed value with a shelf life, and this
            ** tier does not compute it. */
            printf("%-20s %-18s %-8s %s%s%s%s\n",
                   zStart ? (const char*)zStart : "(no time)",
                   zTz ? (const char*)zTz : "",
                   zForm ? (const char*)zForm : "",
                   zSum ? (const char*)zSum : "(no summary)",
                   bDue ? "  [due]" : "",
                   (zStat && strcmp((const char*)zStat, "CANCELLED") == 0) ? "  [CANCELLED]" : "",
                   (zTr && strcmp((const char*)zTr, "TRANSPARENT") == 0) ? "  [TRANSPARENT]" : "");
        }
        n++;
    }
    sqlite3_finalize(st);
    if( bJson ){ printf("]\n"); return 0; }

    /* SAY WHAT WAS SET ASIDE. viki coverage's rule: report the fact, judge
    ** nothing. A reader who sees 3 events and is not told 5 assertions were
    ** superseded cannot tell a quiet calendar from a resolution bug. */
    if( sqlite3_prepare_v2(db,
            "SELECT count(*) FROM cal_event e WHERE EXISTS ("
            " SELECT 1 FROM cal_event b WHERE b.uid=e.uid AND b.recurrence_id=e.recurrence_id"
            "   AND (b.sequence>e.sequence OR (b.sequence=e.sequence AND b.dtstamp>e.dtstamp)))",
            -1, &st, NULL) == SQLITE_OK ){
        if( sqlite3_step(st) == SQLITE_ROW ) nSuperseded = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    fprintf(stderr, "\n%d current, %d superseded assertion(s) retained.\n", n, nSuperseded);
    fprintf(stderr, "  Times are AS WRITTEN with their TZID; no offset is computed here.\n");
    if( n == 0 )
        fprintf(stderr, "  Nothing in range -- and nothing here decides what 'free' means.\n");
    return 0;
}
