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
    char text[1024];
} viki_note;

static void note_clear(viki_note *p){ memset(p, 0, sizeof(*p)); }

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
    if( zType  && zType[0] )  fprintf(f, "@type %s\n", zType);
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
            "(note_id,ts,type,place,who,due,state,text,source_path)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)", -1, &ins, NULL) != SQLITE_OK ){
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

int viki_cmd_notes(sqlite3 *db, const char *zPlace, const char *zType,
                   const char *zState, const char *zWho, const char *zSince,
                   const char *zGrep, int bLast, int nMax){
    char sql[1400];
    char place[64], type[32], who[64], state[16];
    sqlite3_stmt *st;
    int n = 0;

    normalize_key(zPlace, place, sizeof(place));
    normalize_key(zType,  type,  sizeof(type));
    normalize_key(zWho,   who,   sizeof(who));
    normalize_key(zState, state, sizeof(state));

    /* ORDER BY ts DESC is the whole point of --last, and it works without any
    ** date arithmetic because ts is ISO-8601 UTC (see iso_now). */
    sqlite3_snprintf(sizeof(sql), sql,
        "SELECT note_id, ts, type, place, who, due, state, text FROM viki_note"
        " WHERE (?1='' OR place=?1) AND (?2='' OR type=?2)"
        "   AND (?3='' OR state=?3) AND (?4='' OR who=?4)"
        "   AND (?5='' OR ts >= ?5) AND (?6='' OR %s(?6, text))"
        " ORDER BY ts DESC LIMIT ?7",
        "regexp");

    if( viki_grep_register(db) != SQLITE_OK ) return 1;
    if( sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki notes: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(st, 1, place, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, type,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, state, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, who,   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, zSince ? zSince : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, zGrep ? zGrep : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, bLast ? 1 : (nMax > 0 ? nMax : 50));

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *id = (const char*)sqlite3_column_text(st, 0);
        const char *ts = (const char*)sqlite3_column_text(st, 1);
        const char *ty = (const char*)sqlite3_column_text(st, 2);
        const char *pl = (const char*)sqlite3_column_text(st, 3);
        const char *wh = (const char*)sqlite3_column_text(st, 4);
        const char *du = (const char*)sqlite3_column_text(st, 5);
        const char *stt= (const char*)sqlite3_column_text(st, 6);
        const char *tx = (const char*)sqlite3_column_text(st, 7);
        n++;
        printf("%s  %s", id ? id : "?", ts ? ts : "");
        if( ty && ty[0] )  printf("  [%s]", ty);
        if( stt && stt[0] ) printf("  <%s>", stt);
        if( pl && pl[0] )  printf("  @%s", pl);
        if( wh && wh[0] )  printf("  ~%s", wh);
        if( du && du[0] )  printf("  due:%s", du);
        printf("\n    %s\n", tx ? tx : "");
    }
    sqlite3_finalize(st);
    if( n == 0 ) fprintf(stderr, "(no notes match)\n");
    else fprintf(stderr, "viki notes: %d note(s)\n", n);
    return 0;
}
