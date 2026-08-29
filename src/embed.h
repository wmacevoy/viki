/*
** embed.h -- rung-2 sentence embeddings via ONNX Runtime (VIKI_DESIGN.md).
**
** Loads the pinned model (build/dist/model/{model.onnx,vocab.txt,
** viki-manifest.json}, produced by build/build.sh's download+verify
** step), tokenizes with WordPiece (tokenizer.h), runs the BERT encoder,
** mean-pools the last_hidden_state over the attention mask, and
** L2-normalizes -- the standard sentence-transformers recipe for
** all-MiniLM-L6-v2 (this is NOT the BERT pooler_output; that's a
** different, untrained-for-this-purpose head).
*/
#ifndef VIKI_EMBED_H
#define VIKI_EMBED_H

#include <stddef.h>

typedef struct viki_embedder viki_embedder;

/* Opens the embedder from a model directory containing model.onnx,
** vocab.txt, and viki-manifest.json. Returns NULL (with a stderr
** diagnostic) if the directory doesn't exist or fails to load -- this is
** the "no model present" case VIKI_DESIGN.md requires degrading
** gracefully from, not a hard error for callers to propagate as fatal. */
viki_embedder *viki_embedder_open(const char *zModelDir);
/* The same from BYTES -- no filesystem. wasm and a sandboxed robot have no
** directory to point at, and the graph, the vocabulary and the pin all
** travel as blobs. */
viki_embedder *viki_embedder_open_mem(const void *pGraph, size_t nGraph,
                                      const char *zVocab, size_t nVocab,
                                      const char *zModelId, int nDim);
void viki_embedder_close(viki_embedder *e);

const char *viki_embedder_model_id(const viki_embedder *e);

/* The epoch id used when no model is available: chunks are stored with
** embedding=NULL and are retrievable by BM25 only (VIKI_DESIGN.md rung 0/1).
** Lives here rather than in viki_index.c so writers and readers share it. */
#define VIKI_MODEL_NONE "none"

/* CHUNKING IS PART OF THE CACHE EPOCH, so it is declared next to the model id
** rather than privately in viki_index.c, where it used to live.
**
** D-11 makes an embedding a deterministic function of
** (content_hash, model_id, chunk_params). chunk_params was in neither the
** cache key nor the skip test, so two peers built with different
** VIKI_CHUNK_LINES wrote rows that COLLIDED on (content_hash, model_id,
** chunk_ix) while holding different text, and the merge resolved it
** first-writer-wins -- silently indexing one document's lines twice
** (FINDINGS.md, 2026-08-26).
**
** viki_cache_epoch_id() is the ONE derivation of the stored key. It must be
** used by every writer AND every reader: the first cut composed it only in the
** indexer, so `viki ask`'s vector leg went on filtering by the bare model id,
** matched nothing, and hybrid retrieval silently became BM25-only. m1's B2/B5/
** B7/J4 are what caught that. Writing it once is the only defence.
**
** Overridable only so the collision above can be reproduced in a test. */
#ifndef VIKI_CHUNK_LINES
#define VIKI_CHUNK_LINES 40
#endif

/* CHUNK OVERLAP, added 2026-08-26 because it was the measured top failure.
**
** test/retrieval-eval.sh's taxonomy found RIGHT DOCUMENT, WRONG CHUNK in 17 of
** 43 queries, 12 of them at rank 1: another chunk of the gold DOCUMENT
** outranked the chunk holding the answer. No model change addresses that.
** Fixed-size chunks with no overlap cut an answer away from the vocabulary
** that would find it -- a definition on line 39 and its subject on line 41 end
** up in different chunks and neither retrieves well.
**
** Overlapping windows give every boundary a second chance: consecutive chunks
** advance by STRIDE and share OVERLAP lines, so any span of <= OVERLAP lines
** appears whole in at least one chunk. The cost is roughly
** LINES/STRIDE times more chunks -- more storage, more embedding time, and
** more near-duplicate candidates for the pool to sort out. */
#ifndef VIKI_CHUNK_OVERLAP
#define VIKI_CHUNK_OVERLAP 10
#endif

/* How far the window advances. A stride of 0 or less would never terminate,
** so it is clamped rather than trusted: an override is a build flag, and a
** build flag that hangs the indexer is a bad way to find out. */
#define VIKI_CHUNK_STRIDE \
    ((VIKI_CHUNK_LINES - VIKI_CHUNK_OVERLAP) > 0 ? (VIKI_CHUNK_LINES - VIKI_CHUNK_OVERLAP) : 1)

/* Writes "<model_id>/c<chunk_lines>o<overlap>" into zBuf. A NULL embedder yields the
** degraded-path id ("none/c40"), which needs the suffix for exactly the same
** reason: a no-model peer still stores chunk_text, and two no-model peers that
** chunk differently collide just as readily. */
void viki_cache_epoch_id(const viki_embedder *e, char *zBuf, size_t nBuf);
int viki_embedder_dim(const viki_embedder *e);

/* Computes a dim-length L2-normalized embedding for zText into outVec
** (caller-allocated, >= viki_embedder_dim() floats). Returns 0 on
** success, nonzero on failure (stderr diagnostic). */
int viki_embed(viki_embedder *e, const char *zText, float *outVec);

#endif
