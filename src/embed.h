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

typedef struct viki_embedder viki_embedder;

/* Opens the embedder from a model directory containing model.onnx,
** vocab.txt, and viki-manifest.json. Returns NULL (with a stderr
** diagnostic) if the directory doesn't exist or fails to load -- this is
** the "no model present" case VIKI_DESIGN.md requires degrading
** gracefully from, not a hard error for callers to propagate as fatal. */
viki_embedder *viki_embedder_open(const char *zModelDir);
void viki_embedder_close(viki_embedder *e);

const char *viki_embedder_model_id(const viki_embedder *e);
int viki_embedder_dim(const viki_embedder *e);

/* Computes a dim-length L2-normalized embedding for zText into outVec
** (caller-allocated, >= viki_embedder_dim() floats). Returns 0 on
** success, nonzero on failure (stderr diagnostic). */
int viki_embed(viki_embedder *e, const char *zText, float *outVec);

#endif
