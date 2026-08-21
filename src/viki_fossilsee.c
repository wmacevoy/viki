/*
** viki_fossilsee.c -- runtime loader for libfossilsee. See
** viki_fossilsee.h for why this is dlopen and not a link dependency.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include "viki_fossilsee.h"
#include "viki_cache.h"     /* viki_fossil_binary() -- used to guess where
                            ** the library sits, next to the binary */

#ifndef _WIN32
# include <dlfcn.h>
#else
/* MSYS provides dlfcn; viki's Windows build is plain MSYS, not MINGW64
** (it needs fork()/BSD sockets -- see CLAUDE.md), so this is available. */
# include <dlfcn.h>
#endif

/* ---------------------------------------------------------------------
** The libfossilsee ABI, restated.
**
** These declarations MUST match embed/fossilsee.h in the fossil-see
** project. They are restated here rather than #include'd on purpose: this
** file must compile on a machine that has no copy of that project at all,
** which is the entire point of loading the library at runtime. The
** duplication is made safe by fossilsee_abi() -- the library reports the
** version IT was built with, we compare against the one WE were built
** against, and refuse to call anything if they disagree. That check is
** the only thing standing between a version skew and undefined behaviour,
** so do not remove it when adding entry points.
*/
#define VIKI_FOSSILSEE_ABI 1
#define FS_OK    0
#define FS_ERROR 1
#define FS_BUSY  2
#define FS_ABORT 3

typedef struct fossilsee fossilsee;
typedef int (*fs_row_cb)(void *pArg, int nCol,
                         const char *const *azVal, const size_t *anLen);

typedef int  (*fn_abi)(void);
typedef int  (*fn_open)(const char*, const char*, fossilsee**);
typedef void (*fn_close)(fossilsee*);
typedef int  (*fn_sql)(fossilsee*, const char*, fs_row_cb, void*);
typedef const char *(*fn_errmsg)(fossilsee*);

static void       *g_lib      = 0;
static fn_abi      g_abi      = 0;
static fn_open     g_open     = 0;
static fn_close    g_close    = 0;
static fn_sql      g_sql      = 0;
static fn_errmsg   g_errmsg   = 0;
static fossilsee  *g_repo     = 0;    /* held OPEN across calls */
static int         g_tried    = 0;
static char        g_status[512] = "not attempted";

/* ---- growable output buffer ------------------------------------------ */
typedef struct {
    char  *z;
    size_t n;
    size_t nAlloc;
    int    oom;
} Buf;

static void buf_append(Buf *p, const char *z, size_t n){
    if( p->oom ) return;
    if( p->n + n + 1 > p->nAlloc ){
        size_t nNew = p->nAlloc ? p->nAlloc : 65536;
        char *zNew;
        while( nNew < p->n + n + 1 ) nNew *= 2;
        zNew = realloc(p->z, nNew);
        if( !zNew ){ p->oom = 1; return; }
        p->z = zNew;
        p->nAlloc = nNew;
    }
    if( n ) memcpy(p->z + p->n, z, n);
    p->n += n;
    p->z[p->n] = '\0';   /* keep string-shaped output usable; the NUL sits
                         ** PAST p->n, exactly as run_capture() does it */
}

/* Reproduces `fossil sql`'s stdout: value bytes, then one newline. */
static int collect_row(void *pArg, int nCol,
                       const char *const *azVal, const size_t *anLen){
    Buf *p = (Buf*)pArg;
    if( nCol > 0 && azVal[0] ) buf_append(p, azVal[0], anLen[0]);
    buf_append(p, "\n", 1);
    return p->oom;   /* abort the scan on OOM rather than silently short */
}

/* ---- finding the library --------------------------------------------
** Ordered most-specific first. $VIKI_FOSSILSEE_LIB is the override that
** makes this testable without installing anything, and is what
** build/fossilsee-probe.sh uses. */
static const char *lib_names[] = {
#if defined(__APPLE__)
    "libfossilsee.dylib",
#elif defined(_WIN32)
    "libfossilsee.dll",
#else
    "libfossilsee.so",
#endif
    0
};

static void *try_load(const char *zPath){
    if( !zPath || !zPath[0] ) return 0;
    return dlopen(zPath, RTLD_LAZY | RTLD_LOCAL);
}

/* Resolve every symbol or fail as a unit -- a half-bound library is worse
** than none, because the failure would surface at the first call instead
** of here where it can be reported. */
static int bind_symbols(void){
    g_abi    = (fn_abi)    dlsym(g_lib, "fossilsee_abi");
    g_open   = (fn_open)   dlsym(g_lib, "fossilsee_open");
    g_close  = (fn_close)  dlsym(g_lib, "fossilsee_close");
    g_sql    = (fn_sql)    dlsym(g_lib, "fossilsee_sql");
    g_errmsg = (fn_errmsg) dlsym(g_lib, "fossilsee_errmsg");
    return g_abi && g_open && g_close && g_sql && g_errmsg;
}

static void load_once(void){
    const char *zEnv;
    int i;

    if( g_tried ) return;
    g_tried = 1;

    zEnv = getenv("VIKI_FOSSILSEE_LIB");
    if( zEnv && zEnv[0] ){
        g_lib = try_load(zEnv);
        if( !g_lib ){
            snprintf(g_status, sizeof(g_status),
                     "VIKI_FOSSILSEE_LIB=%s did not load: %s", zEnv, dlerror());
            return;   /* an EXPLICIT path that fails is an error worth
                      ** reporting, not something to silently paper over
                      ** by searching elsewhere */
        }
    }
    for(i=0; !g_lib && lib_names[i]; i++) g_lib = try_load(lib_names[i]);

    if( !g_lib ){
        snprintf(g_status, sizeof(g_status),
                 "libfossilsee not found; using `%s` subprocess",
                 viki_fossil_binary());
        return;
    }
    if( !bind_symbols() ){
        snprintf(g_status, sizeof(g_status),
                 "libfossilsee loaded but is missing entry points; ignoring it");
        dlclose(g_lib);
        g_lib = 0;
        return;
    }
    if( g_abi() != VIKI_FOSSILSEE_ABI ){
        snprintf(g_status, sizeof(g_status),
                 "libfossilsee ABI %d, viki expects %d; ignoring it",
                 g_abi(), VIKI_FOSSILSEE_ABI);
        dlclose(g_lib);
        g_lib = 0;
        return;
    }
    snprintf(g_status, sizeof(g_status), "libfossilsee loaded (ABI %d)", g_abi());
}

/* ---- finding the repository -----------------------------------------
** viki runs `fossil sql` with no -R and lets fossil discover the repo
** from the checkout. In-process there is no such discovery, so we do it
** ourselves: `.fslckout` is a PLAIN SQLite database -- verified to be
** unencrypted even for a `.efossil` repo, whose path it stores in
** vvar.repository -- so viki reads it with the sqlite3 it already links,
** with no subprocess and no key.
**
** Walks up from cwd exactly as fossil does, so `viki index` works from a
** subdirectory of the checkout. */
static int find_repo(char *zOut, size_t nOut){
    char zDir[4096];
    const char *zEnv = getenv("VIKI_FOSSIL_REPO");

    if( zEnv && zEnv[0] ){
        snprintf(zOut, nOut, "%s", zEnv);
        return 1;
    }
    if( !getcwd(zDir, sizeof(zDir)) ) return 0;

    for(;;){
        char zCk[4200];
        sqlite3 *db = 0;
        char *zSlash;
        int got = 0;

        snprintf(zCk, sizeof(zCk), "%s/.fslckout", zDir);
        if( sqlite3_open_v2(zCk, &db, SQLITE_OPEN_READONLY, 0)==SQLITE_OK ){
            sqlite3_stmt *pStmt = 0;
            if( sqlite3_prepare_v2(db,
                    "SELECT value FROM vvar WHERE name='repository';",
                    -1, &pStmt, 0)==SQLITE_OK
             && sqlite3_step(pStmt)==SQLITE_ROW ){
                const unsigned char *z = sqlite3_column_text(pStmt, 0);
                if( z && z[0] ){ snprintf(zOut, nOut, "%s", (const char*)z); got = 1; }
            }
            sqlite3_finalize(pStmt);
        }
        sqlite3_close(db);
        if( got ) return 1;

        zSlash = strrchr(zDir, '/');
        if( !zSlash || zSlash == zDir ) return 0;   /* reached / */
        *zSlash = '\0';
    }
}

static int open_repo_if_needed(void){
    char zRepo[4096];
    int rc;

    if( g_repo ) return 1;
    if( !find_repo(zRepo, sizeof(zRepo)) ){
        snprintf(g_status, sizeof(g_status),
                 "libfossilsee loaded but no checkout found; using subprocess");
        return 0;
    }
    /* The key travels in FOSSIL_SEE_KEY exactly as it does for the CLI, so
    ** NULL here means "use the environment" rather than "no key". */
    rc = g_open(zRepo, 0, &g_repo);
    if( rc != FS_OK ){
        snprintf(g_status, sizeof(g_status),
                 "libfossilsee could not open %s (%s); using subprocess",
                 zRepo, g_errmsg(0));
        g_repo = 0;
        return 0;
    }
    return 1;
}

/* ---- public ---------------------------------------------------------- */

int viki_fossilsee_available(void){
    load_once();
    return g_lib != 0;
}

const char *viki_fossilsee_status(void){
    load_once();
    return g_status;
}

char *viki_fossilsee_sql_framed(const char *zSql, size_t *pnOut, int *pbUsed){
    Buf b;
    int rc;

    if( pbUsed ) *pbUsed = 0;
    if( pnOut ) *pnOut = 0;
    if( !viki_fossilsee_available() ) return 0;
    if( !open_repo_if_needed() ) return 0;

    memset(&b, 0, sizeof(b));
    rc = g_sql(g_repo, zSql, collect_row, &b);

    /* From here on the in-process path RAN, so the caller must not fall
    ** back: a real failure here is a real "not authoritative", which is
    ** exactly the signal the subprocess path could never give. */
    if( pbUsed ) *pbUsed = 1;

    if( b.oom ){ free(b.z); return 0; }
    if( rc != FS_OK ){
        fprintf(stderr, "viki: fossil query failed: %s\n", g_errmsg(g_repo));
        free(b.z);
        return 0;
    }
    if( !b.z ) buf_append(&b, "", 0);   /* zero rows is success, not NULL */
    if( pnOut ) *pnOut = b.n;
    return b.z;
}

void viki_fossilsee_shutdown(void){
    if( g_repo && g_close ){ g_close(g_repo); g_repo = 0; }
}
