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

/* FRAGMENT bits -- what a reader is NOT being shown, computed at query
** time and never stored. `viki index` slices a document into 40-line
** chunks and stores each slice raw, so a chunk taken from the middle of a
** document is a dangling excerpt that reads as a complete text: "and
** twenty years ago ..." looks like an assertion rather than the tail of a
** sentence. Now that every hit carries a citable content_hash (KICKOFF.md
** deliverable 2), an agent can quote that excerpt precisely -- which turns
** an old cosmetic wart into a PROVENANCE defect. These bits are what a
** surface uses to say so.
**
** HEAD/TAIL are properties of the CHUNK within its document; CUT is a
** property of the EXCERPT within the chunk. They are three different
** facts and are deliberately reported separately (see viki_ask.c). */
#define VIKI_FRAG_HEAD 0x1u  /* chunk_ix > 0: document text precedes this chunk */
#define VIKI_FRAG_TAIL 0x2u  /* chunk_ix < max(chunk_ix): document text follows it */
#define VIKI_FRAG_CUT  0x4u  /* .snippet is itself a truncated PREFIX of the chunk */

/* The literal marker strings. Every human-readable surface must use these
** rather than spelling its own, so the CLI, `viki serve`'s HTML page and
** any future surface mark the same fact the same way -- and so a probe can
** grep for one string.
**
** THEY CONTAIN NO DOTS, ON PURPOSE. FTS5's snippet() already inserts
** " ... " where it elided text from INSIDE a chunk, which is a different
** fact at a different scope (intra-chunk elision, not "this chunk is a
** slice of a longer document"). Keeping the two notations in disjoint
** alphabets -- ellipsis for elision, angle brackets for fragmentation --
** is what stops a reader collapsing them into one vague "something is
** missing here". They also avoid '[' and ']', which the FTS snippet call
** in viki_ask.c already spends on match highlighting. */
#define VIKI_MARK_HEAD "<<document continues above>>"
#define VIKI_MARK_TAIL "<<document continues below>>"
#define VIKI_MARK_CUT  "<<excerpt truncated>>"

typedef struct {
    char hash[65];     /* content_hash: sha256 of the source text, the citable identity */
    int chunk_ix;
    char source[512];  /* best-effort path from viki_source, or "(unknown)" */
    char snippet[512]; /* best snippet/excerpt we've seen for this hit, UNDECORATED */
    double rrf;
    unsigned legs;     /* internal bookkeeping: VIKI_LEG_* already scored into rrf */
    unsigned frag;     /* VIKI_FRAG_* -- see above; display-side only, nothing stored */
    int chunk_count;   /* chunks in this content_hash, or 0 when the extent is unknown */
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
** cannot shift the position of anything a script wants to read.
**
** The excerpt line carries the VIKI_MARK_* fragment markers when the
** corresponding VIKI_FRAG_* bit is set: VIKI_MARK_HEAD before the excerpt,
** then VIKI_MARK_CUT and VIKI_MARK_TAIL after it, in that order (the
** excerpt's own cut is nearer the excerpt than the document's). The header
** line is deliberately NOT touched -- test/m1.sh (G1-G6),
** build/forum-e2e-probe.sh (C6) and build/model-uv-e2e-probe.sh (F11) all
** parse it by position, and `<hash>#<ix>` is a citation format, not a
** display string. */
int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb);

#endif
