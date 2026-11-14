/*
** viki_regexp.c -- the regex method's engine (Q-6; REQUIREMENTS_v3 Not yet
** answered 7): SQLite's ext/misc/regexp.c from the vendored SQLCipher, included
** whole and compiled as part of SQLite (SQLITE_CORE), so no code is copied and
** its matcher is ours to call. Its dialect is POSIX ERE plus \w \d \s \b and
** their negations, over UTF-8, with no back-references; it is an NFA, so a
** match costs O(pattern x input) and never more (ENV-2).
**
** viki registers one function of its own, vk_regexp(pattern, body), and not
** regexp.c's regexp(): that one reads its input as a C string, so a body
** holding a NUL byte would end at the NUL and regex would silently stop being
** exhaustive. The matcher also takes a zero byte as end of input, so a body
** holding one is matched as a copy in which each NUL is 0x80, which the
** matcher reads as U+FFFD: same length, and only "." matches it.
*/
#define SQLITE_CORE 1
#define sqlite3_regexp_init vk_regexp_c_init   /* its own init: not called, and not exported under its name */
#include "regexp.c"
#include "viki_store_int.h"

static void vk_regexp_func(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    ReCompiled *pRe = sqlite3_get_auxdata(ctx, 0);
    const unsigned char *z;
    unsigned char *pCopy = 0;
    int n, rc, bSetAux = 0;
    (void)argc;
    if( sqlite3_value_type(argv[0])==SQLITE_NULL || sqlite3_value_type(argv[1])==SQLITE_NULL ) return;
    if( pRe==0 ){
        /* As regexp.c's own function compiles and caches a pattern. */
        int mxLen = re_maxlen(ctx);
        const char *zErr;
        const char *zPattern = (const char*)sqlite3_value_text(argv[0]);
        if( zPattern==0 ){ sqlite3_result_error_nomem(ctx); return; }
        if( sqlite3_value_bytes(argv[0])>mxLen ){
            zErr = "REGEXP pattern too big";
        }else{
            zErr = re_compile(&pRe, zPattern, re_maxnfa(mxLen), 0);
        }
        if( zErr ){ re_free(pRe); sqlite3_result_error(ctx, zErr, -1); return; }
        if( pRe==0 ){ sqlite3_result_error_nomem(ctx); return; }
        bSetAux = 1;
    }
    n = sqlite3_value_bytes(argv[1]);
    z = (const unsigned char*)sqlite3_value_blob(argv[1]);
    if( z==0 ) z = (const unsigned char*)"";
    if( n>0 && memchr(z, 0, (size_t)n) ){
        int i;
        pCopy = sqlite3_malloc(n);
        if( pCopy==0 ){
            if( bSetAux ) re_free(pRe);
            sqlite3_result_error_nomem(ctx);
            return;
        }
        for(i=0; i<n; i++) pCopy[i] = z[i] ? z[i] : 0x80;
        z = pCopy;
    }
    rc = re_match(pRe, z, n);
    sqlite3_free(pCopy);
    if( rc<0 ) sqlite3_result_error_nomem(ctx);
    else sqlite3_result_int(ctx, rc);
    if( bSetAux ) sqlite3_set_auxdata(ctx, 0, pRe, re_free_voidptr);
}

int vk_regexp_register(sqlite3 *db){
    return sqlite3_create_function(db, "vk_regexp", 2, SQLITE_UTF8|SQLITE_DETERMINISTIC, 0,
                                   vk_regexp_func, 0, 0);
}
