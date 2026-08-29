/*
** viki_embed_onnx.c -- a REAL embedder behind viki-core's ABI.
**
** THE EMBEDDER ASKS; THE HOST RESOLVES. It names what it needs -- the graph,
** the vocabulary, the pinned dimension -- and where those come from is not
** its business. A directory works, a blob out of a diary works, a wasm fetch
** works, an array baked into the binary works.
**
** THAT IS A CORRECTION, NOT A FLOURISH. The first version took
** $VIKI_MODEL_DIR, which is a filesystem assumption wearing a plugin's
** clothes: there is no directory in wasm and none inside a sandboxed robot,
** which are two of the three places this is supposed to run. Asking by name
** is what makes the seam real rather than shaped like one.
**
** It WRAPS src/embed.c and src/tokenizer.c rather than reimplementing them.
** Those carry measured decisions -- WordPiece conformance, mean pooling over
** the attention mask, L2 normalisation -- and rewriting ONNX plumbing to
** prove a seam would be the worst trade available: new bugs in the one place
** a bug is invisible, since a wrong vector still ranks something.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* embed.c's own entry points collide with the ABI symbol names below, so
** EMBED.C IS COMPILED with -Dviki_embedder_close=viki_embedder_close_impl and
** this file is not. src/ is untouched, so the fossil-linked binary keeps
** building against it -- which is why the rename lives in the build. */
typedef struct viki_embedder viki_embedder;
viki_embedder *viki_embedder_open_mem(const void *pGraph, size_t nGraph,
                                      const char *zVocab, size_t nVocab,
                                      const char *zModelId, int nDim);
void viki_embedder_close_impl(viki_embedder*);
int  viki_embed(viki_embedder*, const char *zText, float *outVec);

typedef const void *(*viki_blob_fn)(void *pApp, const char *zName, size_t *pn);

typedef struct { viki_embedder *e; int nDim; } Adapter;

int viki_embedder_open(viki_blob_fn xBlob, void *pApp, int *pnDim, void **ppOut){
    Adapter *a;
    const void *pGraph, *pVocab, *pDim;
    size_t nGraph = 0, nVocab = 0, nDim = 0;
    char zDim[32];
    int dim;

    if( !xBlob ) return 1;
    pGraph = xBlob(pApp, "model.onnx", &nGraph);
    pVocab = xBlob(pApp, "vocab.txt",  &nVocab);
    pDim   = xBlob(pApp, "dim",        &nDim);
    if( !pGraph || !nGraph ){ fprintf(stderr,"viki-embed-onnx: no model.onnx\n"); return 1; }
    if( !pVocab || !nVocab ){ fprintf(stderr,"viki-embed-onnx: no vocab.txt\n"); return 1; }
    if( !pDim || nDim==0 || nDim>=sizeof zDim ){
        fprintf(stderr,"viki-embed-onnx: no pinned dimension\n"); return 1; }
    memcpy(zDim, pDim, nDim); zDim[nDim] = 0;
    dim = atoi(zDim);
    if( dim<=0 ){ fprintf(stderr,"viki-embed-onnx: bad dimension '%s'\n", zDim); return 1; }

    a = (Adapter*)calloc(1, sizeof(Adapter));
    if( !a ) return 1;
    /* CreateSessionFromArray, so the graph never needs to be a file. */
    a->e = viki_embedder_open_mem(pGraph, nGraph, (const char*)pVocab, nVocab, "onnx", dim);
    if( !a->e ){ free(a); return 1; }
    a->nDim = dim;
    *pnDim  = dim;
    *ppOut  = a;
    return 0;
}

int viki_embedder_embed(void *pApp, const char *zText, float *aOut, int nDim){
    Adapter *a = (Adapter*)pApp;
    if( !a || nDim != a->nDim ) return 1;
    /* Non-zero is DECLINE, not disaster: core stores the assertion without a
    ** vector and reports degraded rather than losing the write. */
    return viki_embed(a->e, zText, aOut)==0 ? 0 : 1;
}

void viki_embedder_close(void *pApp){
    Adapter *a = (Adapter*)pApp;
    if( !a ) return;
    viki_embedder_close_impl(a->e);
    free(a);
}
