#ifndef VIKI_ASK_H
#define VIKI_ASK_H

#include <sqlite3.h>
#include "embed.h"

/* `viki ask "<query>"`: FTS5 BM25 top-K unioned with an ndvss cosine-
** similarity top-K (brute-force scan over viki_chunk.embedding for
** emb's model_id), combined by reciprocal rank fusion, printed as
** content_hash, source path (best-effort), fused score, snippet.
**
** If emb is NULL (no embedding model available), this is BM25-only --
** VIKI_DESIGN.md's required standalone path -- and prints an explicit
** degraded-mode notice rather than silently doing less than advertised.
** Returns 0 on success. */
int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb);

#endif
