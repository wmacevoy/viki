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

    if( len == 0 ) return;

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
    if( !data ) return 0;
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
** result either way. */
static char *run_capture(char *const argv[]){
    int pipefd[2];
    pid_t pid;
    char *buf;
    size_t cap = 65536, len = 0;

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
        waitpid(pid, &status, 0);
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
** redundant chunking for unchanged pages. */
static void index_wiki(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    char *argvList[] = { (char*)fossil, "wiki", "list", NULL };
    char *listOut;
    char *line, *saveptr;

    listOut = run_capture(argvList);
    if( !listOut ) return;

    line = strtok_r(listOut, "\n", &saveptr);
    while( line ){
        while( *line == ' ' || *line == '\t' ) line++;
        if( line[0] ){
            char *argvExport[] = { (char*)fossil, "wiki", "export", line, "-", NULL };
            char *content = run_capture(argvExport);
            if( content ){
                char virtualPath[512];
                snprintf(virtualPath, sizeof(virtualPath), "wiki:%s", line);
                (*nItems)++;
                index_text_blob(db, virtualPath, content, strlen(content), 0, emb, modelId, nChunked);
                free(content);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(listOut);
}

/* Finds a "W <n>\n" card in a raw Fossil manifest and returns a pointer
** to the n bytes that follow (the counted-string payload -- this is how
** Fossil manifests embed arbitrary multi-line text without needing to
** escape it, unlike single-line cards). *outLen receives n. Returns NULL
** if no well-formed W card is found. Verified against a REAL manifest
** (a wiki artifact's raw content via `fossil sql`) -- see FINDINGS.md;
** forum posts use the same manifest format/card vocabulary, but this
** exact path (a forum post specifically) could not be tested end-to-end,
** since creating one turned out to require Fossil's AJAX-driven web UI
** rather than a scriptable CLI/HTTP form post. Fails soft (returns NULL,
** caller skips the post) rather than guessing on a malformed card. */
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
** virtual path, just without a human-friendly title prefix. */
static char *find_line_card(const char *manifest, char cardLetter){
    const char *p = manifest;
    while( *p ){
        if( (p == manifest || p[-1] == '\n') && p[0] == cardLetter && p[1] == ' ' ){
            const char *start = p + 2;
            const char *nl = strchr(start, '\n');
            size_t len = nl ? (size_t)(nl - start) : strlen(start);
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
** forum-post artifact's blob hash (event.type='f'; verified this is the
** correct type code directly from `fossil help timeline`'s --type list),
** then fetch each one's raw manifest content individually via a SEPARATE
** `fossil sql` call. Deliberately not a single query selecting all posts'
** content at once: `fossil sql`'s default output is pipe-separated with
** RAW embedded newlines in multi-line fields, which is ambiguous to parse
** back into distinct rows (same class of trap as the ticket TSV bug --
** see FINDINGS.md). One artifact per call sidesteps that: the entire
** captured stdout of a single-column, single-row query IS the content,
** unambiguously. */
static void index_forum(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    char *argvList[] = { (char*)fossil, "sql", "--readonly",
        "SELECT blob.uuid FROM event JOIN blob ON blob.rid=event.objid WHERE event.type='f';", NULL };
    char *listOut;
    char *line, *saveptr;

    listOut = run_capture(argvList);
    if( !listOut ) return;

    line = strtok_r(listOut, "\n", &saveptr);
    while( line ){
        while( *line == ' ' || *line == '\t' ) line++;
        if( line[0] ){
            char query[256];
            char *manifest;
            char *argvContent[] = { (char*)fossil, "sql", "--readonly", query, NULL };

            snprintf(query, sizeof(query), "SELECT content('%s');", line);
            manifest = run_capture(argvContent);

            if( manifest ){
                size_t bodyLen = 0;
                const char *body = find_w_card(manifest, &bodyLen);
                if( body && bodyLen > 0 && bodyLen <= strlen(manifest) ){
                    char *title = find_line_card(manifest, 'H');
                    char virtualPath[512];
                    snprintf(virtualPath, sizeof(virtualPath), "forum:%s", line);
                    (*nItems)++;
                    if( title ){
                        /* Index "title\n\nbody" so the title contributes to
                        ** both BM25 and the embedding, not just the body. */
                        size_t combinedLen = strlen(title) + 2 + bodyLen;
                        char *combined = malloc(combinedLen + 1);
                        int off = snprintf(combined, combinedLen + 1, "%s\n\n", title);
                        memcpy(combined + off, body, bodyLen);
                        combined[off + (int)bodyLen] = '\0';
                        index_text_blob(db, virtualPath, combined, strlen(combined), 0, emb, modelId, nChunked);
                        free(combined);
                        free(title);
                    }else{
                        index_text_blob(db, virtualPath, body, bodyLen, 0, emb, modelId, nChunked);
                    }
                }
                free(manifest);
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(listOut);
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

static void index_tickets(sqlite3 *db, viki_embedder *emb, const char *modelId, int *nItems, int *nChunked){
    const char *fossil = viki_fossil_binary();
    const char *user = viki_fossil_user();
    char *argv[] = { (char*)fossil, "ticket", "show", "0", "--quote", "--user", (char*)user, NULL };
    char *out;
    char *lineStart = NULL, *p;
    int uuidCol = -1, titleCol = -1, commentCol = -1, statusCol = -1;
    int isHeader = 1;

    out = run_capture(argv);
    if( !out ) return;

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
}

static void walk(sqlite3 *db, const char *dir, int *nFiles, int *nChunked,
                  viki_embedder *emb, const char *modelId){
    DIR *d = opendir(dir);
    struct dirent *ent;
    if( !d ) return;

    while( (ent = readdir(d)) != NULL ){
        char path[4096];
        struct stat st;

        if( strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0 ) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        if( lstat(path, &st) != 0 ) continue;

        if( S_ISDIR(st.st_mode) ){
            if( should_skip_dir(ent->d_name) ) continue;
            walk(db, path, nFiles, nChunked, emb, modelId);
        }else if( S_ISREG(st.st_mode) ){
            index_file(db, path, nFiles, nChunked, emb, modelId);
        }
        /* symlinks intentionally not followed */
    }
    closedir(d);
}

int viki_cmd_index(sqlite3 *db, const char *zDir, viki_embedder *emb){
    int nFiles = 0, nChunked = 0;
    int nWiki = 0, nWikiChunked = 0;
    int nTickets = 0, nTicketChunked = 0;
    int nForum = 0, nForumChunked = 0;
    char *errmsg = NULL;
    const char *modelId = emb ? viki_embedder_model_id(emb) : VIKI_MODEL_NONE;

    if( sqlite3_exec(db, "BEGIN", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: BEGIN failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    walk(db, zDir, &nFiles, &nChunked, emb, modelId);
    /* Wiki pages and tickets aren't files in the checkout (ARCHITECTURE.md);
    ** extracted via `fossil wiki`/`fossil ticket` subprocess calls instead
    ** of walking the filesystem. Both assume cwd is inside an open
    ** fossil-see checkout, same assumption `viki cache push/pull` already
    ** make. */
    index_wiki(db, emb, modelId, &nWiki, &nWikiChunked);
    index_tickets(db, emb, modelId, &nTickets, &nTicketChunked);
    index_forum(db, emb, modelId, &nForum, &nForumChunked);

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
    return 0;
}
