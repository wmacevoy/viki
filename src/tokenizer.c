#include "tokenizer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple chained hash table: vocab.txt (~30k lines) isn't sorted
** alphabetically (it's sorted by id/frequency), so a hash table is
** simpler than maintaining a sorted index for binary search. */
typedef struct HNode {
    const char *tok;
    int id;
    struct HNode *next;
} HNode;

struct VikiVocabImpl {
    viki_vocab pub;
    HNode **buckets;
    int nBuckets;
};

static unsigned long djb2(const char *s){
    unsigned long h = 5381;
    int c;
    while( (c = (unsigned char)*s++) ) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

static int vocab_lookup(const void *vv, const char *tok){
    const struct VikiVocabImpl *impl = (const struct VikiVocabImpl*)vv;
    unsigned long h = djb2(tok) % (unsigned long)impl->nBuckets;
    HNode *n = impl->buckets[h];
    while( n ){
        if( strcmp(n->tok, tok) == 0 ) return n->id;
        n = n->next;
    }
    return -1;
}

viki_vocab *viki_vocab_load(const char *zVocabPath){
    FILE *f = fopen(zVocabPath, "r");
    struct VikiVocabImpl *impl;
    char line[512];
    int cap = 40000;

    if( !f ){
        fprintf(stderr, "viki: cannot open vocab file '%s'\n", zVocabPath);
        return NULL;
    }

    impl = calloc(1, sizeof(*impl));
    impl->pub.tokens = calloc((size_t)cap, sizeof(char*));
    impl->pub.nTokens = 0;
    impl->nBuckets = 65537; /* prime-ish, plenty for ~30k tokens */
    impl->buckets = calloc((size_t)impl->nBuckets, sizeof(HNode*));

    while( fgets(line, sizeof(line), f) ){
        size_t len = strlen(line);
        char *tok;
        HNode *node;
        unsigned long h;

        while( len > 0 && (line[len-1] == '\n' || line[len-1] == '\r') ) line[--len] = '\0';
        if( impl->pub.nTokens >= cap ){
            cap *= 2;
            impl->pub.tokens = realloc(impl->pub.tokens, (size_t)cap * sizeof(char*));
        }
        tok = strdup(line);
        impl->pub.tokens[impl->pub.nTokens] = tok;

        node = malloc(sizeof(HNode));
        node->tok = tok;
        node->id = impl->pub.nTokens;
        h = djb2(tok) % (unsigned long)impl->nBuckets;
        node->next = impl->buckets[h];
        impl->buckets[h] = node;

        impl->pub.nTokens++;
    }
    fclose(f);

    impl->pub.clsId = vocab_lookup(impl, "[CLS]");
    impl->pub.sepId = vocab_lookup(impl, "[SEP]");
    impl->pub.padId = vocab_lookup(impl, "[PAD]");
    impl->pub.unkId = vocab_lookup(impl, "[UNK]");
    if( impl->pub.clsId < 0 || impl->pub.sepId < 0 || impl->pub.unkId < 0 ){
        fprintf(stderr, "viki: vocab '%s' missing [CLS]/[SEP]/[UNK]\n", zVocabPath);
        viki_vocab_free(&impl->pub);
        return NULL;
    }

    return &impl->pub;
}

void viki_vocab_free(viki_vocab *v){
    struct VikiVocabImpl *impl;
    int i;
    if( !v ) return;
    impl = (struct VikiVocabImpl*)v;
    for( i = 0; i < impl->nBuckets; i++ ){
        HNode *n = impl->buckets[i];
        while( n ){ HNode *next = n->next; free(n); n = next; }
    }
    free(impl->buckets);
    for( i = 0; i < v->nTokens; i++ ) free(v->tokens[i]);
    free(v->tokens);
    free(impl);
}

/* ASCII-oriented basic tokenizer: lowercase, split on whitespace, split
** off punctuation as individual single-character tokens. See tokenizer.h
** for the documented Unicode/CJK limitation. */
static int is_ascii_punct(unsigned char c){
    return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) ||
           (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
}

#define MAX_BASIC_TOKENS 512
#define MAX_TOKEN_LEN 128

static int basic_tokenize(const char *zText, char basicToks[][MAX_TOKEN_LEN]){
    int n = 0;
    const char *p = zText;

    while( *p && n < MAX_BASIC_TOKENS ){
        unsigned char c = (unsigned char)*p;

        if( isspace(c) ){ p++; continue; }

        if( is_ascii_punct(c) ){
            basicToks[n][0] = (char)tolower(c);
            basicToks[n][1] = '\0';
            n++;
            p++;
            continue;
        }

        {
            int len = 0;
            while( *p && !isspace((unsigned char)*p) && !is_ascii_punct((unsigned char)*p)
                   && len < MAX_TOKEN_LEN - 1 ){
                basicToks[n][len++] = (char)tolower((unsigned char)*p);
                p++;
            }
            basicToks[n][len] = '\0';
            if( len > 0 ) n++;
            /* if a token got truncated at MAX_TOKEN_LEN, skip the rest of
            ** this run rather than splitting it oddly */
            while( *p && !isspace((unsigned char)*p) && !is_ascii_punct((unsigned char)*p) ) p++;
        }
    }
    return n;
}

/* Greedy longest-match-first WordPiece on one basic token. Appends
** resulting subword ids to outIds, advancing *pOutN (bounded by maxLen).
** Falls back to [UNK] if no split covers the whole token. */
static void wordpiece_one(const viki_vocab *v, const char *word, int *outIds, int *pOutN, int maxLen){
    size_t len = strlen(word);
    size_t start = 0;
    int piecesStart = *pOutN;
    char buf[MAX_TOKEN_LEN + 3]; /* room for "##" prefix */

    if( len == 0 ) return;

    while( start < len && *pOutN < maxLen ){
        size_t end = len;
        int found = -1;
        while( end > start ){
            size_t plen = end - start;
            if( start == 0 ){
                if( plen > MAX_TOKEN_LEN - 1 ) { end--; continue; }
                memcpy(buf, word + start, plen);
                buf[plen] = '\0';
            }else{
                if( plen > MAX_TOKEN_LEN - 3 ) { end--; continue; }
                buf[0] = '#'; buf[1] = '#';
                memcpy(buf + 2, word + start, plen);
                buf[plen + 2] = '\0';
            }
            found = vocab_lookup(v, buf);
            if( found >= 0 ) break;
            end--;
        }
        if( found < 0 ){
            /* No valid split for this word at all: whole word -> [UNK],
            ** discard any pieces already emitted for it. */
            *pOutN = piecesStart;
            if( *pOutN < maxLen ) outIds[(*pOutN)++] = v->unkId;
            return;
        }
        outIds[(*pOutN)++] = found;
        start = end;
    }
}

int viki_tokenize(const viki_vocab *v, const char *zText, int *outIds, int maxLen){
    static char basicToks[MAX_BASIC_TOKENS][MAX_TOKEN_LEN];
    int nBasic = basic_tokenize(zText, basicToks);
    int n = 0;
    int i;

    if( maxLen < 2 ) return 0;
    outIds[n++] = v->clsId;

    for( i = 0; i < nBasic && n < maxLen - 1; i++ ){
        wordpiece_one(v, basicToks[i], outIds, &n, maxLen - 1 /* leave room for [SEP] */);
    }

    outIds[n++] = v->sepId;
    return n;
}
