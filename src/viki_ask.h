#ifndef VIKI_ASK_H
#define VIKI_ASK_H

#include <sqlite3.h>
#include "embed.h"

#define VIKI_CANDIDATE_POOL 40

/* One bit per retrieval leg. Recorded on the candidate itself so a leg can
** contribute to a candidate's RRF score AT MOST ONCE, however many rows that
** leg's SQL happens to return for the same chunk (see viki_ask.c's leg_hit). */
#define VIKI_LEG_FTS 0x1u    /* FTS5 BM25 keyword leg */
#define VIKI_LEG_VEC 0x2u    /* ndvss cosine-similarity vector leg */

typedef struct {
    char hash[65];     /* content_hash: sha256 of the source text, the citable identity */
    int chunk_ix;
    char source[512];  /* best-effort path from viki_source, or "(unknown)" */
    char snippet[512]; /* best snippet/excerpt we've seen for this hit */
    double rrf;
    unsigned legs;     /* internal bookkeeping: VIKI_LEG_* already scored into rrf */
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
** Returns 0 on success.
**
** Each hit is two lines on stdout -- a header line
**
**   [<rank>] rrf=<score>  <content_hash>#<chunk_ix>  <source>
**
** (fields separated by exactly two spaces, score always %.4f, content_hash
** always 64 lowercase hex) followed by the snippet indented four spaces. The
** content_hash is KICKOFF.md deliverable 2's "source content_hash" and the
** thing VIKI_DESIGN.md's agent contract says answers cite; it is also what
** `viki serve`'s /api/ask reports as "hash" and what /api/chunk?hash=&ix=
** takes, so `<content_hash>#<chunk_ix>` names one chunk everywhere. It comes
** first because it is the authoritative identity and is fixed-width, while
** <source> is a best-effort human hint that may be absent
** ("(source path unknown)") or contain spaces -- so it goes last, where it
** cannot shift the position of anything a script wants to read. */
int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb);

#endif
