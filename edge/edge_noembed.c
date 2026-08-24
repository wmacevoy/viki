/* The whole coupling between viki's retrieval core and ONNX Runtime: three
** symbols. viki_ask_query() calls them only inside `if( emb )`, so a build
** that never constructs an embedder never reaches them -- but they must still
** RESOLVE at link time. Supplying them as stubs is what lets the read path
** build with no onnxruntime at all, which is the phone/wasm tier.
**
** They abort rather than return a plausible value: reaching one means a
** caller obtained an embedder handle from somewhere, and silently returning
** dim=0 would corrupt a vector query instead of failing loudly. */
#include <stdio.h>
#include <stdlib.h>
#include "embed.h"

int viki_embed(viki_embedder *e, const char *z, float *out){
    (void)e; (void)z; (void)out;
    fprintf(stderr, "viki edge: built without an embedder; viki_embed unreachable\n");
    abort();
}
int viki_embedder_dim(const viki_embedder *e){
    (void)e;
    fprintf(stderr, "viki edge: built without an embedder; viki_embedder_dim unreachable\n");
    abort();
}
const char *viki_embedder_model_id(const viki_embedder *e){
    (void)e;
    fprintf(stderr, "viki edge: built without an embedder; model_id unreachable\n");
    abort();
}
