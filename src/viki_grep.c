#include "viki_grep.h"
/* For VIKI_MARK_* only. `viki grep` prints the same kind of excerpt
** `viki ask` does -- a possibly-middle chunk, cut short -- under the same
** citable `<hash>#<ix>` header, so it must mark the same facts with the
** SAME strings. Including the header rather than retyping the literals is
** what makes that a compile-time guarantee instead of a convention. */
#include "viki_ask.h"
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
        /* REG_NEWLINE makes ^ and $ match at LINE boundaries inside the
        ** chunk, and stops '.' from crossing a newline -- i.e. the semantics
        ** anyone typing into something called "grep" already has.
        **
        ** Without it they anchored to the whole CHUNK, and the failure was
        ** silent in the worst possible way: an A/B trial had an agent run
        ** `viki grep "^2026-08-2"` against a corpus where eight documents
        ** literally begin with that string on their second line. It returned
        ** ZERO matches, which reads exactly like "this does not exist" -- so
        ** the agent could not enumerate the corpus chronologically and said
        ** so as its single largest uncertainty. An empty result that means
        ** "your anchors mean something else than you think" is the same
        ** class of defect as an index that silently drops 61% of its text. */
        int flags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
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
                  const char *zSourceLike, int nChars,
                  const char *zSince, const char *zUntil, int bNewest, int bShowTime){
    sqlite3_stmt *st;
    /* 1024, not 512. The expanded SQL below measures 393 bytes without
    ** the two fragment columns and 513 with them (nChars=160), so the
    ** 512-byte buffer this used to have would have had
    ** sqlite3_snprintf() truncate it silently and prepare_v2() then fail
    ** with a syntax error naming none of that. */
    char zSql[1024];
    int n = 0, rc;

    if( nChars <= 0 ) nChars = 160;

    /* DISTINCT on (content_hash, chunk_ix): viki_source may hold several
    ** paths for one content_hash (two files with identical bytes share a
    ** hash), and without this the same chunk prints once per path.
    **
    ** Columns 4 and 5 are the FRAGMENT facts, computed exactly as
    ** viki_ask.c does them and for the same reason (see VIKI_FRAG_* in
    ** viki_ask.h): column 4 asks the db whether column 2's substr()
    ** actually threw anything away -- substr()/length() both count
    ** CHARACTERS on TEXT, so the comparison is exact, and the caller
    ** cannot tell afterwards -- and column 5 is the document's last
    ** chunk_ix, taken over EVERY model_id because a chunk here carries no
    ** model_id and must not acquire one. */
    sqlite3_snprintf(sizeof(zSql), zSql,
        "SELECT c.content_hash, c.chunk_ix, substr(c.chunk_text,1,%d),"
        "       (SELECT s.path FROM viki_source s WHERE s.content_hash=c.content_hash LIMIT 1),"
        "       length(c.chunk_text) > %d,"
        "       (SELECT max(m.chunk_ix) FROM viki_chunk m WHERE m.content_hash=c.content_hash)"
        "     , (SELECT s3.ts FROM viki_source s3 WHERE s3.content_hash=c.content_hash"
        "        ORDER BY s3.ts DESC LIMIT 1) AS ts"
        "  FROM viki_chunk c"
        " WHERE %s(?1, c.chunk_text)"
        "   AND (?2 IS NULL OR EXISTS(SELECT 1 FROM viki_source s2"
        "        WHERE s2.content_hash=c.content_hash AND s2.path LIKE ?2))"
        "   AND (?3='' OR ts >= ?3) AND (?4='' OR ts <= ?4)"
        " GROUP BY c.content_hash, c.chunk_ix"
        /* ISO-8601 text, so lexicographic order IS chronological: --newest
        ** needs no date arithmetic, and an empty ts sorts last under DESC,
        ** which is the honest place for "no time known". */
        " ORDER BY %s",
        /* Four specifiers, four arguments -- %d, %d, %s, %s in that order.
        ** The order-by clause was added without its argument once, and
        ** sqlite3_snprintf read a garbage pointer for it: the error came back
        ** as `no such column: <mojibake>`, which points at the SQL rather than
        ** at the format string and cost real time to read correctly. */
        nChars, nChars, bIgnoreCase ? "regexpi" : "regexp",
        bNewest ? "ts DESC, c.content_hash, c.chunk_ix" : "c.content_hash, c.chunk_ix");

    if( sqlite3_prepare_v2(db, zSql, -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki grep: prepare failed: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    sqlite3_bind_text(st, 1, zPattern, -1, SQLITE_STATIC);
    if( zSourceLike ) sqlite3_bind_text(st, 2, zSourceLike, -1, SQLITE_STATIC);
    else sqlite3_bind_null(st, 2);
    sqlite3_bind_text(st, 3, zSince ? zSince : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, zUntil ? zUntil : "", -1, SQLITE_STATIC);

    while( (rc = sqlite3_step(st)) == SQLITE_ROW ){
        const char *zHash = (const char*)sqlite3_column_text(st, 0);
        int ix = sqlite3_column_int(st, 1);
        const char *zText = (const char*)sqlite3_column_text(st, 2);
        const char *zSrc  = (const char*)sqlite3_column_text(st, 3);
        const char *zTs   = (const char*)sqlite3_column_text(st, 6);
        int bCut  = sqlite3_column_int(st, 4);
        /* NULL max(chunk_ix) means "extent unknown", not "chunk 0". Same
        ** rule as fill_fragment_flags(): an extent we could not read marks
        ** the TAIL anyway, because "I could not prove this chunk ends the
        ** document" must never render as "this chunk ends the document". */
        int maxIx = sqlite3_column_type(st, 5) == SQLITE_NULL
                        ? -1 : sqlite3_column_int(st, 5);
        const char *zShow = zText ? zText : "";
        int nText = (int)strlen(zShow);
        n++;
        /* Whitespace comes off BOTH ends for display only, and only from
        ** this printf's view of a const string SQLite owns (a pointer and
        ** a %.*s precision -- no copy, nothing mutated). It is the same
        ** rule viki_ask.c's trim_excerpt() applies, deliberately, so the
        ** two surfaces decorate the same text the same way.
        **
        ** Both ends acquire whitespace that means nothing here. A
        ** chunk_text slice ends with the newline that ended its last line,
        ** and a chunk that begins with a blank line starts with one too --
        ** either way a marker lands on a line of ITS OWN, where it reads
        ** as a separate remark rather than as "and the document goes on
        ** from here". Whitespace is the only thing this may ever remove,
        ** and only from a rendering that already reflows the excerpt (raw
        ** newlines, only the first line indented by the printf below);
        ** anyone who needs the chunk byte-for-byte asks /api/chunk. */
        while( nText > 0 && (unsigned char)zShow[nText - 1] <= ' ' ) nText--;
        while( nText > 0 && (unsigned char)zShow[0] <= ' ' ){ zShow++; nText--; }
        /* Same hit-line shape `viki ask` prints, minus the score, so one
        ** parser handles both -- markers included, and on the EXCERPT line
        ** only. The header is a citation (`<hash>#<ix>` is what
        ** /api/chunk?hash=&ix= takes) and build/grep-probe.sh's C6/C7
        ** count it by position. */
        /* THE HEADER LINE IS A CITATION CONTRACT, and the timestamp is
        ** therefore OPT-IN. `[N] <64hex>#<ix>  <source>` is asserted anchored
        ** at BOTH ends by build/fragment-probe.sh R3 and parsed by position in
        ** three test files; inserting a field broke it immediately. --time
        ** appends after the ragged source, which is already last precisely so
        ** nothing can shift a field a script reads by position. Filtering and
        ** ordering (--since/--until/--newest) work regardless of this flag:
        ** what you can SEE and what you can SELECT ON are separate questions. */
        printf("[%d] %s#%d  %s%s%s%s\n    %s%.*s%s%s\n\n", n, zHash ? zHash : "?", ix,
               zSrc ? zSrc : "(source path unknown)",
               (bShowTime && zTs && zTs[0]) ? "  [" : "",
               (bShowTime && zTs && zTs[0]) ? zTs : "",
               (bShowTime && zTs && zTs[0]) ? "]" : "",
               (ix > 0)                  ? VIKI_MARK_HEAD " " : "",
               nText, zShow,
               bCut                      ? " " VIKI_MARK_CUT  : "",
               (maxIx < 0 || ix < maxIx) ? " " VIKI_MARK_TAIL : "");
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
