/*
** viki_db.h -- local derived-cache database (VIKI_DESIGN.md).
**
** This is NOT the Fossil repo db. It is a disposable, rebuildable,
** local-only SQLite file holding the viki_chunk table + its FTS5 mirror
** (rung 0) and, eventually, ndvss vectors (rung 2). Never committed as a
** versioned Fossil artifact -- distributed only as a `fossil uv` blob
** (D-12), or simply rebuilt from scratch via `viki index`.
*/
#ifndef VIKI_DB_H
#define VIKI_DB_H

#include <sqlite3.h>

/* Default cache db location relative to a checkout root. Kept out of the
** Fossil-tracked tree (add to ignore-glob) since it's derived, not source. */
#define VIKI_DEFAULT_CACHE_DB ".viki/cache.db"

/* Opens (creating if needed) the cache db at zPath, applies the schema
** (idempotent), and registers ndvss's SQL functions on the connection.
** Returns SQLITE_OK on success; *out is left open. On failure, *out may
** still be non-NULL (per sqlite3_open semantics) -- caller should check
** the return code before using it, and call sqlite3_close either way. */
int viki_db_open(const char *zPath, sqlite3 **out);

/* One-time process-wide registration of sqlite-ndvss's entrypoint via
** sqlite3_auto_extension, so every viki_db_open'd connection gets its SQL
** functions automatically. Safe to call more than once (sqlite3_auto_extension
** itself de-duplicates identical registrations). */
void viki_db_register_vec(void);

/* Brings the DERIVED vec0 index (`viki_vec`) into agreement with
** `viki_chunk`, creating it if absent and rebuilding it if the row counts
** disagree. Cheap when already in sync. Safe to call on a BM25-only cache
** (no embeddings => no-op, not an error). Returns 0 on success.
**
** Call before any vector query and after indexing. It is NOT called from
** viki_db_open() on purpose: `viki capture` and `viki notes` open the same
** database and have no business paying for a vector index. */
int viki_db_vec_sync(sqlite3 *db);

#endif
