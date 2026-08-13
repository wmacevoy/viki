#ifndef VIKI_ASK_H
#define VIKI_ASK_H

#include <sqlite3.h>
#include "embed.h"

#define VIKI_CANDIDATE_POOL 40

typedef struct {
    char hash[65];
    int chunk_ix;
    char source[512];  /* best-effort path from viki_source, or "(unknown)" */
    char snippet[512]; /* best snippet/excerpt we've seen for this hit */
    double rrf;
} viki_ask_result;

/* Core retrieval: FTS5 BM25 top-K unioned with an ndvss cosine-similarity
** top-K (brute-force scan over viki_chunk.embedding for emb's model_id,
** when emb is non-NULL), combined by reciprocal rank fusion. Writes up
** to maxResults hits into results (already sorted best-first) and
** returns the actual count. Shared by the CLI (`viki ask`) and `viki
** serve`'s HTTP handlers so there's exactly one retrieval implementation,
** not two that can drift.
**
** If emb is NULL, this is BM25-only -- VIKI_DESIGN.md's required
** standalone path -- silently; callers that want to tell a human/agent
** which mode ran should check emb themselves (viki_embedder_model_id). */
int viki_ask_query(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb,
                    viki_ask_result *results, int maxResults);

/* `viki ask "<query>"` CLI command: calls viki_ask_query and prints
** human-readable output (plus a degraded-mode notice when emb is NULL).
** Returns 0 on success. */
int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb);

#endif
