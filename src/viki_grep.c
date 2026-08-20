#include "viki_grep.h"
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Frees a compiled pattern held as SQLite auxdata. */
static void regexp_free(void *p){
    regfree((regex_t*)p);
    free(p);
}

/* regexp(pattern, text) -> 0/1.
**
** The compiled regex_t is stashed as auxdata on argument 0. SQLite keeps
** auxdata for as long as that argument's VALUE is unchanged across rows,
** which for a bound pattern is the whole scan -- so a 10k-chunk grep calls
** regcomp() ONCE. Without this the function would recompile the pattern for
** every row, which is the classic way a SQL regexp() ends up mysteriously
** slower than a shell pipeline.
**
** REG_NOSUB is set because callers only ask "does it match"; no capture
** groups are reported, and it lets libc pick a cheaper matcher. */
static void regexp_func(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    const char *zText;
    regex_t *pRe;
    int rc;

    if( argc != 2 ) return;
    if( sqlite3_value_type(argv[0]) == SQLITE_NULL
     || sqlite3_value_type(argv[1]) == SQLITE_NULL ){
        sqlite3_result_null(ctx);
        return;
    }

    pRe = (regex_t*)sqlite3_get_auxdata(ctx, 0);
    if( !pRe ){
        const char *zPat = (const char*)sqlite3_value_text(argv[0]);
        int flags = REG_EXTENDED | REG_NOSUB;
        /* A trailing (?i) is not POSIX, so case-insensitivity is a flag on
        ** the command rather than pattern syntax; viki_cmd_grep lowercases
        ** nothing and instead passes the whole pattern through a second
        ** registration under a different name. See viki_grep_register. */
        if( sqlite3_user_data(ctx) != NULL ) flags |= REG_ICASE;
        pRe = malloc(sizeof(regex_t));
        if( !pRe ){ sqlite3_result_error_nomem(ctx); return; }
        rc = regcomp(pRe, zPat ? zPat : "", flags);
        if( rc != 0 ){
            char aErr[256];
            regerror(rc, pRe, aErr, sizeof(aErr));
            free(pRe);
            sqlite3_result_error(ctx, aErr, -1);
            return;
        }
        sqlite3_set_auxdata(ctx, 0, pRe, regexp_free);
        /* If SQLite refused to hold the auxdata it has already called
        ** regexp_free, so pRe must not be used again this row. Re-fetch. */
        pRe = (regex_t*)sqlite3_get_auxdata(ctx, 0);
        if( !pRe ){ sqlite3_result_int(ctx, 0); return; }
    }

    zText = (const char*)sqlite3_value_text(argv[1]);
    sqlite3_result_int(ctx, zText && regexec(pRe, zText, 0, NULL, 0) == 0);
}

int viki_grep_register(sqlite3 *db){
    int rc;
    /* Two registrations differing only in user_data, which regexp_func reads
    ** as "add REG_ICASE". `regexp` keeps SQLite's infix REGEXP operator
    ** working with the expected case-sensitive semantics. */
    rc = sqlite3_create_function(db, "regexp", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                 NULL, regexp_func, NULL, NULL);
    if( rc != SQLITE_OK ) return rc;
    return sqlite3_create_function(db, "regexpi", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                   (void*)1, regexp_func, NULL, NULL);
}

int viki_cmd_grep(sqlite3 *db, const char *zPattern, int nMax, int bIgnoreCase,
                  const char *zSourceLike, int nChars){
    sqlite3_stmt *st;
    char zSql[512];
    int n = 0, rc;

    if( nChars <= 0 ) nChars = 160;

    /* DISTINCT on (content_hash, chunk_ix): viki_source may hold several
    ** paths for one content_hash (two files with identical bytes share a
    ** hash), and without this the same chunk prints once per path. */
    sqlite3_snprintf(sizeof(zSql), zSql,
        "SELECT c.content_hash, c.chunk_ix, substr(c.chunk_text,1,%d),"
        "       (SELECT s.path FROM viki_source s WHERE s.content_hash=c.content_hash LIMIT 1)"
        "  FROM viki_chunk c"
        " WHERE %s(?1, c.chunk_text)"
        "   AND (?2 IS NULL OR EXISTS(SELECT 1 FROM viki_source s2"
        "        WHERE s2.content_hash=c.content_hash AND s2.path LIKE ?2))"
        " GROUP BY c.content_hash, c.chunk_ix"
        " ORDER BY c.content_hash, c.chunk_ix",
        nChars, bIgnoreCase ? "regexpi" : "regexp");

    if( sqlite3_prepare_v2(db, zSql, -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki grep: prepare failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(st, 1, zPattern, -1, SQLITE_STATIC);
    if( zSourceLike ) sqlite3_bind_text(st, 2, zSourceLike, -1, SQLITE_STATIC);
    else sqlite3_bind_null(st, 2);

    while( (rc = sqlite3_step(st)) == SQLITE_ROW ){
        const char *zHash = (const char*)sqlite3_column_text(st, 0);
        int ix = sqlite3_column_int(st, 1);
        const char *zText = (const char*)sqlite3_column_text(st, 2);
        const char *zSrc  = (const char*)sqlite3_column_text(st, 3);
        n++;
        /* Same hit-line shape `viki ask` prints, minus the score, so one
        ** parser handles both. */
        printf("[%d] %s#%d  %s\n    %s\n\n", n, zHash ? zHash : "?", ix,
               zSrc ? zSrc : "(source path unknown)", zText ? zText : "");
        if( nMax > 0 && n >= nMax ) break;
    }
    if( rc == SQLITE_ERROR ){
        /* A bad pattern surfaces here, not at prepare: regcomp runs on the
        ** first row. Report it as the user error it is, not as a crash. */
        fprintf(stderr, "viki grep: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return 1;
    }
    sqlite3_finalize(st);
    if( n == 0 ) fprintf(stderr, "(no matches)\n");
    else fprintf(stderr, "viki grep: %d chunk(s) matched\n", n);
    return 0;
}
