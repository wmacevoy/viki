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

/* THE RAW QUERY SURFACE. Warren, 2026-08-25: "state includes search -- there
** must be a way to reduce a state by a question, otherwise retrieval becomes
** impossible ... direct sql and vector queries by agents".
**
** Why this has to exist rather than "just open the SQLite file": a stock
** sqlite3 opening .viki/cache.db gets `no such function:
** ndvss_cosine_similarity_f`. The vector engine is statically linked into
** viki and registered via sqlite3_auto_extension, so THIS PROCESS is the only
** place a vector query can run. Without this entry point the raw layer is not
** merely inconvenient, it is unreachable, and `ask` becomes the only way to
** ask anything.
**
** READ-ONLY BY CONSTRUCTION, not by inspecting the SQL. Parsing statements to
** decide what is safe is a losing game; SQLITE_OPEN_READONLY is checked by
** SQLite itself. The cache is a projection (D-10) -- rebuild it, never repair
** it through a query console.
**
** bJson emits one object per row so an agent needs no parser for a display
** format. */
int viki_cmd_sql(const char *zPath, const char *zSql, int bJson);

/* One-time process-wide registration of sqlite-ndvss's entrypoint via
** sqlite3_auto_extension, so every viki_db_open'd connection gets its SQL
** functions automatically. Safe to call more than once (sqlite3_auto_extension
** itself de-duplicates identical registrations). */
void viki_db_register_ndvss(void);

#endif
