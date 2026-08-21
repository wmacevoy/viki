#include "viki_db.h"
#include <stdio.h>
#include <string.h>

/* sqlite-vec's amalgamation ships sqlite-vec.h, but declaring the one
** entry point by hand keeps this file compilable without the download
** cache on the include path -- the same reason the sqlite-ndvss build this
** replaced declared its init function here. */
extern int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);

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
    /* Capture-loop projection. Like everything else in this file it is
    ** DERIVED and disposable (D-10): captures/*.md in the checkout are the
    ** truth, and viki_note is rebuilt from them wholesale on every index --
    ** see viki_note.h for why a full rebuild rather than incremental
    ** invalidation. It exists because similarity ranking cannot filter by
    ** state or aggregate, and knows nothing about "last". */
    "CREATE TABLE IF NOT EXISTS viki_note("
    "  note_id     TEXT PRIMARY KEY,"
    "  ts          TEXT NOT NULL,"   /* ISO-8601 UTC: lexical order IS time order */
    "  type        TEXT,"
    "  place       TEXT,"
    "  who         TEXT,"
    "  due         TEXT,"
    "  state       TEXT,"
    "  closes      TEXT,"   /* note_id this one supersedes -- see viki_note.h */
    "  claimed     TEXT,"   /* ISO when --who was set */
    "  lease       TEXT,"   /* ISO when the holder's declared availability lapses */
    "  challenge   TEXT,"   /* "<who> <ISO>" -- an unanswered are-you-still-on-this */
    "  stolen_from TEXT,"   /* prior holder; a steal is a supersession, not an overwrite */
    "  text        TEXT NOT NULL,"
    "  source_path TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS viki_note_ts ON viki_note(ts DESC);"
    "CREATE INDEX IF NOT EXISTS viki_note_place ON viki_note(place);"
    "CREATE TABLE IF NOT EXISTS viki_source("
    "  path TEXT PRIMARY KEY,"
    "  content_hash TEXT NOT NULL,"
    "  mtime INTEGER NOT NULL,"
    /* ISO-8601 UTC, or '' when the source has no meaningful time. Separate
    ** from mtime, which is a fast-skip optimisation and is deliberately 0
    ** for every virtual source (see index_text_blob). ts is the ARTIFACT's
    ** time -- when the check-in was made, the note written, the ticket
    ** changed -- and it is what makes "what changed recently" answerable.
    ** Text, not an integer, so lexicographic order IS chronological order
    ** and no date arithmetic is needed anywhere. */
    "  ts TEXT NOT NULL DEFAULT ''"
    ");";

void viki_db_register_vec(void){
    /* sqlite3_auto_extension runs the given entrypoint on every future
    ** sqlite3_open*() call in this process. Compiled with -DSQLITE_CORE
    ** (see build/build.sh), sqlite3_vec_init binds directly against the
    ** real linked-in sqlite3 API instead of the loadable-extension shim
    ** table -- same static-link pattern SQLite's own FTS5/RTREE use. */
    sqlite3_auto_extension((void(*)(void))sqlite3_vec_init);
}

/* ---- the DERIVED vec0 index -----------------------------------------
**
** `viki_vec` is a LOCAL INDEX, never a source of truth. D-10 says vectors
** are projections: rebuildable, disposable, never what travels between
** peers. `viki_chunk.embedding` remains the artifact D-11 computes once
** and D-12 ships as a fossil unversioned file; this table is derived from
** it and can be dropped and rebuilt at any time without asking a peer for
** anything. That is why swapping the vector ENGINE needs no epoch bump
** and no coordination -- nothing about the shared artifact changed.
**
** model_id is a PARTITION KEY rather than a filtered column, and that is
** correctness rather than tuning: vec0's KNN takes a global top-k, so
** post-filtering by model would return the k nearest across ALL epochs
** and then throw most of them away -- fewer than k rows, or none at all,
** whenever a second epoch is present. A partition key makes k a per-model
** k. (`VIKI_FTS_EPOCH_SLACK` in viki_ask.c is the BM25 leg's answer to the
** same problem; this is the vector leg's, and it is exact where the slack
** factor is a heuristic.)
**
** The dimension is read from the data rather than hardcoded to 384: vec0
** fixes it at CREATE time, and a wrong guess fails at INSERT with a
** dimension-mismatch that reads nothing like "your model changed". */
static int vec_dim(sqlite3 *db){
    sqlite3_stmt *st;
    int dim = 0;
    if( sqlite3_prepare_v2(db,
            "SELECT length(embedding)/4 FROM viki_chunk "
            "WHERE embedding IS NOT NULL LIMIT 1", -1, &st, NULL) != SQLITE_OK ){
        return 0;
    }
    if( sqlite3_step(st) == SQLITE_ROW ) dim = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return dim;
}

static sqlite3_int64 count_of(sqlite3 *db, const char *zSql){
    sqlite3_stmt *st;
    sqlite3_int64 n = -1;
    if( sqlite3_prepare_v2(db, zSql, -1, &st, NULL) != SQLITE_OK ) return -1;
    if( sqlite3_step(st) == SQLITE_ROW ) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

/* Attaches the SEPARATE index database as `vecdb`, creating it if needed.
**
** IT IS A SEPARATE FILE, and that is not tidiness. `.viki/cache.db` is the
** artifact D-11 computes once and D-12 ships between peers as a fossil
** unversioned file, and test/m1.sh's C10 byte-compares the cache a fresh
** clone PULLED against the one that was PUSHED, while C18 asserts the
** clone never wrote to it at all. A derived index living inside cache.db
** breaks both -- and would also push a local, rebuildable projection to
** every peer, which is exactly what D-10 says not to do. `.viki/vec.db`
** is local, disposable, and safe to delete at any time.
**
** Returns 0 when `vecdb` is usable. */
static int vec_attach(sqlite3 *db){
    sqlite3_stmt *st;
    char zPath[2048];
    char *zSql, *zErr = NULL;
    const char *zMain = NULL;
    size_t n;
    int rc;

    /* Already attached on this connection? */
    if( sqlite3_prepare_v2(db, "SELECT 1 FROM pragma_database_list WHERE name='vecdb'",
                           -1, &st, NULL) == SQLITE_OK ){
        rc = (sqlite3_step(st) == SQLITE_ROW);
        sqlite3_finalize(st);
        if( rc ) return 0;
    }

    if( sqlite3_prepare_v2(db, "SELECT file FROM pragma_database_list WHERE name='main'",
                           -1, &st, NULL) != SQLITE_OK ) return -1;
    if( sqlite3_step(st) == SQLITE_ROW ) zMain = (const char*)sqlite3_column_text(st, 0);
    if( !zMain || !zMain[0] ){ sqlite3_finalize(st); return -1; }  /* :memory: */
    snprintf(zPath, sizeof(zPath), "%s", zMain);
    sqlite3_finalize(st);

    /* Sibling of cache.db, whatever it is called. */
    n = strlen(zPath);
    if( n > 8 && strcmp(zPath + n - 8, "cache.db") == 0 ){
        snprintf(zPath + n - 8, sizeof(zPath) - (n - 8), "vec.db");
    }else{
        snprintf(zPath + n, sizeof(zPath) - n, ".vec");
    }

    zSql = sqlite3_mprintf("ATTACH DATABASE %Q AS vecdb", zPath);
    if( !zSql ) return -1;
    rc = sqlite3_exec(db, zSql, NULL, NULL, &zErr);
    sqlite3_free(zSql);
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki: could not attach vec index at %s: %s\n",
                zPath, zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        return -1;
    }
    return 0;
}

int viki_db_vec_sync(sqlite3 *db){
    char zSql[512];
    sqlite3_int64 nChunk, nVec;
    int dim = vec_dim(db);
    char *zErr = NULL;

    /* No embeddings at all is not a failure: it is BM25-only mode, which
    ** VIKI_DESIGN.md makes a required path. Say nothing and do nothing --
    ** and in particular do not create an index file for a cache that has
    ** nothing to index. */
    if( dim <= 0 ) return 0;
    if( vec_attach(db) != 0 ) return -1;

    snprintf(zSql, sizeof(zSql),
        "CREATE VIRTUAL TABLE IF NOT EXISTS vecdb.viki_vec USING vec0("
        "  model_id TEXT PARTITION KEY,"
        "  embedding FLOAT[%d] distance_metric=cosine"
        ")", dim);
    if( sqlite3_exec(db, zSql, NULL, NULL, &zErr) != SQLITE_OK ){
        fprintf(stderr, "viki: could not create vec index: %s\n",
                zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        return -1;
    }

    /* Cheap staleness test. A full compare would hash both sides; a count
    ** compare catches every way this cache actually changes -- indexing
    ** adds rows, `viki index`'s sweep deletes them, and `cache pull`
    ** replaces the file wholesale. It does NOT catch an in-place edit of
    ** an embedding at constant row count, which nothing in viki does:
    ** embeddings are keyed by (content_hash, model_id, chunk_ix) and new
    ** content means a new key. Rebuilding is cheap and disposable, so when
    ** in doubt this errs toward rebuilding. */
    nChunk = count_of(db, "SELECT count(*) FROM viki_chunk WHERE embedding IS NOT NULL");
    nVec   = count_of(db, "SELECT count(*) FROM vecdb.viki_vec");
    if( nChunk < 0 || nVec < 0 ) return -1;
    if( nChunk == nVec ) return 0;

    if( sqlite3_exec(db, "DELETE FROM vecdb.viki_vec", NULL, NULL, &zErr) != SQLITE_OK ){
        fprintf(stderr, "viki: could not clear vec index: %s\n",
                zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        return -1;
    }
    /* rowid is viki_chunk's rowid, which is what the retrieval join uses. */
    if( sqlite3_exec(db,
            "INSERT INTO vecdb.viki_vec(rowid, model_id, embedding) "
            "SELECT rowid, model_id, embedding FROM viki_chunk "
            "WHERE embedding IS NOT NULL", NULL, NULL, &zErr) != SQLITE_OK ){
        fprintf(stderr, "viki: could not populate vec index: %s\n",
                zErr ? zErr : "(no message)");
        sqlite3_free(zErr);
        return -1;
    }
    return 0;
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
    if( rc == SQLITE_OK ){
        /* MIGRATION. `CREATE TABLE IF NOT EXISTS` does nothing to a table that
        ** already exists, so a column added to the schema above is absent from
        ** every cache built before it -- and the failure is nasty: the schema
        ** step errors, and a query naming the new column returns NOTHING,
        ** which reads as "no matches" rather than "your cache is old".
        **
        ** SQLite has no ADD COLUMN IF NOT EXISTS, so the idiom is to run it
        ** and ignore the duplicate-column error. Cheap, idempotent, and it
        ** keeps an existing cache working instead of demanding a reindex --
        ** which matters even pre-alpha, because a stale cache that silently
        ** answers "nothing found" is worse than one that refuses to open.
        ** Any genuinely new column belongs in BOTH places, here and above. */
        /* EVERY column added after a table first shipped belongs in this
        ** list, not just the newest one. I fixed viki_source.ts here and
        ** missed viki_note.closes on the same day, and the second one
        ** surfaced as `viki notes: no such column: closes` on a cache that
        ** predated the --closes work. Adding a column is a two-place edit:
        ** the CREATE above (for fresh caches) and here (for existing ones). */
        static const char *azMigrate[] = {
            "ALTER TABLE viki_source ADD COLUMN ts TEXT NOT NULL DEFAULT ''",
            "ALTER TABLE viki_note ADD COLUMN closes TEXT",
            "ALTER TABLE viki_note ADD COLUMN claimed TEXT",
            "ALTER TABLE viki_note ADD COLUMN lease TEXT",
            "ALTER TABLE viki_note ADD COLUMN challenge TEXT",
            "ALTER TABLE viki_note ADD COLUMN stolen_from TEXT",
            NULL
        };
        int iMig;
        for( iMig = 0; azMigrate[iMig]; iMig++ ){
            /* Duplicate-column is the expected outcome on an up-to-date cache,
            ** so the error is discarded by design. Any OTHER failure is also
            ** discarded, which is the deliberate trade: a migration that
            ** cannot run must not stop viki from opening a cache it can still
            ** mostly use, and the query naming the column will say so
            ** precisely when it is actually needed. */
            sqlite3_exec(db, azMigrate[iMig], NULL, NULL, NULL);
        }
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS viki_source_ts ON viki_source(ts DESC)",
                     NULL, NULL, NULL);
    }
    if( rc != SQLITE_OK ){
        fprintf(stderr, "viki: schema init failed: %s\n", errmsg ? errmsg : "(no message)");
        sqlite3_free(errmsg);
        return rc;
    }

    return SQLITE_OK;
}
