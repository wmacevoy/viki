#include "viki_db.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Declared, not included via a header, because sqlite-ndvss ships no
** public header -- sqlite3ext.h + SQLITE_EXTENSION_INIT1 is meant to be
** the only contract a consumer needs (see vendor/sqlite-ndvss/CLAUDE.md). */
extern int sqlite3_ndvss_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);

/* Shared by SCHEMA_SQL (fresh caches) and migrate_chunk_fts() (existing
** ones) so the two definitions cannot drift apart -- the failure mode this
** file already warns about for ALTER TABLE, one level up. */
#define CHUNK_FTS_CREATE \
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunk_fts USING fts5(" \
    "  chunk_text," \
    "  content_hash UNINDEXED," \
    "  model_id UNINDEXED," \
    "  chunk_ix UNINDEXED," \
    "  content = 'viki_chunk'," \
    "  content_rowid = 'rowid'," \
    "  tokenize = 'porter unicode61'" \
    ");"

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS viki_chunk("
    "  content_hash TEXT NOT NULL,"
    "  model_id     TEXT NOT NULL,"
    "  chunk_ix     INTEGER NOT NULL,"
    "  chunk_text   TEXT NOT NULL,"
    "  embedding    BLOB,"
    "  PRIMARY KEY(content_hash, model_id, chunk_ix)"
    ");"
    /* EXTERNAL-CONTENT FTS5. `content='viki_chunk'` means FTS5 stores only
    ** the inverted index and reads column VALUES back from viki_chunk by
    ** rowid, instead of keeping its own copy of every chunk's text. That
    ** copy was 34.2% of a 5.35MB cache here (chunk_fts_content 1,658,880
    ** bytes) -- pure duplication of viki_chunk.chunk_text, and it rode
    ** along in the blob D-12 ships via `fossil uv` (QUEUE 40).
    **
    ** Contentless (`content=''`) would ALSO drop the copy but breaks
    ** snippet(), which every surface and both fragment markers depend on.
    ** External content keeps snippet() working -- the UNINDEXED columns
    ** below are still queryable, fetched from viki_chunk on demand, so
    ** viki_ask.c's BM25 query needed no change at all.
    **
    ** THE TRAP, measured before writing this: an external-content table
    ** must be deleted from BEFORE its content row goes away. FTS5 needs to
    ** re-read the original text to know which tokens to remove; if
    ** viki_chunk's row is already gone the DELETE silently succeeds and
    ** THE TEXT STAYS SEARCHABLE. That is exactly the withdrawal defect
    ** gc_orphan_chunks() was written to prevent, so its two DELETEs are
    ** now ordered fts-then-chunk and must stay that way. */
    CHUNK_FTS_CREATE
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

void viki_db_register_ndvss(void){
    /* sqlite3_auto_extension runs the given entrypoint on every future
    ** sqlite3_open*() call in this process. Compiled with -DSQLITE_CORE
    ** (see build/build.sh), sqlite3_ndvss_init binds directly against the
    ** real linked-in sqlite3 API instead of the loadable-extension shim
    ** table -- same static-link pattern SQLite's own FTS5/RTREE use. */
    sqlite3_auto_extension((void(*)(void))sqlite3_ndvss_init);
}

/* Rebuild chunk_fts as an external-content table on a cache built before
** that change. `CREATE VIRTUAL TABLE IF NOT EXISTS` will not touch a table
** that already exists, so without this an old cache keeps the duplicating
** schema forever and silently never gets the 34% back (QUEUE 40).
**
** Detection is on the stored SQL rather than a version counter, because
** the cache has no version column and adding one would need this same
** migration to bootstrap it. Rebuilding is safe and cheap: chunk_fts is
** DERIVED from viki_chunk (D-10), so dropping it loses nothing that
** 'rebuild' cannot regenerate from the chunk text that is already there.
**
** Failure is deliberately non-fatal in the same spirit as the ALTER TABLE
** loop above: a cache whose FTS could not be migrated still answers on the
** vector leg, and refusing to open would be worse. */
static void migrate_chunk_fts(sqlite3 *db){
    sqlite3_stmt *st = NULL;
    int bExternal = 0, bFound = 0;

    if( sqlite3_prepare_v2(db,
            "SELECT sql FROM sqlite_schema WHERE type='table' AND name='chunk_fts'",
            -1, &st, NULL) != SQLITE_OK ) return;
    if( sqlite3_step(st) == SQLITE_ROW ){
        const char *zSql = (const char*)sqlite3_column_text(st, 0);
        bFound = 1;
        /* strstr on the stored SQL: FTS5 records the option list verbatim,
        ** and this file is the only thing that ever writes it. */
        if( zSql && strstr(zSql, "content = 'viki_chunk'") ) bExternal = 1;
    }
    sqlite3_finalize(st);
    if( !bFound || bExternal ) return;

    /* Order matters only in that the drop must precede the create; the
    ** rebuild then reads viki_chunk, which was never touched. */
    if( sqlite3_exec(db, "DROP TABLE chunk_fts", NULL, NULL, NULL) != SQLITE_OK ) return;
    if( sqlite3_exec(db, CHUNK_FTS_CREATE, NULL, NULL, NULL) != SQLITE_OK ) return;
    sqlite3_exec(db, "INSERT INTO chunk_fts(chunk_fts) VALUES('rebuild')", NULL, NULL, NULL);
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
    /* Owner-only. SQLite creates the db 0644 minus umask, and this file is
    ** plaintext, key-free, and a complete copy of the corpus (QUEUE 35).
    ** chmod() after open rather than a umask dance so it applies to a db
    ** created by any earlier version too. Best-effort: a cache on a
    ** filesystem with no permission model is not a reason to refuse to
    ** run, and the WAL/journal siblings inherit from the directory. */
    (void)chmod(zPath, 0600);

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
        migrate_chunk_fts(db);
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
