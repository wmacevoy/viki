#include "viki_note.h"
#include "viki_grep.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

/* A capture block, as it sits in the file and in the projection. */
typedef struct {
    char id[40];
    char ts[32];        /* ISO-8601 UTC, so lexicographic order IS time order */
    char type[32];
    char place[64];
    char who[64];
    char due[32];
    char state[16];
    char closes[40];
    char claimed[32];
    char lease[32];
    char challenge[96];
    char stolen[64];
    char text[1024];
} viki_note;

static void note_clear(viki_note *p){ memset(p, 0, sizeof(*p)); }

/* The controlled @type vocabulary, derived from real captured notes rather
** than invented -- see AGENTS.md's "Structuring captures" section for what
** each one means and why the distinctions earn their keep.
**
** Checked but NOT enforced. An unknown type still projects and is still
** queryable: a capture must never be rejected for using a word nobody
** anticipated, and a person's own vocabulary is evidence about what this
** system is missing. The warning exists so a fleet of agents converges on
** one spelling instead of quietly inventing "todo", "task-item" and
** "actionable" in three sessions -- which is the failure this file is
** guarding against, not incorrect input. */
static const char *VIKI_NOTE_TYPES[] = {
    "task",        /* something to DO. The only type carrying open/closed state. */
    "observation", /* something NOTICED. Never a chore -- `ask` returning "new foal
                   ** in rimi's band" under "what needs to be done" is the exact
                   ** false positive the capture loop exists to prevent. */
    "rule",        /* a standing CONSTRAINT that should modify future action
                   ** ("renee cannot lift heavy items"), not merely be retrievable. */
    "schedule",    /* a RECURRENCE ("thursdays and sundays, except labor day"). */
    "alert",       /* time-bounded WARNING with an implied action
                   ** ("rain washout makes MR impassible -- warn all drivers"). */
    "question",    /* the capturer ASKING. Two of sixteen real notes were questions;
                   ** they need an answer, not a doer, and typing one as a task
                   ** puts it on somebody's chore list forever. */
    "note",        /* honest fallback: recorded, no action semantics claimed. Use it
                   ** rather than guessing, but prefer a real type when the text
                   ** supports one. */
    NULL
};

static int type_is_known(const char *z){
    int i;
    if( !z || !z[0] ) return 1;   /* absent is "pending", not "wrong" */
    for( i = 0; VIKI_NOTE_TYPES[i]; i++ ) if( strcmp(z, VIKI_NOTE_TYPES[i]) == 0 ) return 1;
    return 0;
}

static void warn_unknown_type(const char *zType, const char *zWhere){
    int i;
    if( type_is_known(zType) ) return;
    fprintf(stderr, "viki: note type '%s' (%s) is outside the known set:", zType, zWhere);
    for( i = 0; VIKI_NOTE_TYPES[i]; i++ ) fprintf(stderr, " %s", VIKI_NOTE_TYPES[i]);
    fprintf(stderr, "\n      Kept as-is -- see AGENTS.md \"Structuring captures\".\n");
}

/* ISO-8601 UTC with MICROSECONDS. Chosen over an epoch integer because a
** capture file is meant to be read and hand-edited by a person with no
** tooling, and because lexicographic comparison on this format is
** chronological -- so ORDER BY ts needs no date parsing anywhere.
**
** The microseconds are not decoration and were added after a real data loss:
** at second resolution, four captures typed in the same second produced four
** identical ids, and the projection's INSERT OR REPLACE silently kept only
** the last -- seven notes captured, three projected. Rapid capture is the
** NORMAL case (an agent structuring a batch, a person emptying their head at
** a gate), not an edge case, and capture is the one operation in this system
** that must never lose an input. Sub-second precision makes ids unique and
** makes "last" strictly ordered, fixing both symptoms at their single cause. */
static void iso_now(char *out, size_t n){
    struct timeval tv;
    struct tm g;
    char base[32];
    gettimeofday(&tv, NULL);
#ifdef _WIN32
    gmtime_s(&g, &tv.tv_sec);
#else
    gmtime_r(&tv.tv_sec, &g);
#endif
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &g);
    snprintf(out, n, "%s.%06ldZ", base, (long)tv.tv_usec);
}

/* Lowercases and hyphenates a field value so "Monument Rocks", "monument
** rocks" and "Monument  Rocks" are one place rather than three. Applied on
** BOTH sides -- at capture and at query -- so the normalisation can change
** later without stranding old rows, since the table is rebuilt from the
** files every index. */
static void normalize_key(const char *z, char *out, size_t n){
    size_t o = 0;
    int lastDash = 1;
    if( !z ){ out[0] = '\0'; return; }
    while( *z && o + 1 < n ){
        unsigned char c = (unsigned char)*z++;
        if( isalnum(c) ){ out[o++] = (char)tolower(c); lastDash = 0; }
        else if( !lastDash ){ out[o++] = '-'; lastDash = 1; }
    }
    while( o > 0 && out[o-1] == '-' ) o--;
    out[o] = '\0';
}

static void set_field(viki_note *p, const char *key, const char *val){
    if( strcmp(key, "note") == 0 )  { strncpy(p->id, val, sizeof(p->id)-1); }
    else if( strcmp(key, "at") == 0 ){ strncpy(p->ts, val, sizeof(p->ts)-1); }
    else if( strcmp(key, "type") == 0 ) normalize_key(val, p->type, sizeof(p->type));
    else if( strcmp(key, "place") == 0 ) normalize_key(val, p->place, sizeof(p->place));
    else if( strcmp(key, "who") == 0 ) normalize_key(val, p->who, sizeof(p->who));
    else if( strcmp(key, "due") == 0 ) strncpy(p->due, val, sizeof(p->due)-1);
    else if( strcmp(key, "state") == 0 ) normalize_key(val, p->state, sizeof(p->state));
    else if( strcmp(key, "closes") == 0 ) strncpy(p->closes, val, sizeof(p->closes)-1);
    else if( strcmp(key, "claimed") == 0 ) strncpy(p->claimed, val, sizeof(p->claimed)-1);
    else if( strcmp(key, "lease") == 0 ) strncpy(p->lease, val, sizeof(p->lease)-1);
    else if( strcmp(key, "challenge") == 0 ) strncpy(p->challenge, val, sizeof(p->challenge)-1);
    else if( strcmp(key, "stolen-from") == 0 ) strncpy(p->stolen, val, sizeof(p->stolen)-1);
}

int viki_cmd_capture(const char *zDir, const char *zText,
                     const char *zPlace, const char *zType,
                     const char *zWho, const char *zDue, const char *zState){
    char dir[1024], path[1200], ts[32], id[40];
    struct stat st;
    FILE *f;

    if( !zText || !zText[0] ){
        fprintf(stderr, "viki capture: refusing to capture empty text\n");
        return 1;
    }
    iso_now(ts, sizeof(ts));
    snprintf(dir, sizeof(dir), "%s/%s", zDir && zDir[0] ? zDir : ".", VIKI_CAPTURE_DIR);
    if( stat(dir, &st) != 0 && mkdir(dir, 0755) != 0 ){
        perror("viki capture: mkdir");
        return 1;
    }
    /* One file per month: small enough to read whole, few enough to list, and
    ** it keeps a busy month from colliding with a quiet one in fossil's
    ** merge behaviour. */
    snprintf(path, sizeof(path), "%s/%.7s.md", dir, ts);
    /* id = timestamp minus punctuation, so ids sort chronologically and a
    ** human can see when a note was taken without cross-referencing. The
    ** microsecond field is what makes it unique -- see iso_now. */
    snprintf(id, sizeof(id), "%.4s%.2s%.2s-%.2s%.2s%.2s-%.6s",
             ts, ts+5, ts+8, ts+11, ts+14, ts+17, ts+20);

    f = fopen(path, "a");
    if( !f ){ perror("viki capture: open"); return 1; }
    fprintf(f, "@note %s\n@at %s\n", id, ts);
    if( zType  && zType[0] ){
        char nt[32]; normalize_key(zType, nt, sizeof(nt));
        warn_unknown_type(nt, "viki capture --type");
        fprintf(f, "@type %s\n", zType);
    }
    if( zPlace && zPlace[0] ) fprintf(f, "@place %s\n", zPlace);
    if( zWho   && zWho[0] )   fprintf(f, "@who %s\n", zWho);
    if( zDue   && zDue[0] )   fprintf(f, "@due %s\n", zDue);
    /* Default state is "open" ONLY for a task. An observation is not a chore
    ** and must never appear in "what needs to be done" -- that exact
    ** false-positive ("new foal in rimi's band") is why this file exists. */
    if( zState && zState[0] ) fprintf(f, "@state %s\n", zState);
    else if( zType && strcmp(zType, "task") == 0 ) fprintf(f, "@state open\n");
    fprintf(f, "%s\n\n", zText);
    fclose(f);

    printf("%s  %s\n", id, path);
    return 0;
}

/* ---- projection ---- */

static void note_flush(sqlite3_stmt *ins, viki_note *p, const char *zPath, int *pN){
    if( !p->id[0] ) return;
    /* With microsecond ids a collision means two blocks genuinely share an
    ** id -- a hand-edit or a bad merge, never ordinary use. REPLACE would
    ** drop one silently, which is precisely the failure this file already
    ** shipped once. Say so, on the record, and keep going. */
    {
        sqlite3_stmt *chk = NULL;
        sqlite3 *db = sqlite3_db_handle(ins);
        if( sqlite3_prepare_v2(db, "SELECT source_path FROM viki_note WHERE note_id=?1",
                               -1, &chk, NULL) == SQLITE_OK ){
            sqlite3_bind_text(chk, 1, p->id, -1, SQLITE_STATIC);
            if( sqlite3_step(chk) == SQLITE_ROW ){
                fprintf(stderr,
                    "viki index: WARNING duplicate note id %s in %s (already seen in %s) "
                    "-- one of the two will be lost; give one a fresh @note id\n",
                    p->id, zPath, (const char*)sqlite3_column_text(chk, 0));
            }
            sqlite3_finalize(chk);
        }
    }
    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, p->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, p->ts, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, p->type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, p->place, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, p->who, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, p->due, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 7, p->state, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 8, p->text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 9, zPath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 10, p->closes, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 11, p->claimed, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 12, p->lease, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 13, p->challenge, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 14, p->stolen, -1, SQLITE_TRANSIENT);
    if( sqlite3_step(ins) == SQLITE_DONE ) (*pN)++;
    note_clear(p);
}

static void note_parse_file(sqlite3_stmt *ins, const char *zPath, int *pN){
    FILE *f = fopen(zPath, "r");
    char line[2048];
    viki_note cur;
    if( !f ) return;
    note_clear(&cur);
    while( fgets(line, sizeof(line), f) ){
        size_t n = strlen(line);
        while( n > 0 && (line[n-1] == '\n' || line[n-1] == '\r') ) line[--n] = '\0';
        if( line[0] == '@' ){
            char key[32];
            const char *sp = strchr(line + 1, ' ');
            size_t klen = sp ? (size_t)(sp - line - 1) : strlen(line + 1);
            if( klen >= sizeof(key) ) klen = sizeof(key) - 1;
            memcpy(key, line + 1, klen); key[klen] = '\0';
            /* A new @note ends the previous block even without a blank line,
            ** so a hand-edited file that lost its separator still parses into
            ** distinct notes rather than one merged blob. */
            if( strcmp(key, "note") == 0 ) note_flush(ins, &cur, zPath, pN);
            set_field(&cur, key, sp ? sp + 1 : "");
        }else if( line[0] == '\0' ){
            note_flush(ins, &cur, zPath, pN);
        }else if( cur.id[0] ){
            size_t have = strlen(cur.text);
            if( have ) strncat(cur.text, " ", sizeof(cur.text) - have - 1);
            have = strlen(cur.text);
            strncat(cur.text, line, sizeof(cur.text) - have - 1);
        }
    }
    note_flush(ins, &cur, zPath, pN);
    fclose(f);
}

int viki_note_reindex(sqlite3 *db, const char *zDir){
    char dir[1024], path[1200];
    sqlite3_stmt *ins = NULL;
    DIR *d;
    struct dirent *e;
    int n = 0;

    if( sqlite3_exec(db, "DELETE FROM viki_note", NULL, NULL, NULL) != SQLITE_OK ) return -1;
    snprintf(dir, sizeof(dir), "%s/%s", zDir && zDir[0] ? zDir : ".", VIKI_CAPTURE_DIR);
    d = opendir(dir);
    if( !d ) return 0;   /* no captures yet is not an error */

    if( sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO viki_note"
            "(note_id,ts,type,place,who,due,state,text,source_path,closes,"
            " claimed,lease,challenge,stolen_from)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)", -1, &ins, NULL) != SQLITE_OK ){
        closedir(d);
        return -1;
    }
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    while( (e = readdir(d)) != NULL ){
        size_t ln = strlen(e->d_name);
        if( e->d_name[0] == '.' ) continue;
        if( ln < 4 || strcmp(e->d_name + ln - 3, ".md") != 0 ) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        note_parse_file(ins, path, &n);
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    sqlite3_finalize(ins);
    closedir(d);
    return n;
}

/* ---- query ---- */

static void print_note_row(void *pCtx, const viki_note_row *r){
    (void)pCtx;
    printf("%s  %s", r->id ? r->id : "?", r->ts ? r->ts : "");
    if( r->type  && r->type[0] )  printf("  [%s]", r->type);
    if( r->state && r->state[0] ) printf("  <%s>", r->state);
    if( r->place && r->place[0] ) printf("  @%s", r->place);
    if( r->who   && r->who[0] )   printf("  ~%s", r->who);
    if( r->due   && r->due[0] )   printf("  due:%s", r->due);
    if( r->closes&& r->closes[0] )printf("  closes:%s", r->closes);
    if( r->lease  && r->lease[0] )  printf("  lease:%s", r->lease);
    if( r->challenge && r->challenge[0] ) printf("  CHALLENGED:%s", r->challenge);
    if( r->stolen && r->stolen[0] ) printf("  stolen-from:%s", r->stolen);
    printf("\n    %s\n", r->text ? r->text : "");
}

/* ---- the promise ledger --------------------------------------------- */

static long parse_duration(const char *z);          /* defined below */
static void iso_plus(long secs, char *out, size_t n);

typedef struct {
    const char *zMe;
    char zNow[32];
    char zEod[32];      /* end of today, so "at risk today" is a real boundary */
    int nOverdue, nToday, nSoon, nUndated, nTheirs;
    int nShown;
} LedgerCtx;

/* Risk is a function of the due date and nothing else. Deliberately not of
** state or claim: a promise you have claimed and not delivered is exactly as
** broken as one you never touched, and softening that is how a ledger stops
** being believed. */
static const char *risk_of(const LedgerCtx *c, const char *zDue){
    if( !zDue || !zDue[0] ) return "";
    if( strcmp(zDue, c->zNow) < 0 ) return "OVERDUE";
    if( strcmp(zDue, c->zEod) <= 0 ) return "TODAY";
    return "";
}

static void ledger_cb(void *pCtx, const viki_note_row *row){
    LedgerCtx *c = (LedgerCtx*)pCtx;
    const char *zDue = row->due ? row->due : "";
    const char *zRisk = risk_of(c, zDue);
    const char *zWho = (row->who && row->who[0]) ? row->who : "";
    int bMine = (!zWho[0]) || (c->zMe && strcmp(zWho, c->zMe) == 0);

    if( !zDue[0] ) c->nUndated++;
    else if( strcmp(zRisk, "OVERDUE") == 0 ) c->nOverdue++;
    else if( strcmp(zRisk, "TODAY") == 0 ) c->nToday++;
    else c->nSoon++;
    if( !bMine ) c->nTheirs++;

    {
        /* An ISO stamp is 20 characters and made every column ragged. The day
        ** is what a ledger is read for; the clock time only matters for today,
        ** so it is shown only then. */
        char zWhen[16];
        if( !zDue[0] ) snprintf(zWhen, sizeof(zWhen), "%s", "(no due)");
        else if( strcmp(zRisk, "TODAY") == 0 ) snprintf(zWhen, sizeof(zWhen), "%.5s", zDue + 11);
        else snprintf(zWhen, sizeof(zWhen), "%.10s", zDue);
        printf("%-8s %-10s %-12s %s\n",
               zRisk[0] ? zRisk : "",
               zWhen,
               bMine ? "mine" : zWho,
               row->text ? row->text : "");
    }
    printf("         %s%s%s\n",
           row->id ? row->id : "",
           (row->place && row->place[0]) ? "  @" : "",
           (row->place && row->place[0]) ? row->place : "");
    c->nShown++;
}

/* One hop of a supersession chain. */
static int why_row(sqlite3 *db, const char *zId, const char *zMark){
    sqlite3_stmt *st;
    int found = 0;
    if( sqlite3_prepare_v2(db,
        "SELECT ts, coalesce(who,''), coalesce(state,''), coalesce(type,''), text"
        " FROM viki_note WHERE note_id=?1", -1, &st, NULL) != SQLITE_OK ) return 0;
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        printf("%s %s  %-8s %-10s %s\n", zMark,
               sqlite3_column_text(st, 0), sqlite3_column_text(st, 3),
               sqlite3_column_text(st, 1), sqlite3_column_text(st, 4));
        printf("    %s\n", zId);
        found = 1;
    }
    sqlite3_finalize(st);
    return found;
}

int viki_cmd_why(sqlite3 *db, const char *zId){
    char zCur[128];
    char aBack[32][128];
    int nBack = 0, i, nFwd = 0;

    if( !zId || !zId[0] ){ fprintf(stderr, "viki why: need a note id\n"); return 1; }
    snprintf(zCur, sizeof(zCur), "%s", zId);

    /* Backwards first: follow `closes` to the oldest ancestor. Bounded at 32
    ** hops -- a cycle would otherwise hang, and `closes` is not constrained
    ** to be acyclic anywhere. */
    while( nBack < 32 ){
        sqlite3_stmt *st;
        char zPrev[128] = "";
        if( sqlite3_prepare_v2(db, "SELECT coalesce(closes,'') FROM viki_note WHERE note_id=?1",
                               -1, &st, NULL) != SQLITE_OK ) break;
        sqlite3_bind_text(st, 1, zCur, -1, SQLITE_STATIC);
        if( sqlite3_step(st) == SQLITE_ROW )
            snprintf(zPrev, sizeof(zPrev), "%s", (const char*)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        if( !zPrev[0] ) break;
        snprintf(aBack[nBack], sizeof(aBack[0]), "%s", zPrev);
        nBack++;
        snprintf(zCur, sizeof(zCur), "%s", zPrev);
    }

    printf("viki why: %s\n\n", zId);
    for( i = nBack - 1; i >= 0; i-- ) why_row(db, aBack[i], "  ");
    if( !why_row(db, zId, "->") ){
        printf("(no such note)\n");
        return 1;
    }

    /* Forwards: whatever closed it, and whatever closed that. */
    snprintf(zCur, sizeof(zCur), "%s", zId);
    while( nFwd < 32 ){
        sqlite3_stmt *st;
        char zNext[128] = "";
        if( sqlite3_prepare_v2(db, "SELECT note_id FROM viki_note WHERE closes=?1"
                                   " ORDER BY ts ASC LIMIT 1", -1, &st, NULL) != SQLITE_OK ) break;
        sqlite3_bind_text(st, 1, zCur, -1, SQLITE_STATIC);
        if( sqlite3_step(st) == SQLITE_ROW )
            snprintf(zNext, sizeof(zNext), "%s", (const char*)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        if( !zNext[0] ) break;
        why_row(db, zNext, "  ");
        snprintf(zCur, sizeof(zCur), "%s", zNext);
        nFwd++;
    }

    if( nBack == 0 && nFwd == 0 ){
        printf("\nnothing supersedes it and it supersedes nothing.\n");
    }else{
        printf("\n%d before, %d after.\n", nBack, nFwd);
    }
    return 0;
}

/* ---- coverage: a primitive, not a judgment ---------------------------- */

int viki_cmd_coverage(sqlite3 *db, int bJson){
    sqlite3_stmt *st;
    int n = 0;

    if( sqlite3_prepare_v2(db,
        "SELECT CASE WHEN text LIKE '[%]%'"
        "            THEN substr(text, 2, instr(text, ']') - 2)"
        "            ELSE 'captured here' END AS src,"
        "       max(ts), count(*)"
        "  FROM viki_note GROUP BY src ORDER BY max(ts) DESC", -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki coverage: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    if( bJson ) printf("[");
    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *src = (const char*)sqlite3_column_text(st, 0);
        const char *ts  = (const char*)sqlite3_column_text(st, 1);
        int cnt = sqlite3_column_int(st, 2);
        if( bJson ){
            printf("%s{\"source\":\"%s\",\"last_seen\":\"%s\",\"notes\":%d}",
                   n ? "," : "", src ? src : "?", ts ? ts : "", cnt);
        }else{
            printf("%-18s %-28s %d note(s)\n", src ? src : "?", ts ? ts : "never", cnt);
        }
        n++;
    }
    sqlite3_finalize(st);
    if( bJson ) printf("]\n");
    else if( n == 0 ) printf("(nothing captured or ingested)\n");
    /* Deliberately NO "stale" column and no advice. When a source counts as
    ** stale, and what to do about it, are decisions -- see assistant/. */
    return 0;
}

int viki_cmd_promises(sqlite3 *db, const char *zMe, const char *zHorizon, int bAll){
    viki_note_filter f;
    LedgerCtx c;
    long horizon;
    char zCut[32];
    int rc;

    memset(&f, 0, sizeof(f));
    memset(&c, 0, sizeof(c));
    c.zMe = zMe;
    iso_now(c.zNow, sizeof(c.zNow));
    /* End of today in UTC. A real boundary rather than "now + 24h": "is it due
    ** today" is the question a morning brief answers, and a rolling window
    ** would call tomorrow morning's promise today's. */
    snprintf(c.zEod, sizeof(c.zEod), "%.10sT23:59:59Z", c.zNow);

    horizon = parse_duration(zHorizon ? zHorizon : "7d");
    if( horizon <= 0 ) horizon = 7 * 86400;
    iso_plus(horizon, zCut, sizeof(zCut));

    /* A promise is a `task` that nothing has retired. Type is the vocabulary
    ** AGENTS.md already fixes -- only `task` belongs in "what needs to be
    ** done" -- so the ledger reads that rather than inventing a second one. */
    f.type  = "task";
    f.bLive = 1;
    f.nMax  = 200;
    if( !bAll ){
        f.bHasDue = 1;
        f.dueBefore = zCut;
    }

    printf("viki promises: as of %s, horizon %s%s\n",
           c.zNow, zHorizon ? zHorizon : "7d", bAll ? ", including undated" : "");
    printf("%-8s %-10s %-12s %s\n", "RISK", "DUE", "WHO", "WHAT");
    rc = viki_note_query(db, &f, ledger_cb, &c);
    if( rc < 0 ) return 1;

    if( c.nShown == 0 ){
        printf("\nnothing owed in the next %s.\n", zHorizon ? zHorizon : "7d");
    }else{
        printf("\n%d overdue, %d due today, %d later", c.nOverdue, c.nToday, c.nSoon);
        if( c.nUndated ) printf(", %d undated", c.nUndated);
        if( c.nTheirs )  printf("  (%d held by someone else)", c.nTheirs);
        printf("\n");
    }

    /* COVERAGE, not a footnote (VIKIVERSE_V1 2.5). This ledger sees only what
    ** has been captured or ingested. Half of Warren's channels cannot be read
    ** by any machine -- Signal, WhatsApp, Facebook -- so "nothing owed" means
    ** "nothing owed THAT I CAN SEE", and saying only the first would be the
    ** one lie that costs the most trust. */
    if( !bAll ){
        printf("       undated promises are not shown; add --all\n");
    }
    printf("       this ledger sees captured and ingested notes only.\n");
    return 0;
}

int viki_cmd_notes_filter(sqlite3 *db, const viki_note_filter *f){
    int n = viki_note_query(db, f, print_note_row, NULL);
    if( n < 0 ) return 1;
    if( n == 0 ) fprintf(stderr, "(no notes match)\n");
    else fprintf(stderr, "viki notes: %d note(s)\n", n);
    return 0;
}

int viki_cmd_notes(sqlite3 *db, const char *zPlace, const char *zType,
                   const char *zState, const char *zWho, const char *zSince,
                   const char *zGrep, const char *zCloses, int bLast, int nMax){
    viki_note_filter f;
    int n;
    memset(&f, 0, sizeof(f));
    f.place=zPlace; f.type=zType; f.state=zState; f.who=zWho;
    f.since=zSince; f.grep=zGrep; f.closes=zCloses;
    f.bLast=bLast; f.nMax=nMax;
    n = viki_note_query(db, &f, print_note_row, NULL);
    if( n < 0 ) return 1;
    if( n == 0 ) fprintf(stderr, "(no notes match)\n");
    else fprintf(stderr, "viki notes: %d note(s)\n", n);
    return 0;
}



/* ---- claims, leases, stealing ---- */

/* "30s" / "5m" / "2h" / "3d" -> seconds. 0 means unparseable, which callers
** treat as "no lease", never as "expires immediately": a malformed duration
** must not silently make a claim stealable. */
static long parse_duration(const char *z){
    char *end;
    long n;
    if( !z || !z[0] ) return 0;
    n = strtol(z, &end, 10);
    if( end == z || n <= 0 ) return 0;
    switch( *end ){
        case 's': case '\0': return n;
        case 'm': return n * 60;
        case 'h': return n * 3600;
        case 'd': return n * 86400;
        default:  return 0;
    }
}

static void iso_plus(long secs, char *out, size_t n){
    struct timeval tv;
    struct tm g;
    time_t t;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec + secs;
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

/* Reads the current holder and lease straight from the PROJECTION, which is
** the same view the agent read when it decided to act. */
static int claim_state(sqlite3 *db, const char *zId, char *who, size_t nWho,
                       char *lease, size_t nLease, char *chal, size_t nChal){
    sqlite3_stmt *st;
    int found = 0;
    who[0] = lease[0] = chal[0] = '\0';
    if( sqlite3_prepare_v2(db, "SELECT coalesce(who,''), coalesce(lease,''),"
                               " coalesce(challenge,'') FROM viki_note WHERE note_id=?1",
                           -1, &st, NULL) != SQLITE_OK ) return 0;
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        snprintf(who,   nWho,   "%s", (const char*)sqlite3_column_text(st, 0));
        snprintf(lease, nLease, "%s", (const char*)sqlite3_column_text(st, 1));
        snprintf(chal,  nChal,  "%s", (const char*)sqlite3_column_text(st, 2));
        found = 1;
    }
    sqlite3_finalize(st);
    return found;
}

/* ---- structure: find the work, then apply it safely ---- */

int viki_note_query(sqlite3 *db, const viki_note_filter *f, viki_note_cb cb, void *pCtx){
    char sql[2200];   /* grew with the ledger predicates */
    char place[64], type[32], who[64], state[16];
    sqlite3_stmt *st;
    int n = 0;

    normalize_key(f->place, place, sizeof(place));
    normalize_key(f->type,  type,  sizeof(type));
    normalize_key(f->who,   who,   sizeof(who));
    normalize_key(f->state, state, sizeof(state));

    /* ORDER BY ts DESC is what makes --last work, and it needs no date
    ** arithmetic because ts is ISO-8601 UTC (see iso_now). bPending is a
    ** separate predicate rather than type='' so that "untyped" stays one
    ** concept in one place. */
    sqlite3_snprintf(sizeof(sql), sql,
        "SELECT note_id, ts, type, place, who, due, state, text, closes,"
        "       claimed, lease, challenge, stolen_from FROM viki_note"
        " WHERE (?1='' OR place=?1) AND (?2='' OR type=?2)"
        "   AND (?3='' OR state=?3) AND (?4='' OR who=?4)"
        "   AND (?5='' OR ts >= ?5) AND (?6='' OR regexp(?6, text))"
        "   AND (%d=0 OR who IS NULL OR who='')"
        /* An unleased claim is NOT stale -- it is UNDECLARED, and the two are
        ** opposite. Judge it by the age of the claim instead, which is what a
        ** person does anyway ("nobody has touched that in three days"). The
        ** original predicate treated a missing lease as expired, which
        ** punished exactly the claimer least able to fiddle with a phone --
        ** someone mid-task delivering hay. */
        "   AND (%d=0 OR (who IS NOT NULL AND who<>'' AND"
        "        ((lease<>'' AND lease < ?9) OR (coalesce(lease,'')='' AND claimed < ?10))))"
        "   AND (?8='' OR closes=?8)"
        "   AND (%d=0 OR type IS NULL OR type='')"
        /* bLive means STILL OWED, which is two conditions and not one.
        **
        ** Nothing has retired it -- a correlated NOT EXISTS rather than a join,
        ** so a note closed by SEVERAL others still appears once (or rather,
        ** does not appear at all) instead of being multiplied.
        **
        ** AND it has not closed itself. Found by running the brief: `viki
        ** structure --state closed` marks a task done WITHOUT any successor
        ** note, and the first version showed those as open promises and then
        ** asked questions about them. A ledger that keeps asking about finished
        ** work is one you stop reading. */
        "   AND (%d=0 OR (NOT EXISTS (SELECT 1 FROM viki_note c"
        "                              WHERE c.closes = viki_note.note_id)"
        "                 AND coalesce(state,'') NOT IN ('closed','done','cancelled')))"
        "   AND (%d=0 OR (due IS NOT NULL AND due<>''))"
        "   AND (?11='' OR (due IS NOT NULL AND due<>'' AND due <= ?11))"
        " ORDER BY %s LIMIT ?7",
        f->bUnclaimed ? 1 : 0, f->bStale ? 1 : 0,
        f->bPending ? 1 : 0,
        f->bLive ? 1 : 0, f->bHasDue ? 1 : 0,
        /* A ledger orders by WHEN IT IS OWED, not when it was written. Notes
        ** with no due sort last: they are owed but cannot be late. */
        (f->bHasDue || f->dueBefore)
            ? "CASE WHEN coalesce(due,'')='' THEN 1 ELSE 0 END, due ASC, ts DESC"
            : (f->bPending ? "ts ASC" : "ts DESC"));

    if( viki_grep_register(db) != SQLITE_OK ) return -1;
    if( sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki notes: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(st, 1, place, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, type,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, state, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, who,   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, f->since ? f->since : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, f->grep ? f->grep : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, f->bLast ? 1 : (f->nMax > 0 ? f->nMax : 50));
    sqlite3_bind_text(st, 11, f->dueBefore ? f->dueBefore : "", -1, SQLITE_TRANSIENT);
    {
        /* "now", for the lease comparison. Text, so the comparison is the
        ** same lexicographic one used everywhere else in this file. */
        char zNow[32], zCut[32];
        long age = parse_duration(f->staleAfter ? f->staleAfter : "1d");
        iso_now(zNow, sizeof(zNow));
        if( age <= 0 ) age = 86400;
        iso_plus(-age, zCut, sizeof(zCut));
        sqlite3_bind_text(st, 9, zNow, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 10, zCut, -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(st, 8, f->closes ? f->closes : "", -1, SQLITE_TRANSIENT);

    while( sqlite3_step(st) == SQLITE_ROW ){
        viki_note_row r;
        r.id    = (const char*)sqlite3_column_text(st, 0);
        r.ts    = (const char*)sqlite3_column_text(st, 1);
        r.type  = (const char*)sqlite3_column_text(st, 2);
        r.place = (const char*)sqlite3_column_text(st, 3);
        r.who   = (const char*)sqlite3_column_text(st, 4);
        r.due   = (const char*)sqlite3_column_text(st, 5);
        r.state = (const char*)sqlite3_column_text(st, 6);
        r.text  = (const char*)sqlite3_column_text(st, 7);
        r.closes= (const char*)sqlite3_column_text(st, 8);
        r.claimed  = (const char*)sqlite3_column_text(st, 9);
        r.lease    = (const char*)sqlite3_column_text(st, 10);
        r.challenge= (const char*)sqlite3_column_text(st, 11);
        r.stolen   = (const char*)sqlite3_column_text(st, 12);
        n++;
        if( cb ) cb(pCtx, &r);
    }
    sqlite3_finalize(st);
    return n;
}

static void print_pending_row(void *pCtx, const viki_note_row *r){
    (void)pCtx;
    printf("%s\t%s\t%s\n", r->id ? r->id : "?", r->ts ? r->ts : "", r->text ? r->text : "");
}

int viki_cmd_structure_pending(sqlite3 *db, int nMax){
    viki_note_filter f;
    int n;
    memset(&f, 0, sizeof(f));
    f.bPending = 1; f.nMax = nMax > 0 ? nMax : 100;
    n = viki_note_query(db, &f, print_pending_row, NULL);
    if( n < 0 ) return 1;
    if( n == 0 ) fprintf(stderr, "(nothing pending -- every capture has a @type)\n");
    else fprintf(stderr, "viki structure: %d capture(s) awaiting structure\n", n);
    return 0;
}

/* Emits the @key line for one field if the caller supplied it, else the
** original line if there was one. Returning "was it written" lets the caller
** append any field the block did not already carry. */
static int emit_field(FILE *out, const char *key, const char *zNew, const char *zOld){
    const char *v = zNew ? zNew : zOld;
    if( !v || !v[0] ) return 0;
    fprintf(out, "@%s %s\n", key, v);
    return 1;
}

static int structure_write(sqlite3 *db, const char *zId, const char *zType,
                           const char *zPlace, const char *zWho, const char *zDue,
                           const char *zState, const char *zCloses,
                           const char *zClaimed, const char *zLease,
                           const char *zChallenge, const char *zStolen){
    sqlite3_stmt *st;
    char path[1200], tmp[1300];
    FILE *in, *out;
    char line[2048];
    int inBlock = 0, wroteFields = 0, found = 0;
    char oT[64]={0}, oP[64]={0}, oW[64]={0}, oD[32]={0}, oS[16]={0}, oC[40]={0};
    char oCl[32]={0}, oL[32]={0}, oCh[96]={0}, oSt[64]={0};

    if( sqlite3_prepare_v2(db, "SELECT source_path FROM viki_note WHERE note_id=?1",
                           -1, &st, NULL) != SQLITE_OK ) return 1;
    sqlite3_bind_text(st, 1, zId, -1, SQLITE_STATIC);
    if( sqlite3_step(st) != SQLITE_ROW ){
        sqlite3_finalize(st);
        fprintf(stderr, "viki structure: no note %s (run `viki index` first?)\n", zId);
        return 1;
    }
    snprintf(path, sizeof(path), "%s", (const char*)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);

    if( zType && zType[0] ){
        char nt[32]; normalize_key(zType, nt, sizeof(nt));
        warn_unknown_type(nt, "viki structure --type");
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    in = fopen(path, "r");
    if( !in ){ perror("viki structure: open"); return 1; }
    out = fopen(tmp, "w");
    if( !out ){ perror("viki structure: temp"); fclose(in); return 1; }

    while( fgets(line, sizeof(line), in) ){
        if( strncmp(line, "@note ", 6) == 0 ){
            /* Leaving a block: flush any field the caller added that the block
            ** did not already have, before the boundary moves past it. */
            if( inBlock && !wroteFields ) wroteFields = 1;
            inBlock = (strncmp(line + 6, zId, strlen(zId)) == 0);
            if( inBlock ) found = 1;
            fputs(line, out);
            continue;
        }
        if( inBlock && line[0] == '@' ){
            /* Field lines inside the target block are dropped here and
            ** re-emitted in a fixed order below, so repeated structure passes
            ** cannot accumulate duplicate @place lines. */
            char key[32];
            const char *sp = strchr(line + 1, ' ');
            size_t klen = sp ? (size_t)(sp - line - 1) : 0;
            char val[256]; size_t vl;
            if( klen && klen < sizeof(key) ){
                memcpy(key, line + 1, klen); key[klen] = '\0';
                vl = strlen(sp + 1);
                while( vl && (sp[vl] == '\n' || sp[vl] == '\r') ) vl--;
                snprintf(val, sizeof(val), "%.*s", (int)vl, sp + 1);
                if( strcmp(key, "at") == 0 ){ fputs(line, out); continue; }
                if( strcmp(key, "type")  == 0 ) snprintf(oT, sizeof(oT), "%s", val);
                else if( strcmp(key, "place") == 0 ) snprintf(oP, sizeof(oP), "%s", val);
                else if( strcmp(key, "who")   == 0 ) snprintf(oW, sizeof(oW), "%s", val);
                else if( strcmp(key, "due")   == 0 ) snprintf(oD, sizeof(oD), "%s", val);
                else if( strcmp(key, "state") == 0 ) snprintf(oS, sizeof(oS), "%s", val);
                else if( strcmp(key, "closes")== 0 ) snprintf(oC, sizeof(oC), "%s", val);
                else if( strcmp(key, "claimed")== 0 ) snprintf(oCl, sizeof(oCl), "%s", val);
                else if( strcmp(key, "lease")== 0 ) snprintf(oL, sizeof(oL), "%s", val);
                else if( strcmp(key, "challenge")== 0 ) snprintf(oCh, sizeof(oCh), "%s", val);
                else if( strcmp(key, "stolen-from")== 0 ) snprintf(oSt, sizeof(oSt), "%s", val);
                else fputs(line, out);
                continue;
            }
        }
        if( inBlock && !wroteFields ){
            /* First non-@ line of the target block: the text begins, so every
            ** field must already be on paper. */
            emit_field(out, "type",  zType,  oT);
            emit_field(out, "place", zPlace, oP);
            emit_field(out, "who",   zWho,   oW);
            emit_field(out, "due",   zDue,   oD);
            emit_field(out, "state", zState, oS);
            emit_field(out, "closes", zCloses, oC);
            emit_field(out, "claimed", zClaimed, oCl);
            emit_field(out, "lease", zLease, oL);
            emit_field(out, "challenge", zChallenge, oCh);
            emit_field(out, "stolen-from", zStolen, oSt);
            wroteFields = 1;
        }
        fputs(line, out);
    }
    fclose(in);
    fclose(out);

    if( !found ){
        remove(tmp);
        fprintf(stderr, "viki structure: %s not found in %s\n", zId, path);
        return 1;
    }
    /* rename() over the original is atomic on POSIX, so an interrupted run
    ** leaves either the old file or the new one, never a truncated capture. */
    if( rename(tmp, path) != 0 ){
        perror("viki structure: rename");
        remove(tmp);
        return 1;
    }

    /* --closes marks the TARGET closed, in its own file, by recursing. */
    if( zCloses && zCloses[0] ){
        if( structure_write(db, zCloses, NULL, NULL, NULL, NULL, "closed", NULL,
                            NULL, NULL, NULL, NULL) != 0 ){
            fprintf(stderr, "viki structure: WARNING %s recorded as closing %s, "
                            "but %s could not be marked closed\n", zId, zCloses, zCloses);
        }
    }
    printf("%s updated in %s\n", zId, path);
    fprintf(stderr, "viki structure: re-run `viki index` to project the change\n");
    return 0;
}


void viki_structure_defaults(viki_structure_opts *o){
    memset(o, 0, sizeof(*o));
}

int viki_cmd_structure_apply(sqlite3 *db, const char *zId, const char *zType,
                             const char *zPlace, const char *zWho, const char *zDue,
                             const char *zState, const char *zCloses){
    viki_structure_opts o;
    viki_structure_defaults(&o);
    o.zType = zType; o.zPlace = zPlace; o.zWho = zWho;
    o.zDue = zDue; o.zState = zState; o.zCloses = zCloses;
    return viki_cmd_structure_opts(db, zId, &o);
}

int viki_cmd_structure_opts(sqlite3 *db, const char *zId, const viki_structure_opts *opts){
    char who[64], lease[32], chal[96];
    char nowBuf[32], leaseBuf[32], chalBuf[128], stolenBuf[64];
    const char *zClaimed = NULL, *zLease = NULL, *zChallenge = NULL, *zStolen = NULL;
    const char *zWho = opts->zWho;
    const char *zState = opts->zState;

    if( !claim_state(db, zId, who, sizeof(who), lease, sizeof(lease), chal, sizeof(chal)) ){
        fprintf(stderr, "viki structure: no note %s (run `viki index` first?)\n", zId);
        return 1;
    }
    iso_now(nowBuf, sizeof(nowBuf));

    /* HEARTBEAT: the holder renews its own lease. Refuses for anyone else --
    ** a heartbeat from a non-holder would be a claim in disguise. */
    if( opts->bHeartbeat ){
        long secs;
        if( !who[0] ){
            fprintf(stderr, "viki structure: %s has no holder to heart-beat\n", zId);
            return 1;
        }
        /* A bare --heartbeat is "still on it" and needs no duration. Only
        ** re-stamp the lease if the caller actually named one. */
        secs = parse_duration(opts->zLease ? opts->zLease : "");
        if( secs > 0 ){
            iso_plus(secs, leaseBuf, sizeof(leaseBuf));
            zLease = leaseBuf;
        }
        zClaimed = nowBuf; zChallenge = "";   /* answering clears the challenge */
        return structure_write(db, zId, NULL, NULL, NULL, NULL, NULL, NULL,
                               zClaimed, zLease, zChallenge, NULL);
    }

    /* CHALLENGE: refused while the lease is live. That refusal IS the
    ** niceness -- a peer who promised to be reachable until T gets to be
    ** left alone until T. */
    if( opts->zChallenge && opts->zChallenge[0] ){
        if( !who[0] ){
            fprintf(stderr, "viki structure: %s is unclaimed -- take it, do not challenge it\n", zId);
            return 1;
        }
        if( lease[0] && strcmp(nowBuf, lease) < 0 ){
            fprintf(stderr, "viki structure: %s is held by %s under a live lease until %s "
                            "-- challenge refused\n", zId, who, lease);
            return 1;
        }
        if( !lease[0] ){
            /* No lease declared, so there is nothing to have lapsed. Say when
            ** it was claimed and let the challenger judge -- which is what
            ** they were going to do anyway. */
            char claimedAt[32] = "";
            sqlite3_stmt *stC;
            if( sqlite3_prepare_v2(db, "SELECT coalesce(claimed,'') FROM viki_note WHERE note_id=?1",
                                   -1, &stC, NULL) == SQLITE_OK ){
                sqlite3_bind_text(stC, 1, zId, -1, SQLITE_STATIC);
                if( sqlite3_step(stC) == SQLITE_ROW )
                    snprintf(claimedAt, sizeof(claimedAt), "%s", (const char*)sqlite3_column_text(stC, 0));
                sqlite3_finalize(stC);
            }
            fprintf(stderr, "viki structure: %s declared no lease; claimed %s -- your call\n",
                    who, claimedAt[0] ? claimedAt : "(time unknown)");
        }
        snprintf(chalBuf, sizeof(chalBuf), "%s %s", opts->zChallenge, nowBuf);
        fprintf(stderr, "viki structure: challenged %s's claim on %s -- "
                        "they may answer with --heartbeat\n", who, zId);
        return structure_write(db, zId, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, chalBuf, NULL);
    }

    /* STEAL: lapsed lease AND an unanswered challenge that has aged. */
    if( opts->zSteal && opts->zSteal[0] ){
        char cWho[64] = "", cWhen[32] = "";
        long grace = parse_duration(opts->zGrace ? opts->zGrace : "60s");
        if( grace <= 0 ) grace = 60;
        char graceEdge[32];
        if( !who[0] ){
            fprintf(stderr, "viki structure: %s is unclaimed -- take it with --who\n", zId);
            return 1;
        }
        if( lease[0] && strcmp(nowBuf, lease) < 0 ){
            fprintf(stderr, "viki structure: %s holds %s under a live lease until %s "
                            "-- refusing to steal\n", who, zId, lease);
            return 1;
        }
        /* No lease is not an invitation. The challenge-and-grace path still
        ** applies; the only thing a lease changes is that it can REFUSE a
        ** challenge outright. Undeclared means "judge it", not "take it". */
        if( !chal[0] ){
            fprintf(stderr, "viki structure: no challenge on record for %s. Ask first:\n"
                            "  viki structure %s --challenge %s\n", zId, zId, opts->zSteal);
            return 1;
        }
        sscanf(chal, "%63s %31s", cWho, cWhen);
        /* Clock skew is unarbitrable across peers, so a challenge stamped in
        ** the future means the two machines disagree. Refuse rather than
        ** guess: failing toward leaving work alone is the safe direction. */
        if( strcmp(cWhen, nowBuf) > 0 ){
            fprintf(stderr, "viki structure: challenge on %s is stamped in the future (%s) "
                            "-- clocks disagree, refusing to adjudicate\n", zId, cWhen);
            return 1;
        }
        iso_plus(-grace, graceEdge, sizeof(graceEdge));
        if( strcmp(cWhen, graceEdge) > 0 ){
            fprintf(stderr, "viki structure: challenge on %s was only just made (%s); "
                            "give %s the grace period to answer\n", zId, cWhen, who);
            return 1;
        }
        snprintf(stolenBuf, sizeof(stolenBuf), "%s", who);
        fprintf(stderr, "viki structure: %s takes %s from %s "
                        "(lease lapsed %s, challenged %s, unanswered)\n",
                opts->zSteal, zId, who, lease[0] ? lease : "(none declared)", cWhen);
        zWho = opts->zSteal; zStolen = stolenBuf; zChallenge = "";
        zClaimed = nowBuf;
        if( opts->zLease && opts->zLease[0] ){
            iso_plus(parse_duration(opts->zLease), leaseBuf, sizeof(leaseBuf));
            zLease = leaseBuf;
        }
        return structure_write(db, zId, opts->zType, opts->zPlace, zWho, opts->zDue,
                               zState, opts->zCloses, zClaimed, zLease, zChallenge, zStolen);
    }

    /* COMPARE-AND-SET on --who. Setting a holder over an existing, different
    ** one is exactly the collision ROLEPLAY.md prohibits in prose and the
    ** tool used to permit silently. --force is for a holder correcting
    ** itself; displacing someone else goes through --challenge/--steal. */
    if( zWho && zWho[0] && who[0] && strcmp(zWho, who) != 0 && !opts->bForce ){
        fprintf(stderr,
            "viki structure: %s is already held by %s%s%s%s.\n"
            "  To take it over: viki structure %s --challenge %s   (then --steal %s once the\n"
            "  lease has lapsed and the grace period has passed). --force overrides.\n",
            zId, who,
            lease[0] ? " until " : "", lease[0] ? lease : "",
            (lease[0] && strcmp(nowBuf, lease) >= 0) ? " (LAPSED)" : "",
            zId, zWho, zWho);
        return 1;
    }

    if( zWho && zWho[0] ){
        zClaimed = nowBuf;
        if( opts->zLease && opts->zLease[0] ){
            long secs = parse_duration(opts->zLease);
            if( secs <= 0 ){
                fprintf(stderr, "viki structure: unparseable --lease '%s' "
                                "(want 30s, 5m, 2h, 3d)\n", opts->zLease);
                return 1;
            }
            iso_plus(secs, leaseBuf, sizeof(leaseBuf));
            zLease = leaseBuf;
        }
    }
    /* Releasing (--who "") clears the whole claim, not just the holder --
    ** a lease or a challenge outliving its claim would be a ghost. */
    if( zWho && !zWho[0] ){
        zClaimed = ""; zLease = ""; zChallenge = ""; zStolen = "";
    }

    return structure_write(db, zId, opts->zType, opts->zPlace, zWho, opts->zDue,
                           zState, opts->zCloses, zClaimed, zLease, zChallenge, zStolen);
}
