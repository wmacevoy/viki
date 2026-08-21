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

/* INCREMENTAL INDEX. sinceRcvid >= 0 indexes only the artifact classes that
** have received something above that rcvid; VIKI_SINCE_AUTO reads the stored
** high-water mark; VIKI_SINCE_FULL is an ordinary full pass.
**
** AN INCREMENTAL RUN IS NEVER AUTHORITATIVE. sweep_sources() retires any
** source it did not observe, and an incremental run deliberately does not
** observe the artifacts that did NOT change -- so sweeping after one would
** delete almost the entire cache. Every auth flag is forced off, which is the
** existing mechanism doing exactly what it was built for. The cost is that
** withdrawn content is not retired until the next full pass, which is the
** right direction to fail: a missed deletion is recoverable (D-10, the store
** is derived), a wrong one is not.
**
** This exists because an after-receive hook runs SYNCHRONOUSLY in the
** server's request path -- a full re-index there stalls the client that
** pushed. See QUEUE.md 28. */
#define VIKI_SINCE_FULL (-1)
#define VIKI_SINCE_AUTO (-2)
int viki_cmd_index_since(sqlite3 *db, const char *zDir, viki_embedder *emb, long sinceRcvid);

#endif
