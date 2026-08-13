#include "embed.h"
#include "tokenizer.h"

#include <onnxruntime_c_api.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIKI_MAX_SEQ_LEN 256

struct viki_embedder {
    const OrtApi *ort;
    OrtEnv *env;
    OrtSession *session;
    OrtMemoryInfo *memInfo;
    OrtAllocator *allocator;
    viki_vocab *vocab;

    char *inputIdsName, *attnMaskName, *tokenTypeName;
    char *outputName;

    char modelId[128];
    int dim;
};

static const OrtApi *get_ort_api(void){
    const OrtApiBase *base = OrtGetApiBase();
    return base->GetApi(ORT_API_VERSION);
}

/* Returns 1 (and prints to stderr) if status indicates an error, having
** released it either way. Returns 0 on success (status was NULL). */
static int check(const OrtApi *ort, OrtStatus *status, const char *what){
    if( !status ) return 0;
    fprintf(stderr, "viki embed: %s: %s\n", what, ort->GetErrorMessage(status));
    ort->ReleaseStatus(status);
    return 1;
}

static int read_manifest(const char *zPath, char *outModelId, size_t modelIdCap, int *outDim){
    FILE *f = fopen(zPath, "r");
    char buf[4096];
    size_t n;
    char *p;

    if( !f ) return 1;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    p = strstr(buf, "\"model_id\"");
    if( !p ) return 1;
    p = strchr(p, ':');
    if( !p ) return 1;
    p = strchr(p, '"');
    if( !p ) return 1;
    p++;
    {
        char *end = strchr(p, '"');
        size_t len;
        if( !end ) return 1;
        len = (size_t)(end - p);
        if( len >= modelIdCap ) len = modelIdCap - 1;
        memcpy(outModelId, p, len);
        outModelId[len] = '\0';
    }

    p = strstr(buf, "\"dim\"");
    if( !p ) return 1;
    p = strchr(p, ':');
    if( !p ) return 1;
    *outDim = atoi(p + 1);
    if( *outDim <= 0 ) return 1;

    return 0;
}

/* Finds the input/output index whose ORT-reported name CONTAINS zNeedle
** (not exact match -- some exports prefix/suffix these, e.g. "input.1"
** vs "input_ids"; substring match is more forgiving and still specific
** enough given these three canonical BERT input names don't collide). */
static char *find_name(const OrtApi *ort, OrtSession *session, OrtAllocator *alloc,
                        int isInput, const char *zNeedle){
    size_t count = 0, i;
    char *result = NULL;
    OrtStatus *countSt;

    countSt = isInput ? ort->SessionGetInputCount(session, &count)
                       : ort->SessionGetOutputCount(session, &count);
    if( countSt ){ ort->ReleaseStatus(countSt); return NULL; }

    for( i = 0; i < count && !result; i++ ){
        char *name = NULL;
        OrtStatus *st = isInput
            ? ort->SessionGetInputName(session, i, alloc, &name)
            : ort->SessionGetOutputName(session, i, alloc, &name);
        if( st ){ ort->ReleaseStatus(st); continue; }
        if( name && strstr(name, zNeedle) ){
            size_t len = strlen(name);
            result = malloc(len + 1);
            memcpy(result, name, len + 1);
        }
        if( name ) alloc->Free(alloc, name);
    }
    return result;
}

viki_embedder *viki_embedder_open(const char *zModelDir){
    struct viki_embedder *e;
    char modelPath[4096], vocabPath[4096], manifestPath[4096];
    OrtStatus *st;
    OrtSessionOptions *opts;

    snprintf(modelPath, sizeof(modelPath), "%s/model.onnx", zModelDir);
    snprintf(vocabPath, sizeof(vocabPath), "%s/vocab.txt", zModelDir);
    snprintf(manifestPath, sizeof(manifestPath), "%s/viki-manifest.json", zModelDir);

    e = calloc(1, sizeof(*e));
    if( read_manifest(manifestPath, e->modelId, sizeof(e->modelId), &e->dim) != 0 ){
        fprintf(stderr, "viki embed: no usable model at '%s' (missing/bad viki-manifest.json) -- degraded BM25-only mode\n", zModelDir);
        free(e);
        return NULL;
    }

    e->vocab = viki_vocab_load(vocabPath);
    if( !e->vocab ){ free(e); return NULL; }

    e->ort = get_ort_api();
    if( check(e->ort, e->ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "viki", &e->env), "CreateEnv") ){
        viki_vocab_free(e->vocab); free(e); return NULL;
    }
    if( check(e->ort, e->ort->CreateSessionOptions(&opts), "CreateSessionOptions") ){
        viki_vocab_free(e->vocab); free(e); return NULL;
    }
    st = e->ort->CreateSession(e->env, modelPath, opts, &e->session);
    e->ort->ReleaseSessionOptions(opts);
    if( check(e->ort, st, "CreateSession") ){
        viki_vocab_free(e->vocab); free(e); return NULL;
    }

    if( check(e->ort, e->ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &e->memInfo), "CreateCpuMemoryInfo") ){
        viki_vocab_free(e->vocab); free(e); return NULL;
    }
    if( check(e->ort, e->ort->GetAllocatorWithDefaultOptions(&e->allocator), "GetAllocatorWithDefaultOptions") ){
        viki_vocab_free(e->vocab); free(e); return NULL;
    }

    e->inputIdsName  = find_name(e->ort, e->session, e->allocator, 1, "input_ids");
    e->attnMaskName  = find_name(e->ort, e->session, e->allocator, 1, "attention_mask");
    e->tokenTypeName = find_name(e->ort, e->session, e->allocator, 1, "token_type_ids");
    e->outputName    = find_name(e->ort, e->session, e->allocator, 0, "last_hidden_state");
    if( !e->outputName ){
        /* fall back to whatever the first output is named */
        e->outputName = find_name(e->ort, e->session, e->allocator, 0, "");
    }
    if( !e->inputIdsName || !e->attnMaskName || !e->outputName ){
        fprintf(stderr,
            "viki embed: model at '%s' doesn't expose the expected input/output names "
            "(input_ids=%s attention_mask=%s token_type_ids=%s output=%s)\n",
            zModelDir,
            e->inputIdsName ? e->inputIdsName : "?",
            e->attnMaskName ? e->attnMaskName : "?",
            e->tokenTypeName ? e->tokenTypeName : "(absent, will omit)",
            e->outputName ? e->outputName : "?");
        viki_embedder_close(e);
        return NULL;
    }

    return e;
}

void viki_embedder_close(viki_embedder *e){
    if( !e ) return;
    free(e->inputIdsName); free(e->attnMaskName); free(e->tokenTypeName); free(e->outputName);
    if( e->memInfo ) e->ort->ReleaseMemoryInfo(e->memInfo);
    if( e->session ) e->ort->ReleaseSession(e->session);
    if( e->env ) e->ort->ReleaseEnv(e->env);
    if( e->vocab ) viki_vocab_free(e->vocab);
    free(e);
}

const char *viki_embedder_model_id(const viki_embedder *e){ return e->modelId; }
int viki_embedder_dim(const viki_embedder *e){ return e->dim; }

int viki_embed(viki_embedder *e, const char *zText, float *outVec){
    const OrtApi *ort = e->ort;
    int64_t ids[VIKI_MAX_SEQ_LEN], mask[VIKI_MAX_SEQ_LEN], types[VIKI_MAX_SEQ_LEN];
    int idsI32[VIKI_MAX_SEQ_LEN];
    int nTok, i;
    int64_t shape[2];
    OrtValue *tIds = NULL, *tMask = NULL, *tTypes = NULL;
    OrtValue *inputs[3];
    const char *inputNames[3];
    int nInputs = 0;
    OrtValue *output = NULL;
    OrtStatus *st;
    float *hidden;
    int dim = e->dim;
    int seqLen;
    int rc = 1;

    nTok = viki_tokenize(e->vocab, zText, idsI32, VIKI_MAX_SEQ_LEN);
    if( nTok <= 0 ) return 1;
    seqLen = nTok;
    for( i = 0; i < nTok; i++ ){
        ids[i] = idsI32[i];
        mask[i] = 1;
        types[i] = 0;
    }
    shape[0] = 1; shape[1] = nTok;

    if( check(ort, ort->CreateTensorWithDataAsOrtValue(e->memInfo, ids, sizeof(int64_t)*(size_t)nTok,
                    shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &tIds), "CreateTensorWithDataAsOrtValue(input_ids)") )
        goto done;
    if( check(ort, ort->CreateTensorWithDataAsOrtValue(e->memInfo, mask, sizeof(int64_t)*(size_t)nTok,
                    shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &tMask), "CreateTensorWithDataAsOrtValue(attention_mask)") )
        goto done;

    inputNames[nInputs] = e->inputIdsName; inputs[nInputs] = tIds; nInputs++;
    inputNames[nInputs] = e->attnMaskName; inputs[nInputs] = tMask; nInputs++;
    if( e->tokenTypeName ){
        if( check(ort, ort->CreateTensorWithDataAsOrtValue(e->memInfo, types, sizeof(int64_t)*(size_t)nTok,
                        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &tTypes), "CreateTensorWithDataAsOrtValue(token_type_ids)") )
            goto done;
        inputNames[nInputs] = e->tokenTypeName; inputs[nInputs] = tTypes; nInputs++;
    }

    st = ort->Run(e->session, NULL, inputNames, (const OrtValue *const *)inputs, (size_t)nInputs,
                  (const char *const *)&e->outputName, 1, &output);
    if( check(ort, st, "Run") ) goto done;

    if( check(ort, ort->GetTensorMutableData(output, (void**)&hidden), "GetTensorMutableData") )
        goto done;

    /* hidden is [1, seqLen, dim] row-major float32: mean-pool over seqLen
    ** (attention_mask is all-1s here, no padding, so a plain mean over
    ** every token position is exactly the masked-mean sentence-
    ** transformers uses), then L2-normalize -- the standard
    ** sentence-transformers post-processing for this model family. */
    for( i = 0; i < dim; i++ ) outVec[i] = 0.0f;
    for( i = 0; i < seqLen; i++ ){
        int j;
        const float *row = hidden + (size_t)i * (size_t)dim;
        for( j = 0; j < dim; j++ ) outVec[j] += row[j];
    }
    {
        float norm = 0.0f;
        for( i = 0; i < dim; i++ ){ outVec[i] /= (float)seqLen; norm += outVec[i] * outVec[i]; }
        norm = norm > 0.0f ? (float)sqrt((double)norm) : 1.0f;
        for( i = 0; i < dim; i++ ) outVec[i] /= norm;
    }

    rc = 0;

done:
    if( tIds ) ort->ReleaseValue(tIds);
    if( tMask ) ort->ReleaseValue(tMask);
    if( tTypes ) ort->ReleaseValue(tTypes);
    if( output ) ort->ReleaseValue(output);
    return rc;
}
