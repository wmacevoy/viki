/* viki edge's EMBEDDER, backed by JavaScript.
**
** viki_ask_query() does not know or care how a query becomes a vector -- it
** takes a `viki_embedder *` and calls exactly three functions on it. That
** interface is the seam, so the edge supplies its OWN implementation instead
** of ONNX Runtime, and viki_ask.c needs no change at all. Keeping
** viki_ask_query() as the single implementation is the property that stops
** the CLI, /api/ask and this build from diverging (CLAUDE.md), and it would
** survive a dart:ffi front end for the same reason.
**
** THE DIVISION OF LABOUR, and why it falls this way:
**   C  (here)  -- WordPiece tokenization, via viki's OWN tokenizer.c. This
**                 MUST be viki's tokenizer and not a JS reimplementation:
**                 D-11 makes an embedding a deterministic function of
**                 (content_hash, model_id, chunk_params), and a query
**                 tokenized differently from the corpus is simply in a
**                 different space. tokenizer.c has no POSIX dependency, so
**                 it compiles here unchanged.
**   JS (page)  -- runs the ONNX graph via onnxruntime-web. ORT has no static
**                 wasm library available to link against without building it
**                 from source, and there is no reason to: the model is a pure
**                 function and JS can drive it.
**   C  (here)  -- mean-pool and L2-normalize the hidden states. Deliberately
**                 NOT done in JS: it mirrors embed.c:273-289 exactly, and a
**                 second copy of that arithmetic in another language is the
**                 kind of duplication this project keeps getting bitten by.
**
** If no query vector has been set, edge_ask() passes NULL and the whole thing
** degrades to BM25 + the literal leg, which is a required path here, not a
** failure. */
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "embed.h"
#include "tokenizer.h"

#define EDGE_MAX_SEQ_LEN 256      /* must match VIKI_MAX_SEQ_LEN in embed.c */
#define EDGE_MAX_DIM     4096

/* viki_embedder is an opaque typedef in embed.h; this build supplies the
** definition. The handle carries nothing -- state is file-static below --
** but it must be a real object so callers can hold a non-NULL pointer. */
struct viki_embedder { int inUse; };

static struct viki_embedder g_emb = { 0 };
static viki_vocab *g_vocab = NULL;
static float g_qvec[EDGE_MAX_DIM];
static int   g_dim = 0;
static char  g_modelId[128] = "";

/* ---- the three symbols viki_ask.c references ------------------------- */

int viki_embed(viki_embedder *e, const char *zText, float *outVec){
    (void)e; (void)zText;           /* the vector was computed by the page */
    if( g_dim <= 0 ) return 1;
    memcpy(outVec, g_qvec, sizeof(float) * (size_t)g_dim);
    return 0;
}
int viki_embedder_dim(const viki_embedder *e){ (void)e; return g_dim; }
const char *viki_embedder_model_id(const viki_embedder *e){ (void)e; return g_modelId; }

/* ---- exported to JS -------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE
int edge_vocab_load(const char *zPath){
    if( g_vocab ) viki_vocab_free(g_vocab);
    g_vocab = viki_vocab_load(zPath);
    return g_vocab ? 1 : 0;
}

/* Writes token ids for zText into outIds and returns the count. The page
** feeds these straight to ORT as input_ids, with attention_mask all-ones and
** token_type_ids all-zeros -- which is what embed.c does, and is why the
** plain mean below is the masked mean. */
EMSCRIPTEN_KEEPALIVE
int edge_tokenize(const char *zText, int *outIds, int maxLen){
    if( !g_vocab ) return 0;
    if( maxLen > EDGE_MAX_SEQ_LEN ) maxLen = EDGE_MAX_SEQ_LEN;
    return viki_tokenize(g_vocab, zText, outIds, maxLen);
}

/* Takes ORT's hidden states [1, seqLen, dim] row-major float32 and stores the
** pooled, normalized query vector. MIRRORS embed.c:273-289 -- if that changes,
** this changes. */
EMSCRIPTEN_KEEPALIVE
int edge_set_query_from_hidden(const float *hidden, int seqLen, int dim, const char *zModelId){
    int i, j;
    float norm = 0.0f;
    if( dim <= 0 || dim > EDGE_MAX_DIM || seqLen <= 0 ) return 0;
    for( i = 0; i < dim; i++ ) g_qvec[i] = 0.0f;
    for( i = 0; i < seqLen; i++ ){
        const float *row = hidden + (size_t)i * (size_t)dim;
        for( j = 0; j < dim; j++ ) g_qvec[j] += row[j];
    }
    for( i = 0; i < dim; i++ ){ g_qvec[i] /= (float)seqLen; norm += g_qvec[i] * g_qvec[i]; }
    norm = norm > 0.0f ? (float)sqrt((double)norm) : 1.0f;
    for( i = 0; i < dim; i++ ) g_qvec[i] /= norm;
    g_dim = dim;
    snprintf(g_modelId, sizeof(g_modelId), "%s", zModelId ? zModelId : "");
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void edge_clear_query_vector(void){ g_dim = 0; }

/* NULL when no vector is loaded -- viki_ask_query() then runs BM25 + literal
** only, which is exactly what the no-model build does. */
viki_embedder *edge_embedder(void){ return g_dim > 0 ? &g_emb : NULL; }
