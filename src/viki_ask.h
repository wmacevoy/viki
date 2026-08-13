#ifndef VIKI_ASK_H
#define VIKI_ASK_H

#include <sqlite3.h>

/* `viki ask "<query>"`: FTS5 BM25 retrieval over chunk_fts, top-K results
** printed as content_hash, source path (best-effort), score, snippet.
**
** This is currently the ONLY retrieval path: no ONNX embedding pipeline
** exists yet (see FINDINGS.md / VIKI_DESIGN.md rung 1/2), so there is no
** ndvss cosine-similarity leg to fuse in. VIKI_DESIGN.md explicitly
** requires this to work standalone ("Works with no model present
** (BM25-only, degraded flag)") -- this implements exactly that path, and
** prints a degraded-mode notice rather than silently pretending hybrid
** retrieval is happening. Returns 0 on success. */
int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK);

#endif
