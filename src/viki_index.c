#include "viki_index.h"
#include "viki_note.h"
#include "viki_cache.h" /* viki_fossil_binary/viki_fossil_user, shared subprocess config */
#include "sha256.h"

#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* rung-0-only placeholder model id, until an ONNX embedding pipeline
** exists (VIKI_DESIGN.md rung 1/2; see FINDINGS.md). Chunks stored under
** this id have embedding=NULL and are retrievable via FTS5 BM25 only. */
#define VIKI_MODEL_NONE "none"

/* Naive fixed-size line chunking. No overlap, no token-awareness. A
** documented placeholder -- see FINDINGS.md -- not a design decision to
** relitigate here (VIKI_DESIGN.md doesn't pin a chunking strategy). */
#define VIKI_CHUNK_LINES 40

static const char *SKIP_DIRS[] = {
    ".git", ".fslckout", "_FOSSIL_", ".viki", "vendor", "build", NULL
};

/* Per-namespace authority to INVALIDATE, one flag per extractor.
**
** A `viki index` run observes only some of viki_source's namespaces, and
** may delete rows only in the ones it can prove it observed. Each flag is
** set by its extractor and means "this run really saw this namespace, so a
** row it did not name is genuinely gone" -- never merely "the extractor was
** called". See sweep_sources() for what that buys and why the distinction
** is the difference between invalidation and data loss. */
typedef struct VikiAuth VikiAuth;
struct VikiAuth {
    int fs;       /* the directory walk enumerated its whole subtree */
    int wiki;
    int ticket;
    int forum;
    int ckin;     /* check-in comments */
    int note;     /* tech notes */
    int tchg;     /* ticket change artifacts */
    int attach;   /* attachment content */
    int uv;       /* unversioned file content */
};

/* The `path` column of viki_source holds two disjoint kinds of key: real
** filesystem paths produced by walk(), and VIRTUAL paths minted by the
** Fossil extractors. This table is what tells them apart AND what routes
** each one to its authority flag -- deliberately ONE table rather than the
** prefix list plus the parallel strncmp chain sweep_sources() used to
** carry, because those were two lists that had to be kept in sync by hand
** and the failure mode of forgetting one was silent (a namespace either
** un-invalidatable, or swept with somebody else's authority).
**
** Adding a content type means adding one row here and one field to
** VikiAuth; the compiler catches the second half. */
static const struct VirtualNs {
    const char *zPrefix;
    size_t nPrefix;
    size_t iAuth;      /* offsetof() the matching VikiAuth field */
} VIRTUAL_NS[] = {
    { "wiki:",   5, offsetof(VikiAuth, wiki)   },
    { "ticket:", 7, offsetof(VikiAuth, ticket) },
    { "forum:",  6, offsetof(VikiAuth, forum)  },
    { "ckin:",   5, offsetof(VikiAuth, ckin)   },
    { "note:",   5, offsetof(VikiAuth, note)   },
    { "tchg:",   5, offsetof(VikiAuth, tchg)   },
    { "attach:", 7, offsetof(VikiAuth, attach) },
    { "uv:",     3, offsetof(VikiAuth, uv)     },
    { NULL, 0, 0 }
};

/* True if zPath is SHAPED like a virtual path -- `<scheme>:` at the very
** start with no '/' before the colon -- whether or not VIRTUAL_NS knows
** that scheme.
**
** This is the safety net, and it is deliberately lexical rather than a
** lookup: a namespace some future extractor mints without registering it
** above must fall OUT of every sweep's scope, not into the filesystem
** branch. It would otherwise land there and be deleted, because
** path_in_dir("someprefix:key", ".") is true -- an unregistered namespace
** was previously protected only by the hand-maintained prefix list, i.e.
** by the very thing that had been forgotten.
**
** It cannot collide with a real filesystem path: walk() composes every
** path as "<dir>/<name>", so a walked path always carries a '/' before any
** ':' it might contain. */
static int looks_like_namespace(const char *zPath){
    const char *p = zPath;
    if( !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) ) return 0;
    while( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
        || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ) p++;
    return *p == ':';
}

/* Lexical-only normalization of zPath into zOut: collapses repeated '/',
** drops "." segments, strips a trailing '/'. "." and "./" normalize to the
** empty string (meaning "the cwd itself"). Returns 0 on success, nonzero
** if the result would not fit OR if any segment is ".." -- a path with
** ".." is deliberately refused rather than resolved, because resolving it
** correctly needs the filesystem (symlinks) and a path we cannot reason
** about must fall out of scope, never into it.
**
** Deliberately lexical, and deliberately NOT applied to what gets stored:
** viki_source rows keep the exact spelling `viki ask` prints (m1.sh
** asserts on `./docs/barn.md`), so this normalization exists only to make
** the scope test agree across spellings -- `viki index .` records
** `./docs/x.md`, `viki index docs` records `docs/x.md`, and both are the
** same file. Without it a stale row written under one spelling would be
** invisible to a sweep run under the other. */
static int canon_path(const char *zPath, char *zOut, size_t nOut){
    size_t n = 0;
    const char *p = zPath;

    if( nOut == 0 ) return 1;
    if( *p == '/' ){ if( n + 1 >= nOut ) return 1; zOut[n++] = '/'; while( *p == '/' ) p++; }
    while( *p ){
        const char *seg = p;
        size_t segLen;
        while( *p && *p != '/' ) p++;
        segLen = (size_t)(p - seg);
        while( *p == '/' ) p++;
        if( segLen == 0 ) continue;
        if( segLen == 1 && seg[0] == '.' ) continue;
        if( segLen == 2 && seg[0] == '.' && seg[1] == '.' ) return 1;
        if( n > 0 && zOut[n-1] != '/' ){ if( n + 1 >= nOut ) return 1; zOut[n++] = '/'; }
        if( n + segLen >= nOut ) return 1;
        memcpy(zOut + n, seg, segLen);
        n += segLen;
    }
    zOut[n] = '\0';
    return 0;
}

/* True if zPath names something strictly beneath directory zDir, judged
** lexically (see canon_path). Both arguments are interpreted in the same
** frame of reference -- the cwd `viki index` was run from -- which is
** sound because viki.c opens the cache db at a cwd-relative path too, so
** every row in a given cache.db was written from the same cwd.
**
** Answers 0 whenever it cannot be sure: a ".." anywhere, an absolute row
** path against a relative zDir (or the reverse), or an over-long path.
** That bias is the whole point -- a false 0 leaves a stale row in place
** (survivable: the store is derived and rebuildable, D-10), a false 1
** deletes live data. */
static int path_in_dir(const char *zPath, const char *zDir){
    char zP[4096], zD[4096];
    size_t nD;

    if( canon_path(zPath, zP, sizeof(zP)) ) return 0;
    if( canon_path(zDir, zD, sizeof(zD)) ) return 0;
    if( zD[0] == '\0' ){
        /* zDir is the cwd itself: it contains every RELATIVE path, and no
        ** absolute one (an absolute row may well point inside the cwd, but
        ** proving that needs realpath() on a file that, in the case we
        ** care about, has just been deleted). */
        return zP[0] != '\0' && zP[0] != '/';
    }
    if( strcmp(zD, "/") == 0 ) return zP[0] == '/' && zP[1] != '\0';
    nD = strlen(zD);
    return strncmp(zP, zD, nD) == 0 && zP[nD] == '/';
}

static int should_skip_dir(const char *name){
    int i;
    if( name[0] == '.' ) return 1;
    for( i = 0; SKIP_DIRS[i]; i++ ){
        if( strcmp(name, SKIP_DIRS[i]) == 0 ) return 1;
    }
    return 0;
}

/* Heuristic: treat as binary (skip) if a NUL byte appears in the first
** 8KB. Simple, standard, good enough for a skeleton. */
static int looks_binary(const char *data, size_t len){
    size_t i, n = len < 8192 ? len : 8192;
    for( i = 0; i < n; i++ ){
        if( data[i] == '\0' ) return 1;
    }
    return 0;
}

static char *read_whole_file(const char *path, size_t *outlen){
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;

    if( !f ) return NULL;
    if( fseek(f, 0, SEEK_END) != 0 ){ fclose(f); return NULL; }
    size = ftell(f);
    if( size < 0 ){ fclose(f); return NULL; }
    rewind(f);

    buf = malloc((size_t)size + 1);
    if( !buf ){ fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    *outlen = got;
    return buf;
}

static int chunk_count_already_present(sqlite3 *db, const char *hash, const char *modelId){
    sqlite3_stmt *st;
    int present = 0;
    if( sqlite3_prepare_v2(db,
            "SELECT 1 FROM viki_chunk WHERE content_hash=?1 AND model_id=?2 LIMIT 1",
            -1, &st, NULL) != SQLITE_OK ){
        return 0;
    }
    sqlite3_bind_text(st, 1, hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, modelId, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ) present = 1;
    sqlite3_finalize(st);
    return present;
}

static int insert_chunks(sqlite3 *db, const char *hash, const char *text, size_t len,
                          viki_embedder *emb, const char *modelId){
    sqlite3_stmt *stChunk, *stFts;
    const char *p = text;
    const char *end = text + len;
    int ix = 0;
    int rc = SQLITE_OK;
    float *vec = NULL;
    int dim = 0;

    if( emb ){
        dim = viki_embedder_dim(emb);
        vec = malloc(sizeof(float) * (size_t)dim);
    }

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO viki_chunk(content_hash, model_id, chunk_ix, chunk_text, embedding) "
        "VALUES(?1, ?2, ?3, ?4, ?5)", -1, &stChunk, NULL);
    if( rc != SQLITE_OK ){ free(vec); return rc; }
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO chunk_fts(chunk_text, content_hash, model_id, chunk_ix) "
        "VALUES(?1, ?2, ?3, ?4)", -1, &stFts, NULL);
    if( rc != SQLITE_OK ){ sqlite3_finalize(stChunk); free(vec); return rc; }

    while( p < end ){
        const char *chunk_start = p;
        int lines = 0;
        while( p < end && lines < VIKI_CHUNK_LINES ){
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            if( !nl ){ p = end; lines++; break; }
            p = nl + 1;
            lines++;
        }
        {
            size_t clen = (size_t)(p - chunk_start);
            int haveVec = 0;

            if( emb && clen > 0 ){
                /* chunk_text isn't NUL-terminated in place (it's a slice
                ** of the whole-file buffer); viki_embed wants a C string. */
                char *tmp = malloc(clen + 1);
                memcpy(tmp, chunk_start, clen);
                tmp[clen] = '\0';
                haveVec = (viki_embed(emb, tmp, vec) == 0);
                free(tmp);
            }

            sqlite3_bind_text(stChunk, 1, hash, -1, SQLITE_STATIC);
            sqlite3_bind_text(stChunk, 2, modelId, -1, SQLITE_STATIC);
            sqlite3_bind_int(stChunk, 3, ix);
            sqlite3_bind_text(stChunk, 4, chunk_start, (int)clen, SQLITE_STATIC);
            if( haveVec ){
                sqlite3_bind_blob(stChunk, 5, vec, (int)(sizeof(float) * (size_t)dim), SQLITE_TRANSIENT);
            }else{
                sqlite3_bind_null(stChunk, 5);
            }
            sqlite3_step(stChunk);
            sqlite3_reset(stChunk);

            sqlite3_bind_text(stFts, 1, chunk_start, (int)clen, SQLITE_STATIC);
            sqlite3_bind_text(stFts, 2, hash, -1, SQLITE_STATIC);
            sqlite3_bind_text(stFts, 3, modelId, -1, SQLITE_STATIC);
            sqlite3_bind_int(stFts, 4, ix);
            sqlite3_step(stFts);
            sqlite3_reset(stFts);
        }
        ix++;
    }

    sqlite3_finalize(stChunk);
    sqlite3_finalize(stFts);
    free(vec);
    return SQLITE_OK;
}

static void upsert_source(sqlite3 *db, const char *path, const char *hash, long mtime){
    sqlite3_stmt *st;
    if( sqlite3_prepare_v2(db,
            "INSERT INTO viki_source(path, content_hash, mtime) VALUES(?1, ?2, ?3) "
            "ON CONFLICT(path) DO UPDATE SET content_hash=excluded.content_hash, mtime=excluded.mtime",
            -1, &st, NULL) != SQLITE_OK ){
        return;
    }
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 3, mtime);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* Records that zPath is a LIVE source as of this run -- i.e. that its
** viki_source row now reflects current content. The sweep below deletes
** exactly the in-scope rows this table does not name, so every early
** return on a path that still exists must call this or that path's
** content gets dropped.
**
** A TEMP table, not a column on viki_source, on purpose: `viki cache
** push` ships the cache db verbatim to peers (D-12), and a per-run marker
** is meaningless -- worse, misleading -- in someone else's copy. temp.*
** lives in a scratch file that dies with the connection. */
static void mark_seen(sqlite3 *db, const char *zPath){
    sqlite3_stmt *st;
    if( sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO temp.viki_seen(path) VALUES(?1)",
                           -1, &st, NULL) != SQLITE_OK ) return;
    sqlite3_bind_text(st, 1, zPath, -1, SQLITE_STATIC);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

static int previously_seen_unchanged(sqlite3 *db, const char *path, long mtime, char *outhash){
    sqlite3_stmt *st;
    int unchanged = 0;
    if( sqlite3_prepare_v2(db,
            "SELECT content_hash, mtime FROM viki_source WHERE path=?1",
            -1, &st, NULL) != SQLITE_OK ){
        return 0;
    }
    sqlite3_bind_text(st, 1, path, -1, SQLITE_STATIC);
    if( sqlite3_step(st) == SQLITE_ROW ){
        long stored_mtime = (long)sqlite3_column_int64(st, 1);
        if( stored_mtime == mtime ){
            const unsigned char *h = sqlite3_column_text(st, 0);
            if( h ){
                strncpy(outhash, (const char*)h, 64);
                outhash[64] = '\0';
                unchanged = 1;
            }
        }
    }
    sqlite3_finalize(st);
    return unchanged;
}

/* Shared by real files and virtual (wiki/ticket) sources: hash, chunk if
** this exact (hash, model_id) isn't already present, upsert viki_source.
** mtime is a fast-skip optimization only -- pass 0 for virtual sources
** (no meaningful filesystem mtime), which just means they're always
** re-hashed on every `viki index` run; content-hash dedup still avoids
** redundant re-chunking when the content itself hasn't changed. */
static void index_text_blob(sqlite3 *db, const char *virtualPath, const char *data, size_t len,
                             long mtime, viki_embedder *emb, const char *modelId, int *nChunked){
    char hash[65];
    char cached_hash[65];

    /* Note the ordering: a source whose content is now EMPTY is not marked
    ** seen, so the sweep drops its row and its old text stops being
    ** retrievable. Emptying a file is a withdrawal of content, not an
    ** absence of information about it. */
    if( len == 0 ) return;

    mark_seen(db, virtualPath);

    if( mtime != 0 && previously_seen_unchanged(db, virtualPath, mtime, cached_hash)
        && chunk_count_already_present(db, cached_hash, modelId) ){
        return;
    }

    viki_sha256_hex(data, len, hash);

    if( !chunk_count_already_present(db, hash, modelId) ){
        insert_chunks(db, hash, data, len, emb, modelId);
        (*nChunked)++;
    }
    upsert_source(db, virtualPath, hash, mtime);
}

static int index_file(sqlite3 *db, const char *path, int *nFiles, int *nChunked,
                       viki_embedder *emb, const char *modelId){
    struct stat st;
    char *data;
    size_t len;

    if( stat(path, &st) != 0 || !S_ISREG(st.st_mode) ) return 0;

    (*nFiles)++;

    data = read_whole_file(path, &len);
    if( !data ){
        /* stat() said this is a regular file, so it is still there -- we
        ** just could not read it this run (permissions, I/O error). That
        ** is a failure to observe, not evidence the content was withdrawn,
        ** so keep the existing row: "when in doubt, keep". */
        mark_seen(db, path);
        return 0;
    }
    /* Deliberately NOT marked seen: a file that has become binary has no
    ** indexable text any more, and continuing to serve the text it used to
    ** hold is the same defect as serving a deleted file's. */
    if( looks_binary(data, len) ){ free(data); return 0; }

    index_text_blob(db, path, data, len, (long)st.st_mtime, emb, modelId, nChunked);

    free(data);
    return 0;
}

/* Runs argv (NULL-terminated) as a child process and captures its stdout
** into a malloc'd, NUL-terminated buffer (caller frees). stderr is
** discarded (fossil's own warnings/prompts would otherwise pollute
** content we're about to index). Returns NULL on fork/pipe failure; a
** nonzero child exit code is not itself treated as failure here --
** whatever was captured (often nothing) is still returned, since e.g.
** `fossil ticket show 0` legitimately exits nonzero on an empty ticket
** table in some fossil versions and the caller can just get an empty
** result either way.
**
** *pExit (optional) receives the child's exit status, or -1 if it was
** signalled or never ran. That distinction is what makes invalidation
** safe: an empty capture from `fossil wiki list` means "this repo has no
** wiki pages" when the child exited 0, and "there is no usable fossil
** binary here" when it exited 127 -- and only the first licenses deleting
** every wiki: row in the cache. Before this out-param the two were
** indistinguishable, so no caller could have swept safely.
**
** *pnOut (optional) receives the captured byte count. A NUL terminator is
** always written past it so string-shaped output stays usable, but for
** attachment and unversioned payloads -- which are arbitrary bytes and
** routinely contain NUL -- strlen() would silently truncate the content
** mid-artifact and looks_binary() would then never see the bytes that
** prove it binary. Any caller handling a payload that is not known-text
** MUST take the length. */
static char *run_capture(char *const argv[], int *pExit, size_t *pnOut){
    int pipefd[2];
    pid_t pid;
    char *buf;
    size_t cap = 65536, len = 0;

    if( pExit ) *pExit = -1;
    if( pnOut ) *pnOut = 0;
    if( pipe(pipefd) != 0 ) return NULL;
    pid = fork();
    if( pid < 0 ){ close(pipefd[0]); close(pipefd[1]); return NULL; }

    if( pid == 0 ){
        int devnull;
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        devnull = open("/dev/null", O_WRONLY);
        if( devnull >= 0 ){ dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    buf = malloc(cap);
    if( !buf ){ close(pipefd[0]); waitpid(pid, NULL, 0); return NULL; }
    for(;;){
        ssize_t n;
        /* +1 for the NUL written below: the old test was `len + 4096 > cap`,
        ** which permits a read that lands len exactly ON cap and then writes
        ** buf[cap] -- a one-byte heap overflow reachable from any capture
        ** whose size hits a power of two (65536 is one pipe's worth). */
        if( len + 4096 + 1 > cap ){
            char *bufNew = realloc(buf, cap * 2);
            if( !bufNew ) break;   /* keep what we have; the caller's framing
                                   ** check will reject a truncated capture */
            buf = bufNew;
            cap *= 2;
        }
        n = read(pipefd[0], buf + len, 4096);
        if( n <= 0 ) break;
        len += (size_t)n;
    }
    close(pipefd[0]);
    {
        int status;
        if( waitpid(pid, &status, 0) >= 0 && WIFEXITED(status) && pExit ){
            *pExit = WEXITSTATUS(status);
        }
    }
    buf[len] = '\0';
    if( pnOut ) *pnOut = len;
    return buf;
}

/* ---------------------------------------------------------------------
** COUNTED FRAMING over a single `fossil sql` call.
**
** WHY THIS EXISTS AT ALL. index_forum()'s original "list the ids, then
** fetch each artifact in its own subprocess" idiom is correct but its cost
** is O(N) SUBPROCESSES, and on an ENCRYPTED repository a fossil subprocess
** costs ~470 ms of SQLCipher key derivation before it does any work --
** size-independent, ~71x the plaintext cost, measured over 10 invocations
** each (FINDINGS.md). One `viki index` on a four-file checkout is 1.51 s
** wall of which 0.010 s is viki. Extracting 2,001 check-in comments that
** way would take ~16 minutes; the framed form below does it in 25 ms.
**
** The reason the old idiom existed is still valid and is NOT being
** relitigated: `fossil sql`'s default output is pipe-separated with RAW
** embedded newlines, so multi-line values cannot be split back into rows.
** Counted framing satisfies the same requirement a different way -- the
** payload is never scanned for a delimiter, it is COUNTED:
**
**     <key> <nBytes> <free header text>\n
**     <exactly nBytes of payload>
**     \n                     <- the newline `fossil sql` appends per row
**
** Only tokens 1 and 2 are parsed. The REST OF THE HEADER LINE IS OPAQUE,
** which is what lets a branch name, a filename or a username sit in it
** without any escaping and without being able to desynchronise the parse.
**
** TWO TRAPS, both measured, both fatal if skipped:
**
**   1. `length()` on TEXT counts CHARACTERS, not bytes -- one accented
**      commit message measures length()=55 and
**      length(cast(X AS BLOB))=70. Every count below casts to BLOB or the
**      framing desynchronises on the first non-ASCII artifact.
**   2. `fossil sql` EXITS 0 WHEN THE QUERY FAILS. It exits nonzero only
**      when the repository cannot be opened. Exit status is therefore not
**      authority -- see fossil_sql_framed().
*/
#define VIKI_FRAME_EOF "#viki-eof"

/* Wraps a SQL expression so it cannot contain a raw newline. Only ever
** applied to free-header-text fields (branch, user, filename, target).
** Fossil rejects newlines in these today, so this is belt-and-braces
** against a repo that acquired one some other way -- a header line that
** wrapped would shift the payload start and desynchronise everything
** after it. The payload itself is never sanitized: it is counted. */
#define VIKI_ONELINE(x) "replace(replace(" x ",char(10),' '),char(13),' ')"

typedef struct FramedRec FramedRec;
struct FramedRec {
    char *zKey;        /* header token 1 -- NUL-terminated in place */
    char *zHdr;        /* rest of the header line, opaque free text ("" if none) */
    char *zBody;       /* nBody bytes, also NUL-terminated in place */
    size_t nBody;
};

typedef struct FramedIter FramedIter;
struct FramedIter {
    char *z;
    char *zEnd;
    int seenEof;       /* the VIKI_FRAME_EOF header line was reached */
};

static void framed_init(FramedIter *p, char *zBuf, size_t nBuf){
    p->z = zBuf;
    p->zEnd = zBuf + nBuf;
    p->seenEof = 0;
}

/* Advances to the next record. Returns 1 and fills *pRec, or 0 at the end.
**
** Every stop short of the sentinel leaves p->seenEof == 0, which the
** callers turn into "not authoritative". That is the whole design: a
** malformed frame, a count that overruns the capture, or a missing
** row-separator newline are all indistinguishable from a truncated
** capture, and none of them may license deleting anything. */
static int framed_next(FramedIter *p, FramedRec *pRec){
    char *nl, *sp, *end;
    long nBody;

    if( p->z >= p->zEnd ) return 0;
    nl = memchr(p->z, '\n', (size_t)(p->zEnd - p->z));
    if( !nl ) return 0;
    *nl = '\0';
    if( strcmp(p->z, VIKI_FRAME_EOF) == 0 ){
        p->seenEof = 1;
        p->z = nl + 1;
        return 0;
    }
    sp = strchr(p->z, ' ');
    if( !sp ) return 0;
    *sp = '\0';
    pRec->zKey = p->z;
    nBody = strtol(sp + 1, &end, 10);
    if( end == sp + 1 || nBody < 0 ) return 0;
    if( *end == ' ' ) end++;
    pRec->zHdr = end;
    pRec->zBody = nl + 1;
    /* The body must be followed by the row-separator newline fossil emits
    ** after every value. Requiring it is a free end-to-end integrity check
    ** on the declared count: if the two disagree the frame is not what we
    ** think it is, and we stop rather than reinterpret arbitrary bytes as
    ** the next header. */
    if( (size_t)nBody + 1 > (size_t)(p->zEnd - pRec->zBody) ) return 0;
    if( pRec->zBody[nBody] != '\n' ) return 0;
    pRec->zBody[nBody] = '\0';
    pRec->nBody = (size_t)nBody;
    p->z = pRec->zBody + nBody + 1;
    return 1;
}

/* Runs one `fossil sql --readonly` script and returns its raw stdout
** (caller frees), with *pnOut set. Returns NULL when the extraction is not
** worth trusting at all.
**
** THE AUTHORITY RULE, and it is not the exit status. `fossil sql` exits 0
** when the QUERY fails -- measured: `SELECT count(*) FROM forumpost` on a
** repo that never had a forum prints an error to stderr (which
** run_capture() discards) and exits 0; `SELEKT 1;` likewise. It exits
** nonzero only when the REPOSITORY cannot be opened. So every extractor
** appends `SELECT '#viki-eof';` and the caller's framed walk demands that
** sentinel: a failing statement ABORTS the rest of the script (measured --
** statement 2 does not run when statement 1 fails to prepare), so the
** sentinel is emitted if and only if the real query prepared AND ran to
** completion.
**
** This is the fix for a real latent bug, not hygiene. index_forum()'s
** comment claimed a missing `forumpost` table "makes the query FAIL, which
** reads as 'not authoritative'". It does not: rc == 0, the empty output
** looks exactly like "this repo has no forum posts", and sweep_sources()
** then DELETES EVERY forum: ROW IN THE CACHE. Reproduced live on a repo
** that never had a forum post. */
static char *fossil_sql_framed(const char *zSql, size_t *pnOut){
    const char *zFossil = viki_fossil_binary();
    char *argv[] = { (char*)zFossil, "sql", "--readonly", (char*)zSql, NULL };
    char *zOut;
    int rc = -1;

    zOut = run_capture(argv, &rc, pnOut);
    if( !zOut ) return NULL;
    if( rc != 0 ){ free(zOut); *pnOut = 0; return NULL; }
    return zOut;
}

/* Composes "<zHeader>\n\n<body>" -- the frozen shape every new virtual
** class uses. The header line is built entirely in SQL (see each
** extractor) so that it arrives here as the opaque free-header text and
** needs no field-splitting on this side; a username or branch containing a
** space therefore cannot shift anything.
**
** The composed bytes ARE the content_hash preimage, so this recipe is part
** of what peers must agree on -- see the contract note in viki_cmd_index(). */
static char *compose_headed(const char *zHeader, const char *zBody, size_t nBody,
                             size_t *pnOut){
    size_t nHdr = strlen(zHeader);
    size_t n = nHdr + 2 + nBody;
    char *z = malloc(n + 1);
    if( !z ) return NULL;
    memcpy(z, zHeader, nHdr);
    z[nHdr] = '\n';
    z[nHdr + 1] = '\n';
    memcpy(z + nHdr + 2, zBody, nBody);
    z[n] = '\0';
    *pnOut = n;
    return z;
}

/* Undoes `fossil ticket show ... --quote`'s escaping in place (the
** unescaped result is never longer than the input, so this is safe to
** do without a second buffer). See FINDINGS.md: --quote is the only
** reliable way to TSV-parse ticket content, since an unquoted comment
** containing a literal tab or newline would otherwise corrupt column/row
** boundaries. */
static void unquote_fossil(char *s){
    char *w = s;
    while( *s ){
        if( *s == '\\' && s[1] ){
            switch( s[1] ){
                case 's': *w++ = ' '; break;
                case 't': *w++ = '\t'; break;
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 'f': *w++ = '\f'; break;
                case 'v': *w++ = '\v'; break;
                case '0': *w++ = '\0'; break;
                case '\\': *w++ = '\\'; break;
                default: *w++ = s[1]; break;
            }
            s += 2;
        }else{
            *w++ = *s++;
        }
    }
    *w = '\0';
}

/* `viki index`'s wiki extraction: `fossil wiki list` for page names, then
** `fossil wiki export NAME -` per page. Always re-extracts (no mtime to
** fast-skip on); content-hash dedup in index_text_blob still avoids
** redundant chunking for unchanged pages.
**
** Returns 1 only if this run is AUTHORITATIVE for the wiki: namespace --
** i.e. `fossil wiki list` really ran and succeeded, and every page it
** named was exported successfully. Anything less returns 0 and the sweep
** leaves every wiki: row alone, because a failed extraction is
** indistinguishable, in the resulting empty output, from a repo whose
** wiki was genuinely emptied. */
static int index_wiki(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    char *argvList[] = { (char*)fossil, "wiki", "list", NULL };
    char *listOut;
    char *line, *saveptr;
    int rcList = -1;
    int authoritative = 1;

    listOut = run_capture(argvList, &rcList, NULL);
    if( !listOut ) return 0;
    if( rcList != 0 ){ free(listOut); return 0; }

    line = strtok_r(listOut, "\n", &saveptr);
    while( line ){
        while( *line == ' ' || *line == '\t' ) line++;
        if( line[0] ){
            char *argvExport[] = { (char*)fossil, "wiki", "export", line, "-", NULL };
            int rcExport = -1;
            char *content = run_capture(argvExport, &rcExport, NULL);
            if( content ){
                char virtualPath[512];
                snprintf(virtualPath, sizeof(virtualPath), "wiki:%s", line);
                (*nItems)++;
                index_text_blob(db, virtualPath, content, strlen(content), 0, emb, modelId, nChunked);
                free(content);
            }
            if( rcExport != 0 ) authoritative = 0;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(listOut);
    return authoritative;
}

/* Finds a "W <n>\n" card in a raw Fossil manifest and returns a pointer
** to the n bytes that follow (the counted-string payload -- this is how
** Fossil manifests embed arbitrary multi-line text without needing to
** escape it, unlike single-line cards). *outLen receives n. Returns NULL
** if no well-formed W card is found. Now verified against REAL forum
** manifests (thread-start, reply, and edit), not just a wiki artifact --
** see FINDINGS.md. The comment that used to live here claimed a forum
** post could not be created without Fossil's "AJAX-driven web UI"; that
** was wrong (the blocker was a missing Referer: header), and the byte
** counted W-card path is correct on real forum artifacts. Fails soft
** (returns NULL, caller skips the post) rather than guessing on a
** malformed card. */
static const char *find_w_card(const char *manifest, size_t *outLen){
    const char *p = manifest;
    while( *p ){
        if( (p == manifest || p[-1] == '\n') && p[0] == 'W' && p[1] == ' ' ){
            char *end;
            long n = strtol(p + 2, &end, 10);
            if( end > p + 2 && *end == '\n' && n >= 0 ){
                *outLen = (size_t)n;
                return end + 1;
            }
        }
        p++;
    }
    return NULL;
}

/* Single-line card value, e.g. "H <title>\n" -- returns a malloc'd copy
** of the rest of the line, or NULL if the card isn't present. Forum
** threads carry an H (title) card only on the first post; replies don't
** have one, which is fine -- the post is still indexed under its own
** virtual path, just without a human-friendly title prefix.
**
** zLimit bounds the search and is NOT optional: it must be the first byte
** of the W card's counted body. Manifest cards are sorted alphabetically
** (D G H I N P U W Z), so every single-line card of interest precedes the
** W body -- but the body is arbitrary user text that can itself contain a
** line starting with "H ". Without the bound, a REPLY (which has no H
** card at all) happily "finds" such a body line and prepends it as a
** title, duplicating it into the chunk. Reproduced against a live reply
** whose body listed water chemistry: "H 7.9 pH at the wellhead ..." was
** indexed as that post's title. See FINDINGS.md.
**
** The value is returned still-escaped; callers that put it in front of
** human text must run it through unquote_fossil(). */
static char *find_line_card(const char *manifest, const char *zLimit, char cardLetter){
    const char *p = manifest;
    while( p + 2 <= zLimit && *p ){
        if( (p == manifest || p[-1] == '\n') && p[0] == cardLetter && p[1] == ' ' ){
            const char *start = p + 2;
            const char *nl = memchr(start, '\n', (size_t)(zLimit - start));
            size_t len = nl ? (size_t)(nl - start) : (size_t)(zLimit - start);
            char *out = malloc(len + 1);
            memcpy(out, start, len);
            out[len] = '\0';
            return out;
        }
        p++;
    }
    return NULL;
}

/* Sinks ONE artifact whose text lives in a counted W card -- forum posts
** and tech notes have byte-identical structure here, differing only in
** which single-line card holds the title (`H` vs `C`).
**
** zHeader is an optional provenance line prefixed ahead of everything
** else, and it is NULL for forum posts ON PURPOSE: `content_hash` is
** sha256(this composed text), so giving forum posts a header now would
** re-key every forum: row in every shared D-11 cache for no retrieval
** gain. Tech notes are new, so they get one.
**
** A parse failure is never treated as absence: a W-card artifact is
** IMMUTABLE and keyed by its own uuid, so failing to re-read it is a
** failure to observe unchanging content, never a withdrawal of it. Mark it
** seen so the sweep keeps whatever an earlier, successful run parsed. */
static void index_w_card_artifact(sqlite3 *db, const char *zPath,
                                   const char *zHeader, char cTitleCard,
                                   const char *zManifest,
                                   viki_embedder *emb, const char *modelId,
                                   int *nItems, int *nChunked){
    size_t bodyLen = 0;
    const char *body = find_w_card(zManifest, &bodyLen);
    char *title = NULL;
    char *combined;
    size_t nCombined = 0;
    int haveTitle = 0;

    /* Bound the W count against the bytes that actually follow the W card,
    ** not against the whole manifest's length -- the latter includes the
    ** ~150 bytes of cards BEFORE the body, so a manifest whose W count
    ** overruns its own payload would still pass that test and be read past
    ** its end. (Hardening: found by reading, never triggered by a real
    ** artifact, since Fossil validates a manifest through manifest_parse()
    ** before storing it.) */
    if( !(body && bodyLen > 0 && bodyLen <= strlen(body)) ){
        mark_seen(db, zPath);
        return;
    }

    (*nItems)++;
    title = find_line_card(zManifest, body, cTitleCard);
    if( title ){
        /* Single-line manifest cards are escaped (a space in a thread title
        ** is stored as `\s`), so the title card MUST be run through
        ** unquote_fossil() -- the same mapping as Fossil's own
        ** defossilize(), which Fossil applies to this exact field at
        ** manifest.c's `defossilize(p->zThreadTitle)`. Skipping it does not
        ** merely look ugly: FTS5 then tokenizes `\sseal` as one word, so
        ** every title word after the first becomes unsearchable. See
        ** FINDINGS.md. The W-card body needs no such decoding -- it is a
        ** counted string, not escaped. */
        unquote_fossil(title);
        /* "title\n\nbody" so the title contributes to both BM25 and the
        ** embedding, not just the body. */
        combined = compose_headed(title, body, bodyLen, &nCombined);
        free(title);
        haveTitle = 1;
    }else if( zHeader ){
        combined = compose_headed(zHeader, body, bodyLen, &nCombined);
    }else{
        index_text_blob(db, zPath, body, bodyLen, 0, emb, modelId, nChunked);
        return;
    }
    if( !combined ){ mark_seen(db, zPath); return; }

    if( zHeader && haveTitle ){
        /* header, then title, then body. Composed in two steps rather than
        ** one three-way concatenation so that the forum path
        ** (zHeader == NULL) is provably byte-identical to what it produced
        ** before this refactor -- no forum content_hash may move. */
        size_t nFull = 0;
        char *full = compose_headed(zHeader, combined, nCombined, &nFull);
        free(combined);
        if( !full ){ mark_seen(db, zPath); return; }
        combined = full;
        nCombined = nFull;
    }
    index_text_blob(db, zPath, combined, nCombined, 0, emb, modelId, nChunked);
    free(combined);
}

/* `viki index`'s forum extraction: every CURRENT forum-post artifact's raw
** manifest, in ONE `fossil sql` call using the counted framing above
** (event.type='f'; verified this is the correct type code directly from
** `fossil help timeline`'s --type list).
**
** This used to be the two-step "list the uuids, then fetch each artifact
** in its own subprocess" shape. The REASON for that shape was sound and is
** unchanged -- `fossil sql`'s default pipe-separated output cannot be
** split back into rows when a value contains raw newlines, which every
** manifest does -- but its cost was 1+N subprocesses at ~470 ms each on an
** encrypted repo. Counted framing satisfies the same requirement in one
** call. The extracted text is byte-identical either way (the W-card body
** is a counted string; only the transport changed), so no `content_hash`
** moves and no shared D-11 cache is invalidated by this conversion.
**
** The `NOT IN (SELECT fprev ...)` clause is load-bearing, not tidiness.
** Editing a forum post does not rewrite anything -- it appends a NEW
** artifact carrying a `P <old-uuid>` card, and the superseded artifact
** stays in `event` with type='f' forever. Selecting on event.type alone
** therefore indexes every historical revision of every edited post and
** hands the stale text back from `viki ask` as if it were current, while
** Fossil's own /forumthread page shows only the newest. The repository
** already records which is which: `forumpost.fprev` names the artifact
** each edit replaces, so a superseded post is exactly one that appears as
** some other row's fprev. Reproduced and then re-verified against a live
** edited reply -- see FINDINGS.md.
**
** THE AUTHORITY BUG THIS FIXES, which the comment that used to live here
** got backwards. A repository that never held a forum post has no
** `forumpost` table at all (it is created on the first crosslink), so this
** query fails with "no such table: forumpost". The old comment claimed
** that failure "reads as 'not authoritative'", and called relying on it
** "harmless by luck". It was neither: `fossil sql` EXITS 0 WHEN A QUERY
** FAILS, so rc == 0, `authoritative` came back 1, the empty output looked
** exactly like "this repo has no forum posts", and sweep_sources() then
** deleted EVERY forum: ROW IN THE CACHE. Reproduced live on a repo that
** has never had a forum post. The `SELECT '#viki-eof';` sentinel is what
** actually separates the two cases now -- a failing statement aborts the
** rest of the script, so the sentinel appears iff the real query prepared
** AND ran.
**
** Returns 1 only if the query prepared, ran to completion (sentinel
** reached), and every record framed correctly. */
static int index_forum(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT b.uuid || ' ' || length(cast(content(b.uuid) AS BLOB))"
        "    || char(10) || content(b.uuid)"
        "  FROM event e JOIN blob b ON b.rid = e.objid"
        " WHERE e.type = 'f'"
        "   AND b.rid NOT IN (SELECT fprev FROM forumpost WHERE fprev IS NOT NULL)"
        " ORDER BY e.mtime;"
        "SELECT '" VIKI_FRAME_EOF "';";
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        snprintf(virtualPath, sizeof(virtualPath), "forum:%s", rec.zKey);
        index_w_card_artifact(db, virtualPath, NULL, 'H', rec.zBody,
                              emb, modelId, nItems, nChunked);
    }
    free(zOut);
    return it.seenEof;
}

/* `viki index`'s ticket extraction: `fossil ticket show 0 --quote` dumps
** every ticket as one TSV row (report 0 = all columns defined in the
** TICKET table, so this doesn't depend on any project-specific saved
** report existing). --quote is required for safe parsing -- see
** unquote_fossil(). Columns are located by header name rather than
** assumed fixed positions, since custom installations can add ticket
** fields (per `fossil help ticket`). */
/* Splits s in place on sep, writing up to maxOut field-start pointers to
** out[] (empty fields ARE preserved as zero-length strings) and returns
** the count. strtok_r is the wrong tool for this: it collapses runs of
** consecutive delimiters instead of yielding empty tokens between them,
** which silently shifts every later column left whenever a row has
** adjacent empty fields -- `fossil ticket show`'s TSV output routinely
** does (type/status/subsystem/priority/severity/foundin/private_contact/
** resolution are all empty on a freshly-added ticket, eight empty fields
** in a row) and produced exactly that corruption before this fix: the
** comment text ended up labeled "Status:" and the title vanished
** entirely. Found by comparing indexed output against the raw TSV. */
static int split_preserve_empty(char *s, char sep, char **out, int maxOut){
    int n = 0;
    if( maxOut <= 0 ) return 0;
    out[n++] = s;
    while( *s && n < maxOut ){
        if( *s == sep ){
            *s = '\0';
            out[n++] = s + 1;
        }
        s++;
    }
    return n;
}

/* Returns 1 only if `fossil ticket show` really ran and exited 0, which is
** this run's authority to invalidate ticket: rows. Note FINDINGS.md: some
** fossil versions exit nonzero on an empty ticket table, and this refuses
** to sweep in that case. That costs nothing -- a repo with no tickets and
** no ticket: rows has nothing to sweep anyway -- and it is the only rule
** that also covers the cases that matter: no fossil binary on PATH, no
** resolvable user, or cwd outside a checkout, each of which yields the
** same empty output as "this repo has no tickets". */
static int index_tickets(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    const char *user = viki_fossil_user();
    char *argv[] = { (char*)fossil, "ticket", "show", "0", "--quote", "--user", (char*)user, NULL };
    char *out;
    char *lineStart = NULL, *p;
    int uuidCol = -1, titleCol = -1, commentCol = -1, statusCol = -1;
    int isHeader = 1;
    int rcShow = -1;

    out = run_capture(argv, &rcShow, NULL);
    if( !out ) return 0;

    lineStart = out;
    for( p = out; ; p++ ){
        int atEnd = (*p == '\0');
        if( *p == '\n' || atEnd ){
            char *line = lineStart;
            int lineLen = (int)(p - lineStart);
            char *fields[32];
            int nFields;

            *p = '\0'; /* terminate this line (harmless no-op at atEnd) */
            lineStart = p + 1;
            if( lineLen == 0 ){ if( atEnd ) break; else continue; }

            nFields = split_preserve_empty(line, '\t', fields, 32);

            if( isHeader ){
                int i;
                for( i = 0; i < nFields; i++ ){
                    if( strcmp(fields[i], "tkt_uuid") == 0 ) uuidCol = i;
                    else if( strcmp(fields[i], "title") == 0 ) titleCol = i;
                    else if( strcmp(fields[i], "comment") == 0 ) commentCol = i;
                    else if( strcmp(fields[i], "status") == 0 ) statusCol = i;
                }
                isHeader = 0;
            }else if( uuidCol >= 0 && uuidCol < nFields ){
                char buf[8192];
                int off = 0;
                int i;
                char virtualPath[512];
                for( i = 0; i < nFields; i++ ) unquote_fossil(fields[i]);

                off += snprintf(buf + off, sizeof(buf) - (size_t)off, "Ticket %s\n", fields[uuidCol]);
                if( statusCol >= 0 && statusCol < nFields && fields[statusCol][0] )
                    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "Status: %s\n", fields[statusCol]);
                if( titleCol >= 0 && titleCol < nFields && fields[titleCol][0] )
                    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "Title: %s\n", fields[titleCol]);
                if( commentCol >= 0 && commentCol < nFields && fields[commentCol][0] )
                    snprintf(buf + off, sizeof(buf) - (size_t)off, "%s\n", fields[commentCol]);

                snprintf(virtualPath, sizeof(virtualPath), "ticket:%s", fields[uuidCol]);
                (*nItems)++;
                index_text_blob(db, virtualPath, buf, strlen(buf), 0, emb, modelId, nChunked);
            }

            if( atEnd ) break;
        }
    }
    free(out);
    return rcShow == 0;
}

/* ---------------------------------------------------------------------
** THE REST OF FOSSIL STATE.
**
** Everything below indexes an artifact class the checkout does not
** contain. They exist because the measurement said so, not because the
** list looked incomplete: over `test/retrieval-queries.tsv`, 16 of 59
** questions had their answer ONLY in an artifact `viki index` could not
** read, and all 16 failed at every rank. Nine of those 16 live in
** CHECK-IN COMMENTS. A memory that ranks perfectly over a corpus missing
** the commit log is still a bad memory.
**
** Two properties every one of them shares, and neither is optional:
**
**   SUPERSESSION. Each class has its OWN shape and the forum bug (every
**   historical revision indexed as current) generalises to exactly one of
**   them. Measured, per class:
**     - check-in: ONE event row, updated in place. `fossil amend` writes
**       event.ecomment and leaves event.comment holding the pre-amend
**       text, so `coalesce(ecomment, comment)` is the current revision --
**       and it is Fossil's own timeline rule, not an invention here.
**     - tech note: ONE event row. manifest.c:2543 DELETEs the superseded
**       row before re-inserting, so `event.type='e'` alone already means
**       "current" -- again Fossil's own selector (wiki.c:2142's
**       wiki_technote_to_rid() resolves a technote by exactly this join).
**       `tagxref` still holds both revisions; do not select from there.
**     - ticket change: append-only and IMMUTABLE by design. No filter
**       needed or wanted -- superseded field values are precisely the
**       content this class exists to keep.
**     - attachment: superseded versions get isLatest=0, and DELETION sets
**       src='' rather than NULL.
**     - unversioned: latest-wins by construction; only current bytes exist.
**
**   AUTHORITY. Each returns 1 only when its query prepared, ran to the
**   sentinel, AND every record framed correctly -- never merely "the
**   subprocess was spawned". sweep_sources() turns that into permission to
**   delete, and a wrong deletion is unrecoverable.
**
** THE COMPOSED-TEXT CONTRACT, which nothing in viki-manifest covers today
** and which the header format below is therefore frozen against. Because
** `content_hash = sha256(the composed extracted text)`, the COMPOSITION
** RECIPE is de facto part of D-11's sharing contract. Two peers on
** different viki versions that compose a check-in header differently
** produce different hashes for the same check-in, both embed it, and both
** appear in `viki ask`. That is cache FRAGMENTATION, not corruption, and
** it is NOT an epoch bump (model_id and chunk_params are untouched) -- but
** it does mean these header lines must not be changed casually, and any
** later change to one must be called out as cache-fragmenting.
**
** WHY THE TIME GOES IN THE TEXT AND NOT IN viki_source.mtime.
** index_text_blob() takes a fast-skip path when mtime != 0 and the stored
** mtime matches -- and `fossil amend` changes ecomment while leaving the
** check-in's own time alone, so storing the artifact's real timestamp
** would make the skip permanently serve the PRE-amend text. Time belongs
** in the composed header, where it is both FTS-indexed and embedded.
*/

/* Composes the frozen provenance header for a class entirely in SQL, so it
** reaches this file as the framed record's opaque free-header text and
** needs no field-splitting here. A username or branch containing a space
** therefore cannot shift anything.
**
** FROZEN header formats (see the contract note above):
**   ckin:    check-in <uuid16> on <ISO-8601> by <user> branch <branch>
**   note:    tech note <id16> on <ISO-8601> by <user>
**   tchg:    ticket <uuid16> changed <ISO-8601> by <user>
**   attach:  attachment <filename> on <target> at <ISO-8601>
**   uv:      unversioned file <name>
** followed by a blank line and then the content. `uv:` deliberately
** carries NO timestamp: a uv blob's mtime moves on every re-upload even
** when the bytes are identical, and that would re-hash and re-embed
** unchanged content on every sync.
*/

/* `viki index`'s CHECK-IN COMMENT extraction -- the largest coverage gap
** in the tool and the reason this round exists. The timeline is the record
** of what was done and why; nothing indexed it.
**
** Keyed on the CHECK-IN UUID, which is stable forever. An amend therefore
** changes the composed text under a STABLE key, so index_text_blob()
** re-hashes it and gc_orphan_chunks() reaps the superseded chunks -- the
** same clean path an edited file takes. No supersession bug is reachable
** here, unlike forum, where the key was the artifact and edits minted new
** artifacts.
**
** `AND coalesce(...) <> ''` is not cosmetic. index_text_blob() returns
** early on len == 0 WITHOUT calling mark_seen(), so an empty-comment
** check-in (fossil's own "initial empty check-in" has one, but any
** `commit -m ""` does too) would become a phantom sweep candidate on every
** single run.
**
** The BRANCH in the header is load-bearing, not decoration: it is what
** makes "which branch carries the windows build work" answerable, and it
** is why no separate tag: namespace is needed. Measured: the whole
** repository's tagxref free text is 120 bytes across 18 valued rows, and
** branch names are single words -- close to a worst case for both BM25 and
** a sentence embedder as their own sources. */
static int index_checkins(sqlite3 *db, viki_embedder *emb, const char *modelId,
                           int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT b.uuid || ' '"
        "    || length(cast(coalesce(e.ecomment, e.comment, '') AS BLOB)) || ' '"
        "    || 'check-in ' || substr(b.uuid, 1, 16)"
        "    || ' on ' || strftime('%Y-%m-%dT%H:%M:%S', e.mtime)"
        "    || ' by ' || " VIKI_ONELINE("coalesce(e.euser, e.user, '-')")
        "    || ' branch ' || " VIKI_ONELINE(
                 "coalesce((SELECT x.value FROM tagxref x JOIN tag t"
                 " ON t.tagid = x.tagid WHERE x.rid = e.objid"
                 " AND t.tagname = 'branch'), '-')")
        "    || char(10)"
        "    || coalesce(e.ecomment, e.comment, '')"
        "  FROM event e JOIN blob b ON b.rid = e.objid"
        " WHERE e.type = 'ci'"
        "   AND coalesce(e.ecomment, e.comment, '') <> ''"
        " ORDER BY e.mtime;"
        "SELECT '" VIKI_FRAME_EOF "';";
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        char *zText;
        size_t nText = 0;
        snprintf(virtualPath, sizeof(virtualPath), "ckin:%s", rec.zKey);
        zText = compose_headed(rec.zHdr, rec.zBody, rec.nBody, &nText);
        if( !zText ){ mark_seen(db, virtualPath); continue; }
        (*nItems)++;
        index_text_blob(db, virtualPath, zText, nText, 0, emb, modelId, nChunked);
        free(zText);
    }
    free(zOut);
    return it.seenEof;
}

/* `viki index`'s TECH NOTE extraction (event.type='e'). This is
** index_forum() with three substitutions -- type 'f'->'e', title card
** 'H'->'C', prefix forum:->note: -- plus a provenance header, because a
** tech note IS dated work by definition (that is what distinguishes it
** from a wiki page) and the date is the thing an episodic query asks for.
**
** `fossil wiki list` does not list tech notes and index_wiki() only walks
** that list, which is why they were invisible despite living one table
** away from wiki pages.
**
** KEYED ON THE TECHNOTE ID (`substr(tagname,7)`, the `event-<40hex>`
** suffix), not on the artifact uuid. The id is stable across edits, it is
** what Fossil's own /technote/<id> URL uses, and it turns an edit into the
** same re-hash-and-GC path a file takes. Keying on the artifact would work
** too -- the superseded artifact stops being listed and is swept -- but it
** loses the stable citation.
**
** NO fprev-style filter is needed here and adding one would be wrong:
** Fossil DELETEs the superseded `event` row (manifest.c:2543), so
** event.type='e' already holds exactly the current revision. Noted while
** reading: manifest.c:2531 carries Fossil's own `BUG: this check is only
** correct if subsequent version has already been crosslinked` comment
** above that logic, so an out-of-order sync could in principle leave a
** stale row -- matching Fossil's own view is still the right definition of
** "current", since diverging from its /technote/ page would be worse. */
static int index_technotes(sqlite3 *db, viki_embedder *emb, const char *modelId,
                            int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT substr(t.tagname, 7) || ' '"
        "    || length(cast(content(b.uuid) AS BLOB)) || ' '"
        "    || 'tech note ' || substr(substr(t.tagname, 7), 1, 16)"
        "    || ' on ' || strftime('%Y-%m-%dT%H:%M:%S', e.mtime)"
        "    || ' by ' || " VIKI_ONELINE("coalesce(e.euser, e.user, '-')")
        "    || char(10) || content(b.uuid)"
        "  FROM event e JOIN blob b ON b.rid = e.objid"
        "  JOIN tag t ON t.tagid = e.tagid"
        " WHERE e.type = 'e'"
        " ORDER BY e.mtime;"
        "SELECT '" VIKI_FRAME_EOF "';";
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        snprintf(virtualPath, sizeof(virtualPath), "note:%s", rec.zKey);
        index_w_card_artifact(db, virtualPath, rec.zHdr, 'C', rec.zBody,
                              emb, modelId, nItems, nChunked);
    }
    free(zOut);
    return it.seenEof;
}

/* `viki index`'s TICKET CHANGE HISTORY extraction.
**
** WHY IT IS NOT REDUNDANT WITH ticket:. `fossil ticket change UUID
** +comment TEXT` APPENDS to the ticket's own `comment` field, so
** index_tickets() already sees it. A change that REPLACES a field writes
** the new value into `ticket` and the per-change record into
** `ticketchng` -- and the PREVIOUS value then exists nowhere else. "What
** did this ticket say before it was closed" is exactly the episodic
** question about a ticket, and ticket: alone destroys the answer.
**
** THE TEXT IS NOT IN THE TABLE. `ticketchng.icomment` is NULL in every row
** of every repo measured -- the CLI `ticket add/change` path writes
** `ticket.comment`, not `icomment`. The change text lives only in the
** change ARTIFACT's `J` cards, so the artifact is what gets parsed:
**
**     D 2026-08-17T02:44:39.438
**     J comment REPLACED\sCOMMENT\sTEXT
**     J +comment APPENDED\sCOMMENT\sTEXT
**     J status Closed
**     K <ticket-uuid>
**     U auditor
**
** Split each `J ` line ONCE on the first space and unquote_fossil() the
** value -- the same `\s` escaping as the forum H card. A leading `+` on
** the field name means append; it is KEPT, because "was this added to or
** did it replace what was there" is itself the episodic signal.
**
** Line-based scanning is safe here specifically because a ticket-change
** artifact carries no counted-string card: every line is a single-line
** card, and escaped values cannot contain a raw newline. That is NOT true
** of forum/technote manifests, which is why those go through
** find_line_card() with an explicit W-body bound instead.
**
** No supersession filter, deliberately: these artifacts are immutable and
** every one of them is permanently current. The newest change's `J
** comment` does duplicate `ticket.comment`, so tchg: and ticket: carry
** overlapping text; measured impact was small and it is what makes the
** replaced-value questions answerable, so no de-duplication rule. */
static int index_ticket_changes(sqlite3 *db, viki_embedder *emb, const char *modelId,
                                 int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT b.uuid || ' '"
        "    || length(cast(content(b.uuid) AS BLOB)) || ' '"
        "    || 'ticket ' || substr(coalesce(k.tkt_uuid, '?'), 1, 16)"
        "    || ' changed ' || strftime('%Y-%m-%dT%H:%M:%S', c.tkt_mtime)"
        "    || ' by ' || " VIKI_ONELINE("coalesce(c.tkt_user, '-')")
        "    || char(10) || content(b.uuid)"
        "  FROM ticketchng c JOIN blob b ON b.rid = c.tkt_rid"
        "  LEFT JOIN ticket k ON k.tkt_id = c.tkt_id"
        " ORDER BY c.tkt_mtime;"
        "SELECT '" VIKI_FRAME_EOF "';";
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        char *zFields;
        size_t nFields = 0;
        char *p = rec.zBody;
        char *zText;
        size_t nText = 0;

        /* nBody + 2 is PROVABLY enough, which is why there is no fixed
        ** buffer here. A J card "J <name> <value>\n" consumes
        ** 3 + nName + nValEscaped + 1 bytes of the body and emits
        ** nName + 2 + nValUnescaped + 1, and unquote_fossil() only ever
        ** shrinks -- so it emits strictly less than it consumes. The one
        ** line that can break even is a VALUE-LESS J card, and the one that
        ** can exceed by a single byte is a value-less J card that is also
        ** the last line with no trailing newline. Hence +1 for that, +1 for
        ** the NUL. Non-J lines emit nothing.
        **
        ** The fixed 4 KB value buffer this replaces would have SILENTLY
        ** DROPPED any ticket comment longer than that -- and a long ticket
        ** comment is exactly the episodic content this class exists to
        ** keep, so the failure would have been invisible and total. */
        zFields = malloc(rec.nBody + 2);
        snprintf(virtualPath, sizeof(virtualPath), "tchg:%s", rec.zKey);
        if( !zFields ){ mark_seen(db, virtualPath); continue; }
        zFields[0] = '\0';

        while( *p ){
            char *nl = strchr(p, '\n');
            size_t lineLen = nl ? (size_t)(nl - p) : strlen(p);
            if( lineLen > 2 && p[0] == 'J' && p[1] == ' ' ){
                char *zName = p + 2;
                char *zVal;
                size_t nName;
                /* Split ONCE on the first space. The value keeps every
                ** later space -- it is escaped (`\s`), so an unsplit value
                ** cannot contain a raw one, but splitting again would still
                ** be wrong the day Fossil changes that. */
                char *sp = memchr(zName, ' ', lineLen - 2);
                if( sp ){
                    nName = (size_t)(sp - zName);
                    zVal = sp + 1;
                }else{
                    nName = lineLen - 2;
                    zVal = p + lineLen;   /* a value-less J card: empty value */
                }
                /* Terminate the value in place (we own this buffer) and
                ** unquote it in place too, so no copy and no size limit. */
                if( nl ) *nl = '\0';
                unquote_fossil(zVal);
                memcpy(zFields + nFields, zName, nName);
                nFields += nName;
                zFields[nFields++] = ':';
                zFields[nFields++] = ' ';
                {
                    size_t nVal = strlen(zVal);
                    memcpy(zFields + nFields, zVal, nVal);
                    nFields += nVal;
                }
                zFields[nFields++] = '\n';
                zFields[nFields] = '\0';
            }
            if( !nl ) break;
            p = nl + 1;
        }

        /* A change artifact with no J card at all carries no text worth
        ** indexing (a pure status/mimetype record). Skipping it without
        ** marking it seen is correct: there is nothing to withdraw. */
        if( nFields == 0 ){ free(zFields); continue; }
        zText = compose_headed(rec.zHdr, zFields, nFields, &nText);
        free(zFields);
        if( !zText ){ mark_seen(db, virtualPath); continue; }
        (*nItems)++;
        index_text_blob(db, virtualPath, zText, nText, 0, emb, modelId, nChunked);
        free(zText);
    }
    free(zOut);
    return it.seenEof;
}

/* `viki index`'s ATTACHMENT CONTENT extraction. `fossil wiki export`
** returns a page's body only, so an attachment's bytes are an artifact
** nothing in viki has ever read.
**
** Keyed on the CONTENT BLOB uuid, so a superseded version simply stops
** being listed and is swept. `target` may be a wiki page name, a technote
** id or a ticket uuid -- it goes in the header TEXT, never in the key, so
** there is no escaping problem to get wrong.
**
** `src <> ''` is the DELETION filter and it is READ FROM FOSSIL'S SOURCE,
** NOT MEASURED: attach.c:648 filters `WHERE isLatest AND src!=''`, and
** deletion sets src to the empty string rather than NULL. There is no
** `fossil attachment rm` subcommand (deletion is web-UI only), so this
** path could not be exercised here. Say it that way rather than implying
** it was tested.
**
** looks_binary() gates the payload, and THAT is why run_capture() had to
** grow an out-length: a planted 64-byte random attachment comes back
** through `fossil sql` as raw bytes, and strlen() on it would truncate at
** the first NUL -- hiding from looks_binary() exactly the bytes that prove
** it binary. */
static int index_attachments(sqlite3 *db, viki_embedder *emb, const char *modelId,
                              int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT a.src || ' '"
        "    || length(cast(content(a.src) AS BLOB)) || ' '"
        "    || 'attachment ' || " VIKI_ONELINE("coalesce(a.filename, '?')")
        "    || ' on ' || " VIKI_ONELINE("coalesce(a.target, '?')")
        "    || ' at ' || strftime('%Y-%m-%dT%H:%M:%S', a.mtime)"
        "    || char(10) || content(a.src)"
        "  FROM attachment a JOIN blob b ON b.uuid = a.src"
        " WHERE a.isLatest AND a.src IS NOT NULL AND a.src <> ''"
        " ORDER BY a.mtime;"
        "SELECT '" VIKI_FRAME_EOF "';";
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        char *zText;
        size_t nText = 0;
        snprintf(virtualPath, sizeof(virtualPath), "attach:%s", rec.zKey);
        /* Deliberately NOT marked seen when binary, exactly as index_file()
        ** treats a file that has become binary: there is no indexable text
        ** any more, and continuing to serve what it used to hold is the
        ** same defect as serving a deleted file's. */
        if( looks_binary(rec.zBody, rec.nBody) ) continue;
        zText = compose_headed(rec.zHdr, rec.zBody, rec.nBody, &nText);
        if( !zText ){ mark_seen(db, virtualPath); continue; }
        (*nItems)++;
        index_text_blob(db, virtualPath, zText, nText, 0, emb, modelId, nChunked);
        free(zText);
    }
    free(zOut);
    return it.seenEof;
}

/* Names viki publishes into `fossil uv` itself (viki_cache.c). Indexing
** them would be indexing viki's own derived output as if it were content:
** viki-model/vocab.txt alone is 231,508 bytes of one WordPiece token per
** line and would chunk into ~760 chunks of pure token noise, and
** viki-cache.db is the cache this run is writing. */
static int is_viki_own_uv(const char *zName){
    return strcmp(zName, "viki-cache.db") == 0
        || strncmp(zName, "viki-model/", 11) == 0;
}

/* `viki index`'s UNVERSIONED CONTENT extraction. Two calls, and it MUST be
** two: `unversioned.content` is zlib-compressed with a 4-byte big-endian
** length prefix whenever `encoding=1` (measured: sz=734,
** length(content)=467, hex begins 000002DE789C...), so reading it through
** `fossil sql` returns compressed bytes. `fossil unversioned cat` is the
** only extraction path that does not require linking zlib.
**
** The listing puts the NAME IN THE PAYLOAD rather than in the key, because
** a uv name is the one key in this file that is not a hex uuid and may
** legitimately contain a space. Everything else uses the key slot.
**
** `unversioned` is one of only two tables that do not exist in a virgin
** repository (`forumpost` is the other), so the sentinel is load-bearing
** for CORRECTNESS here, not merely hygiene: without it a repo that has
** never used uv looks identical to one whose uv blobs were all removed. */
static int index_unversioned(sqlite3 *db, viki_embedder *emb, const char *modelId,
                              int *nItems, int *nChunked){
    static const char *zSql =
        "SELECT 'uv ' || length(cast(name AS BLOB))"
        "    || char(10) || name"
        "  FROM unversioned WHERE sz > 0 ORDER BY name;"
        "SELECT '" VIKI_FRAME_EOF "';";
    const char *zFossil = viki_fossil_binary();
    char *zOut;
    size_t nOut = 0;
    FramedIter it;
    FramedRec rec;
    int authoritative;

    zOut = fossil_sql_framed(zSql, &nOut);
    if( !zOut ) return 0;

    framed_init(&it, zOut, nOut);
    while( framed_next(&it, &rec) ){
        char virtualPath[512];
        char zHeader[512];
        char *argvCat[] = { (char*)zFossil, "unversioned", "cat", NULL, NULL };
        char *zContent;
        size_t nContent = 0;
        int rcCat = -1;
        char *zText;
        size_t nText = 0;

        if( is_viki_own_uv(rec.zBody) ) continue;

        argvCat[3] = rec.zBody;
        zContent = run_capture(argvCat, &rcCat, &nContent);
        if( !zContent ) continue;
        snprintf(virtualPath, sizeof(virtualPath), "uv:%s", rec.zBody);
        /* The header is built here rather than in SQL because this class's
        ** name travels in the PAYLOAD, not the key -- it is the one key in
        ** this file that is not a hex uuid and may contain a space. */
        snprintf(zHeader, sizeof(zHeader), "unversioned file %s", rec.zBody);
        if( rcCat != 0 ){
            /* Could not re-read a blob the listing says exists. A failure
            ** to observe, not a withdrawal -- keep whatever we have. */
            mark_seen(db, virtualPath);
            free(zContent);
            continue;
        }
        if( looks_binary(zContent, nContent) ){ free(zContent); continue; }
        zText = compose_headed(zHeader, zContent, nContent, &nText);
        free(zContent);
        if( !zText ){ mark_seen(db, virtualPath); continue; }
        (*nItems)++;
        index_text_blob(db, virtualPath, zText, nText, 0, emb, modelId, nChunked);
        free(zText);
    }
    authoritative = it.seenEof;
    free(zOut);
    return authoritative;
}

/* Returns 1 if the whole subtree was enumerated, 0 if ANY directory in it
** could not be opened. The caller uses that as its authority to delete
** rows under zDir: a directory that would not open looks exactly like a
** directory whose files were all deleted, and only one of those may be
** acted on. FINDINGS.md already noted that `viki index <nonexistent-dir>`
** exits 0 with a silent opendir() failure -- silence is tolerable for a
** typo that indexes nothing, and unacceptable for one that would DELETE
** everything under the name that was typed. */
static int walk(sqlite3 *db, const char *dir, int *nFiles, int *nChunked,
                  viki_embedder *emb, const char *modelId){
    DIR *d = opendir(dir);
    struct dirent *ent;
    int complete = 1;
    if( !d ) return 0;

    while( (ent = readdir(d)) != NULL ){
        char path[4096];
        struct stat st;

        if( strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 ) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if( lstat(path, &st) != 0 ) continue;

        if( S_ISDIR(st.st_mode) ){
            if( should_skip_dir(ent->d_name) ) continue;
            if( !walk(db, path, nFiles, nChunked, emb, modelId) ) complete = 0;
        }else if( S_ISREG(st.st_mode) ){
            index_file(db, path, nFiles, nChunked, emb, modelId);
        }
        /* symlinks intentionally not followed */
    }
    closedir(d);
    return complete;
}

/* Deletes the viki_source rows this run is BOTH authoritative over and did
** not see -- superseded or withdrawn sources. Returns how many it dropped.
**
** The scoping rule, which is the whole difficulty here. viki_source is one
** flat table holding several independently-maintained namespaces, and a
** given `viki index` run observes only some of them:
**
**   1. Filesystem paths are swept only when path_in_dir(path, zDir) AND
**      the walk of zDir completed. `viki index docs` therefore invalidates
**      inside docs/ and cannot touch anything above or beside it -- a
**      naive "delete everything not seen this run" would wipe the rest of
**      the cache on every subdirectory index.
**   2. Every VIRTUAL namespace is swept only when its own extractor
**      reports success. Each extractor produces empty output in two
**      totally different situations -- "this repo has none" and "there is
**      no fossil binary / this is not a checkout / that table does not
**      exist in this repo" -- and only the extractor's own authority flag
**      separates them. Without that check, running `viki index` on a
**      machine with no fossil would silently delete every wiki page,
**      ticket, forum post and check-in comment from the cache.
**   3. A row in a namespace nobody claimed this run is never touched --
**      including a namespace VIRTUAL_NS has never heard of, which is what
**      looks_like_namespace() is for.
**
** Every branch resolves ambiguity toward keeping. Missed deletions are
** recoverable (D-10: the store is derived, rebuild it); wrong deletions
** are not. */
static int sweep_sources(sqlite3 *db, const char *zDir, const VikiAuth *pAuth){
    sqlite3_stmt *stSel, *stDel;
    char **azDrop = NULL;
    int nDrop = 0, nAlloc = 0, i, nDeleted = 0;

    if( sqlite3_prepare_v2(db,
            "SELECT path FROM viki_source s "
            "WHERE NOT EXISTS(SELECT 1 FROM temp.viki_seen k WHERE k.path=s.path)",
            -1, &stSel, NULL) != SQLITE_OK ){
        return 0;
    }
    /* Collected first, deleted after the scan finishes: mutating a table
    ** while stepping a SELECT over it leaves which rows the scan still
    ** visits up to the query plan. */
    while( sqlite3_step(stSel) == SQLITE_ROW ){
        const char *zPath = (const char*)sqlite3_column_text(stSel, 0);
        int drop = 0;
        int matched = 0;
        int iNs;
        if( !zPath ) continue;
        for( iNs = 0; VIRTUAL_NS[iNs].zPrefix; iNs++ ){
            if( strncmp(zPath, VIRTUAL_NS[iNs].zPrefix, VIRTUAL_NS[iNs].nPrefix) == 0 ){
                drop = *(const int*)((const char*)pAuth + VIRTUAL_NS[iNs].iAuth);
                matched = 1;
                break;
            }
        }
        if( !matched ){
            /* Not a registered namespace. Either a filesystem path, or a
            ** namespace some extractor minted without registering it above
            ** -- and the second must NOT fall through to the filesystem
            ** rule, because path_in_dir("newns:key", ".") is true and would
            ** delete it. */
            if( looks_like_namespace(zPath) ) drop = 0;
            else drop = pAuth->fs && path_in_dir(zPath, zDir);
        }
        if( !drop ) continue;
        if( nDrop == nAlloc ){
            int nNew = nAlloc ? nAlloc * 2 : 16;
            char **azNew = realloc(azDrop, sizeof(char*) * (size_t)nNew);
            if( !azNew ) break;
            azDrop = azNew;
            nAlloc = nNew;
        }
        azDrop[nDrop] = malloc(strlen(zPath) + 1);
        if( !azDrop[nDrop] ) break;
        strcpy(azDrop[nDrop], zPath);
        nDrop++;
    }
    sqlite3_finalize(stSel);

    if( nDrop > 0
     && sqlite3_prepare_v2(db, "DELETE FROM viki_source WHERE path=?1", -1, &stDel, NULL) == SQLITE_OK ){
        for( i = 0; i < nDrop; i++ ){
            sqlite3_bind_text(stDel, 1, azDrop[i], -1, SQLITE_STATIC);
            if( sqlite3_step(stDel) == SQLITE_DONE ) nDeleted += sqlite3_changes(db);
            sqlite3_reset(stDel);
        }
        sqlite3_finalize(stDel);
    }
    for( i = 0; i < nDrop; i++ ) free(azDrop[i]);
    free(azDrop);
    return nDeleted;
}

/* Deletes every chunk whose content_hash no longer has ANY live source
** referencing it, from viki_chunk and its FTS5 mirror alike. Returns the
** number of viki_chunk rows removed.
**
** Reachability, not identity, is the test, and it has to be, because
** content is content-addressed and therefore SHARED: two paths with
** identical bytes collapse to one content_hash, so deleting a chunk the
** moment one of its paths goes away would silently blank the other. The
** LEFT-over reference count is what decides.
**
** Deliberately NOT filtered by model_id. `viki_chunk` is keyed
** (content_hash, model_id, chunk_ix) so two epochs can coexist
** (VIKI_DESIGN.md), and it is tempting to spare the other epoch's rows.
** That would be wrong twice over: an unreferenced content_hash is
** unreachable under EVERY model_id at once (no live path names those
** bytes, whatever they were embedded with), and `viki ask`'s BM25 leg does
** not filter on model_id at all -- chunk_fts is queried by MATCH alone --
** so leaving another epoch's row behind would leave the withdrawn text
** retrievable, which is the entire defect being fixed. A peer that still
** needs those vectors still has the content, and D-11 recomputes them
** deterministically from (content_hash, model_id).
**
** chunk_fts is a plain FTS5 table (not external-content), so it holds its
** own copy of every row and must be deleted from explicitly -- nothing
** cascades. Getting this half wrong is invisible in viki_chunk yet leaves
** the text fully searchable, which is the more dangerous half. */
static int gc_orphan_chunks(sqlite3 *db){
    static const char *zLive =
        " WHERE content_hash NOT IN (SELECT content_hash FROM viki_source)";
    char zSql[256];
    int nChunks = 0;

    snprintf(zSql, sizeof(zSql), "DELETE FROM viki_chunk%s", zLive);
    if( sqlite3_exec(db, zSql, NULL, NULL, NULL) == SQLITE_OK ) nChunks = sqlite3_changes(db);

    snprintf(zSql, sizeof(zSql), "DELETE FROM chunk_fts%s", zLive);
    sqlite3_exec(db, zSql, NULL, NULL, NULL);

    return nChunks;
}

int viki_cmd_index(sqlite3 *db, const char *zDir, viki_embedder *emb){
    int nFiles = 0, nChunked = 0;
    int nWiki = 0, nWikiChunked = 0;
    int nTickets = 0, nTicketChunked = 0;
    int nForum = 0, nForumChunked = 0;
    int nCkin = 0, nCkinChunked = 0;
    int nNote = 0, nNoteChunked = 0;
    int nTchg = 0, nTchgChunked = 0;
    int nAttach = 0, nAttachChunked = 0;
    int nUv = 0, nUvChunked = 0;
    VikiAuth auth;
    int nDropped, nOrphans;
    char *errmsg = NULL;
    const char *modelId = emb ? viki_embedder_model_id(emb) : VIKI_MODEL_NONE;

    memset(&auth, 0, sizeof(auth));

    if( sqlite3_exec(db, "BEGIN", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: BEGIN failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    /* Per-run scratch: which sources were observed live. Dropped and
    ** recreated rather than assumed absent, so a second viki_cmd_index()
    ** call on one connection cannot inherit the first run's marks. */
    if( sqlite3_exec(db,
            "CREATE TEMP TABLE IF NOT EXISTS viki_seen(path TEXT PRIMARY KEY);"
            "DELETE FROM temp.viki_seen;", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: temp table init failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return 1;
    }

    auth.fs = walk(db, zDir, &nFiles, &nChunked, emb, modelId);
    /* None of the rest are files in the checkout (ARCHITECTURE.md);
    ** extracted via `fossil wiki`/`fossil ticket`/`fossil sql` subprocess
    ** calls instead of walking the filesystem. All assume cwd is inside an
    ** open fossil-see checkout, the same assumption `viki cache push/pull`
    ** already make. Each returns whether it actually succeeded -- that, not
    ** the item count, is what licenses invalidating its namespace below.
    **
    ** Ordered by measured episodic value, not alphabetically: check-in
    ** comments carry 9 of the 16 answers the coverage measurement said
    ** were unreachable. */
    auth.wiki   = index_wiki(db, emb, modelId, &nWiki, &nWikiChunked);
    auth.ticket = index_tickets(db, emb, modelId, &nTickets, &nTicketChunked);
    auth.forum  = index_forum(db, emb, modelId, &nForum, &nForumChunked);
    auth.ckin   = index_checkins(db, emb, modelId, &nCkin, &nCkinChunked);
    auth.note   = index_technotes(db, emb, modelId, &nNote, &nNoteChunked);
    auth.tchg   = index_ticket_changes(db, emb, modelId, &nTchg, &nTchgChunked);
    auth.attach = index_attachments(db, emb, modelId, &nAttach, &nAttachChunked);
    auth.uv     = index_unversioned(db, emb, modelId, &nUv, &nUvChunked);

    /* Invalidation, in this order: retire the sources that are gone, THEN
    ** collect the chunks nothing points at any more. Running the GC second
    ** is what makes shared content safe -- a hash reachable from some other
    ** surviving path is still reachable when the GC looks. */
    nDropped = sweep_sources(db, zDir, &auth);
    nOrphans = gc_orphan_chunks(db);

    if( sqlite3_exec(db, "COMMIT", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: COMMIT failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    /* Three lines, and the split is not cosmetic: test/m1.sh anchors
    ** `^viki index: N stale source(s) retired, M orphan chunk(s) removed$`
    ** at BOTH ends, so the new counts get their own line rather than
    ** extending that one. */
    fprintf(stderr,
        "viki index: %d file(s) scanned, %d (re)chunked; %d wiki page(s), %d (re)chunked; "
        "%d ticket(s), %d (re)chunked; %d forum post(s), %d (re)chunked (model_id=%s)\n",
        nFiles, nChunked, nWiki, nWikiChunked, nTickets, nTicketChunked,
        nForum, nForumChunked, modelId);
    fprintf(stderr,
        "viki index: %d check-in comment(s), %d (re)chunked; %d tech note(s), %d (re)chunked; "
        "%d ticket change(s), %d (re)chunked; %d attachment(s), %d (re)chunked; "
        "%d unversioned file(s), %d (re)chunked\n",
        nCkin, nCkinChunked, nNote, nNoteChunked, nTchg, nTchgChunked,
        nAttach, nAttachChunked, nUv, nUvChunked);
    fprintf(stderr, "viki index: %d stale source(s) retired, %d orphan chunk(s) removed\n",
            nDropped, nOrphans);
    /* Say out loud which namespaces were left untouched. A silent "0
    ** retired" is ambiguous between "nothing was stale" and "this run had
    ** no way to tell", and the difference is exactly what decides whether
    ** withdrawn content is still being served.
    **
    ** Expect to see this on a repo that has never used a feature: the
    ** `unversioned` and `forumpost` tables do not exist until first use, so
    ** their extractors correctly report no authority rather than claiming
    ** the namespace is empty. That is the fix, not a warning. */
    if( !auth.fs || !auth.wiki || !auth.ticket || !auth.forum || !auth.ckin
     || !auth.note || !auth.tchg || !auth.attach || !auth.uv ){
        fprintf(stderr, "viki index: not authoritative this run for%s%s%s%s%s%s%s%s%s"
                " -- existing entries there left in place\n",
                auth.fs     ? "" : " files(dir unreadable)",
                auth.wiki   ? "" : " wiki:",
                auth.ticket ? "" : " ticket:",
                auth.forum  ? "" : " forum:",
                auth.ckin   ? "" : " ckin:",
                auth.note   ? "" : " note:",
                auth.tchg   ? "" : " tchg:",
                auth.attach ? "" : " attach:",
                auth.uv     ? "" : " uv:");
    }
        {
        /* Capture projection, after the chunk work: viki_note is rebuilt from
        ** captures/*.md wholesale, so it can never carry a stale row and needs
        ** none of the sweep-scoping care above. */
        int nNote = viki_note_reindex(db, zDir);
        if( nNote >= 0 )
            fprintf(stderr, "viki index: %d captured note(s) projected\n", nNote);
    }
return 0;
}
