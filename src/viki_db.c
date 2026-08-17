#include "viki_db.h"
#include <stdio.h>
#include <string.h>

/* Declared, not included via a header, because sqlite-ndvss ships no
** public header -- sqlite3ext.h + SQLITE_EXTENSION_INIT1 is meant to be
** the only contract a consumer needs (see vendor/sqlite-ndvss/CLAUDE.md). */
extern int sqlite3_ndvss_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS viki_chunk("
    "  content_hash TEXT NOT NULL,"
    "  model_id     TEXT NOT NULL,"
    "  chunk_ix     INTEGER NOT NULL,"
    "  chunk_text   TEXT NOT NULL,"
    "  embedding    BLOB,"
    "  PRIMARY KEY(content_hash, model_id, chunk_ix)"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunk_fts USING fts5("
    "  chunk_text,"
    "  content_hash UNINDEXED,"
    "  model_id UNINDEXED,"
    "  chunk_ix UNINDEXED,"
    "  tokenize = 'porter unicode61'"
    ");"
    /* Side table: path -> (content_hash, mtime), so `viki index` can skip
    ** re-hashing/re-chunking files whose mtime hasn't moved. Not part of
    ** VIKI_DESIGN.md's schema block (which is intentionally content-
    ** addressed, path-independent) -- this is a local bookkeeping aid for
    ** incremental indexing and for `viki ask` to show a human-friendly
    ** source hint. Rebuildable like everything else here.
    **
    ** It carries a third job that is easy to miss and easy to break: it is
    ** the LIVENESS SET. A content_hash referenced by no row here is
    ** unreachable content, and `viki index` deletes its chunks on that
    ** basis (viki_index.c's gc_orphan_chunks). Two consequences. Deleting
    ** a row is therefore not free bookkeeping -- it can retire chunks --
    ** which is why viki_index.c only ever deletes rows it can prove are
    ** stale. And because content is shared (two paths with identical bytes
    ** collapse to one content_hash), liveness is a reference count, never
    ** a one-to-one mapping. */
    "CREATE TABLE IF NOT EXISTS viki_source("
    "  path TEXT PRIMARY KEY,"
    "  content_hash TEXT NOT NULL,"
    "  mtime INTEGER NOT NULL"
    ");";

void viki_db_register_ndvss(void){
    /* sqlite3_auto_extension runs the given entrypoint on every future
    ** sqlite3_open*() call in this process. Compiled with -DSQLITE_CORE
    ** (see build/build.sh), sqlite3_ndvss_init binds directly against the
    ** real linked-in sqlite3 API instead of the loadable-extension shim
    ** table -- same static-link pattern SQLite's own FTS5/RTREE use. */
    sqlite3_auto_extension((void(*)(void))sqlite3_ndvss_init);
}

int viki_db_open(const char *zPath, sqlite3 **out){
    sqlite3 *db = NULL;
    char *errmsg = NULL;
    int rc;

    rc = sqlite3_open_v2(zPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    *out = db;
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki: cannot open cache db '%s': %s\n", zPath, sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &errmsg);
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki: schema init failed: %s\n", errmsg ? errmsg : "(no message)");
        sqlite3_free(errmsg);
        return rc;
    }

    return SQLITE_OK;
}
