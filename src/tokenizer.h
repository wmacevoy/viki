/*
** tokenizer.h -- BERT-style WordPiece tokenization for the pinned
** sentence-transformers/all-MiniLM-L6-v2 model (uncased BERT vocab).
**
** Scope/known limitations (see FINDINGS.md): correct for ASCII text --
** lowercasing, whitespace/punctuation splitting, greedy longest-match
** WordPiece against vocab.txt. Does NOT implement Unicode NFD accent
** stripping or CJK per-character splitting (both real parts of the
** reference BERT basic tokenizer); non-ASCII input degrades to more
** [UNK] tokens rather than being mis-tokenized silently wrong -- an
** accuracy gap, not a crash/corruption risk.
*/
#ifndef VIKI_TOKENIZER_H
#define VIKI_TOKENIZER_H

#include <stddef.h>

typedef struct {
    char **tokens;      /* vocab, indexed by id */
    int   *sortedIdx;   /* indices into tokens[], sorted lexicographically, for binary search */
    int    nTokens;
    int    clsId, sepId, padId, unkId;
} viki_vocab;

/* Loads vocab.txt (one token per line, line number == token id). Returns
** NULL on failure (prints diagnostics to stderr). */
viki_vocab *viki_vocab_load(const char *zVocabPath);
void viki_vocab_free(viki_vocab *v);

/* Tokenizes zText into WordPiece token ids, framed with [CLS] ... [SEP],
** truncated to maxLen ids total (including CLS/SEP). Writes up to maxLen
** ids to outIds, returns the actual count written (>= 2 if vocab has
** CLS/SEP, 0 on total failure). */
int viki_tokenize(const viki_vocab *v, const char *zText, int *outIds, int maxLen);

#endif
