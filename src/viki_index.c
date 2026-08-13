#include "viki_index.h"
#include "sha256.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int index_file(sqlite3 *db, const char *path, int *nFiles, int *nChunked,
                       viki_embedder *emb, const char *modelId){
    struct stat st;
    char *data;
    size_t len;
    char hash[65];
    char cached_hash[65];

    if( stat(path, &st) != 0 || !S_ISREG(st.st_mode) ) return 0;

    (*nFiles)++;

    if( previously_seen_unchanged(db, path, (long)st.st_mtime, cached_hash)
        && chunk_count_already_present(db, cached_hash, modelId) ){
        /* mtime unchanged AND we already have chunks for this exact
        ** (hash, model_id) pair -- nothing new to compute even if the
        ** model_id being requested differs from a previous run's. */
        return 0;
    }

    data = read_whole_file(path, &len);
    if( !data ) return 0;
    if( looks_binary(data, len) ){ free(data); return 0; }

    viki_sha256_hex(data, len, hash);

    if( !chunk_count_already_present(db, hash, modelId) ){
        insert_chunks(db, hash, data, len, emb, modelId);
        (*nChunked)++;
    }
    upsert_source(db, path, hash, (long)st.st_mtime);

    free(data);
    return 0;
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
    char *errmsg = NULL;
    const char *modelId = emb ? viki_embedder_model_id(emb) : VIKI_MODEL_NONE;

    if( sqlite3_exec(db, "BEGIN", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: BEGIN failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    walk(db, zDir, &nFiles, &nChunked, emb, modelId);

    if( sqlite3_exec(db, "COMMIT", NULL, NULL, &errmsg) != SQLITE_OK ){
        fprintf(stderr, "viki index: COMMIT failed: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return 1;
    }

    fprintf(stderr, "viki index: scanned %d file(s), (re)chunked %d (model_id=%s)\n",
            nFiles, nChunked, modelId);
    return 0;
}
