#ifndef VIKI_INDEX_H
#define VIKI_INDEX_H

#include <sqlite3.h>
#include "embed.h"

/* `viki index <dir>`: walk dir recursively, chunk + hash text files,
** incrementally maintain viki_chunk + chunk_fts in db. If emb is
** non-NULL, also computes and stores a real embedding per chunk under
** emb's model_id; if NULL, chunks are stored under model_id "none"
** (rung 0 / BM25-only -- VIKI_DESIGN.md's required degraded path).
** Returns 0 on success. */
int viki_cmd_index(sqlite3 *db, const char *zDir, viki_embedder *emb);

#endif
