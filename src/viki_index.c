#include "viki_index.h"
#include "viki_cache.h" /* viki_fossil_binary/viki_fossil_user, shared subprocess config */
#include "sha256.h"

#include <dirent.h>
#include <fcntl.h>
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

/* The `path` column of viki_source holds two disjoint kinds of key: real
** filesystem paths produced by walk(), and VIRTUAL paths minted by the
** three Fossil extractors. These prefixes are what tells them apart, and
** they are load-bearing for invalidation (see sweep_sources): each names a
** namespace with a different authority rule, so a run that walked a
** directory must not conclude anything about wiki/ticket/forum rows, and
** vice versa. Kept adjacent to the snprintf()s that mint them
** (index_wiki/index_tickets/index_forum) -- if a fourth content type is
** ever added, its prefix belongs here too or its rows become
** un-invalidatable. */
static const char *VIRTUAL_PREFIX[] = { "wiki:", "ticket:", "forum:", NULL };

static int is_virtual_path(const char *zPath){
    int i;
    for( i = 0; VIRTUAL_PREFIX[i]; i++ ){
        if( strncmp(zPath, VIRTUAL_PREFIX[i], strlen(VIRTUAL_PREFIX[i])) == 0 ) return 1;
    }
    return 0;
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
** indistinguishable, so no caller could have swept safely. */
static char *run_capture(char *const argv[], int *pExit){
    int pipefd[2];
    pid_t pid;
    char *buf;
    size_t cap = 65536, len = 0;

    if( pExit ) *pExit = -1;
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
    for(;;){
        ssize_t n;
        if( len + 4096 > cap ){ cap *= 2; buf = realloc(buf, cap); }
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
    return buf;
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

    listOut = run_capture(argvList, &rcList);
    if( !listOut ) return 0;
    if( rcList != 0 ){ free(listOut); return 0; }

    line = strtok_r(listOut, "\n", &saveptr);
    while( line ){
        while( *line == ' ' || *line == '\t' ) line++;
        if( line[0] ){
            char *argvExport[] = { (char*)fossil, "wiki", "export", line, "-", NULL };
            int rcExport = -1;
            char *content = run_capture(argvExport, &rcExport);
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

/* `viki index`'s forum extraction: two-step, like wiki -- first list every
** CURRENT forum-post artifact's blob hash (event.type='f'; verified this
** is the correct type code directly from `fossil help timeline`'s --type
** list), then fetch each one's raw manifest content individually via a
** SEPARATE `fossil sql` call. Deliberately not a single query selecting
** all posts' content at once: `fossil sql`'s default output is
** pipe-separated with RAW embedded newlines in multi-line fields, which
** is ambiguous to parse back into distinct rows (same class of trap as
** the ticket TSV bug -- see FINDINGS.md). One artifact per call sidesteps
** that: the entire captured stdout of a single-column, single-row query
** IS the content, unambiguously.
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
** Note the one wrinkle that clause buys: a repository that has never held
** a forum post has no `forumpost` table at all (it is created on the
** first crosslink), so this query fails with "no such table: forumpost".
** That is harmless today -- run_capture() discards stderr and the empty
** result is the correct answer for such a repo -- but it is luck, not
** design. Any repo with at least one forum post has the table. If
** run_capture() ever stops swallowing stderr, this will start printing a
** spurious error on every forum-less repo.
**
** That "harmless by luck" now pays for itself: the listing's exit status
** is what this function returns as its authority to invalidate. A repo
** with no forumpost table makes the query FAIL, which reads as "not
** authoritative", which leaves any existing forum: rows alone -- exactly
** the conservative answer. Contrast a repo that really has a forum: the
** query succeeds, and a post that no longer appears in it (because an
** edit superseded it, so it is now some other row's fprev) is correctly
** dropped from the cache instead of staying retrievable forever.
**
** Returns 1 only if the listing succeeded AND every listed artifact was
** fetched successfully. */
static int index_forum(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    char *argvList[] = { (char*)fossil, "sql", "--readonly",
        "SELECT blob.uuid FROM event JOIN blob ON blob.rid=event.objid"
        " WHERE event.type='f'"
        "   AND blob.rid NOT IN (SELECT fprev FROM forumpost WHERE fprev IS NOT NULL);", NULL };
    char *listOut;
    char *line, *saveptr;
    int rcList = -1;
    int authoritative = 1;

    listOut = run_capture(argvList, &rcList);
    if( !listOut ) return 0;
    if( rcList != 0 ){ free(listOut); return 0; }

    line = strtok_r(listOut, "\n", &saveptr);
    while( line ){
        while( *line == ' ' || *line == '\t' ) line++;
        if( line[0] ){
            char query[256];
            char *manifest;
            int rcContent = -1;
            char *argvContent[] = { (char*)fossil, "sql", "--readonly", query, NULL };

            snprintf(query, sizeof(query), "SELECT content('%s');", line);
            manifest = run_capture(argvContent, &rcContent);
            if( rcContent != 0 ) authoritative = 0;

            if( manifest ){
                size_t bodyLen = 0;
                const char *body = find_w_card(manifest, &bodyLen);
                /* Bound the W count against the bytes that actually follow
                ** the W card, not against the whole manifest's length --
                ** the latter includes the ~150 bytes of cards BEFORE the
                ** body, so a manifest whose W count overruns its own
                ** payload would still pass that test and be read past its
                ** end. (Hardening: found by reading, never triggered by a
                ** real artifact, since Fossil validates a manifest through
                ** manifest_parse() before storing it.) */
                if( body && bodyLen > 0 && bodyLen <= strlen(body) ){
                    char *title = find_line_card(manifest, body, 'H');
                    char virtualPath[512];
                    snprintf(virtualPath, sizeof(virtualPath), "forum:%s", line);
                    (*nItems)++;
                    if( title ){
                        size_t combinedLen;
                        char *combined;
                        int off;

                        /* Single-line manifest cards are escaped (a space
                        ** in a thread title is stored as `\s`), so the H
                        ** card MUST be run through unquote_fossil() -- the
                        ** same mapping as Fossil's own defossilize(),
                        ** which Fossil applies to this exact field at
                        ** manifest.c's `defossilize(p->zThreadTitle)`.
                        ** Skipping it does not merely look ugly: FTS5 then
                        ** tokenizes `\sseal` as one word, so every title
                        ** word after the first becomes unsearchable. See
                        ** FINDINGS.md. The W-card body needs no such
                        ** decoding -- it is a counted string, not escaped. */
                        unquote_fossil(title);

                        /* Index "title\n\nbody" so the title contributes to
                        ** both BM25 and the embedding, not just the body. */
                        combinedLen = strlen(title) + 2 + bodyLen;
                        combined = malloc(combinedLen + 1);
                        off = snprintf(combined, combinedLen + 1, "%s\n\n", title);
                        memcpy(combined + off, body, bodyLen);
                        combined[off + (int)bodyLen] = '\0';
                        index_text_blob(db, virtualPath, combined, strlen(combined), 0, emb, modelId, nChunked);
                        free(combined);
                        free(title);
                    }else{
                        index_text_blob(db, virtualPath, body, bodyLen, 0, emb, modelId, nChunked);
                    }
                }else{
                    /* Listed as current, but its manifest has no usable W
                    ** card. A forum: path is keyed by ARTIFACT uuid, and an
                    ** artifact is immutable -- so this is a failure to
                    ** re-read unchanging content, never a withdrawal of it.
                    ** Mark it seen so the sweep keeps whatever we parsed on
                    ** an earlier, successful run. */
                    char virtualPath[512];
                    snprintf(virtualPath, sizeof(virtualPath), "forum:%s", line);
                    mark_seen(db, virtualPath);
                }
                free(manifest);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(listOut);
    return authoritative;
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

    out = run_capture(argv, &rcShow);
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
**   2. wiki:/ticket:/forum: rows are swept only when their own extractor
**      reports success. Each extractor produces empty output in two
**      totally different situations -- "this repo has none" and "there is
**      no fossil binary / this is not a checkout" -- and only the exit
**      status separates them. Without that check, running `viki index` on
**      a machine with no fossil would silently delete every wiki page,
**      ticket and forum post from the cache.
**   3. A row in a namespace nobody claimed this run is never touched.
**
** Every branch resolves ambiguity toward keeping. Missed deletions are
** recoverable (D-10: the store is derived, rebuild it); wrong deletions
** are not. */
static int sweep_sources(sqlite3 *db, const char *zDir, int fsAuth,
                          int wikiAuth, int ticketAuth, int forumAuth){
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
        int drop;
        if( !zPath ) continue;
        if( strncmp(zPath, "wiki:", 5) == 0 )        drop = wikiAuth;
        else if( strncmp(zPath, "ticket:", 7) == 0 ) drop = ticketAuth;
        else if( strncmp(zPath, "forum:", 6) == 0 )  drop = forumAuth;
        else if( is_virtual_path(zPath) )            drop = 0; /* namespace added without a rule here */
        else                                         drop = fsAuth && path_in_dir(zPath, zDir);
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
    int fsAuth, wikiAuth, ticketAuth, forumAuth;
    int nDropped, nOrphans;
    char *errmsg = NULL;
    const char *modelId = emb ? viki_embedder_model_id(emb) : VIKI_MODEL_NONE;

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

    fsAuth = walk(db, zDir, &nFiles, &nChunked, emb, modelId);
    /* Wiki pages and tickets aren't files in the checkout (ARCHITECTURE.md);
    ** extracted via `fossil wiki`/`fossil ticket` subprocess calls instead
    ** of walking the filesystem. Both assume cwd is inside an open
    ** fossil-see checkout, same assumption `viki cache push/pull` already
    ** make. Each returns whether it actually succeeded -- that, not the
    ** item count, is what licenses invalidating its namespace below. */
    wikiAuth = index_wiki(db, emb, modelId, &nWiki, &nWikiChunked);
    ticketAuth = index_tickets(db, emb, modelId, &nTickets, &nTicketChunked);
    forumAuth = index_forum(db, emb, modelId, &nForum, &nForumChunked);

    /* Invalidation, in this order: retire the sources that are gone, THEN
    ** collect the chunks nothing points at any more. Running the GC second
    ** is what makes shared content safe -- a hash reachable from some other
    ** surviving path is still reachable when the GC looks. */
    nDropped = sweep_sources(db, zDir, fsAuth, wikiAuth, ticketAuth, forumAuth);
    nOrphans = gc_orphan_chunks(db);

    if( sqlite3_exec(db, "COMMIT", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: COMMIT failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    fprintf(stderr,
        "viki index: %d file(s) scanned, %d (re)chunked; %d wiki page(s), %d (re)chunked; "
        "%d ticket(s), %d (re)chunked; %d forum post(s), %d (re)chunked (model_id=%s)\n",
        nFiles, nChunked, nWiki, nWikiChunked, nTickets, nTicketChunked,
        nForum, nForumChunked, modelId);
    fprintf(stderr, "viki index: %d stale source(s) retired, %d orphan chunk(s) removed\n",
            nDropped, nOrphans);
    /* Say out loud which namespaces were left untouched. A silent "0
    ** retired" is ambiguous between "nothing was stale" and "this run had
    ** no way to tell", and the difference is exactly what decides whether
    ** withdrawn content is still being served. */
    if( !fsAuth || !wikiAuth || !ticketAuth || !forumAuth ){
        fprintf(stderr, "viki index: not authoritative this run for%s%s%s%s"
                " -- existing entries there left in place\n",
                fsAuth ? "" : " files(dir unreadable)",
                wikiAuth ? "" : " wiki:",
                ticketAuth ? "" : " ticket:",
                forumAuth ? "" : " forum:");
    }
    return 0;
}
