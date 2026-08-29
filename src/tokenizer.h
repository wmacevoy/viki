/*
** tokenizer.h -- BERT-style WordPiece tokenization for the pinned
** sentence-transformers/all-MiniLM-L6-v2 model (uncased BERT vocab).
**
** THIS TOKENIZER IS NOT FREE TO IMPROVE. Its output ids are the model's
** input, so "better" here can only ever mean "closer to the tokenizer
** all-MiniLM-L6-v2 was trained on" -- HuggingFace's BertTokenizer, i.e.
** BasicTokenizer(do_lower_case=True) followed by WordpieceTokenizer. A
** token stream the model never saw in training produces a worse embedding,
** not a better one, however sensible the scheme looks in isolation.
**
** ---------------------------------------------------------------------
** DEFAULT BUILD (VIKI_TOKENIZER_CONFORMANT undefined): ASCII-scoped
** ---------------------------------------------------------------------
** Lowercasing, whitespace/punctuation splitting and greedy longest-match
** WordPiece against vocab.txt. On pure-ASCII input this is not merely
** "good enough", it is exactly conformant: measured over 107,732 tokens
** (AGENTS.md + FINDINGS.md + every .c and .h under src/) all 183 chunks are
** id-identical to reference BERT, at a 0.000% [UNK] rate.
**
** It has three gaps on non-ASCII input, and the third one is the reason
** the paragraph that used to sit here was WRONG. It claimed non-ASCII
** "degrades to more [UNK] tokens rather than being mis-tokenized silently
** wrong". Measured false:
**
**   1. No Unicode NFD accent stripping. Honest degradation, as claimed:
**      "cafe<ACUTE-E>" -> [UNK] where reference gives "cafe". 31.3% of
**      words in accented prose are lost this way, and it is total -- the
**      vocab contains ZERO precomposed accented codepoints, so accent
**      stripping is the ONLY path by which this model can ever match an
**      accented word. Note the internal disagreement it creates: FTS5's
**      unicode61 strips diacritics by default, so viki's keyword leg
**      indexes "cafe<ACUTE-E>" as "cafe" and matches it while the vector
**      leg cannot see it at all. The two legs disagree on accented text.
**   2. No CJK per-character splitting. Worth less than it sounds: this
**      vocab holds only 244 bare CJK characters, so reference BERT [UNK]s
**      most Chinese/Japanese too. It closes a silent-wrongness path (see
**      3), not a recall gap.
**   3. NON-ASCII PUNCTUATION IS NOT TREATED AS PUNCTUATION, and this one
**      is silently wrong rather than loudly absent. is_ascii_punct()
**      covers the four ASCII ranges only, while reference punctuation is
**      also every Unicode P* codepoint. A P* character therefore stays
**      glued to its neighbours -- and because the vocab carries "##"
**      continuations for these characters, greedy WordPiece SUCCEEDS with
**      the wrong pieces instead of failing to [UNK]:
**
**          "decision<EM-DASH>recorded"
**              viki: decision ##<EM-DASH> ##re ##cor ##ded
**              ref : decision   <EM-DASH>   recorded
**
**      Note the contagion: the entire FOLLOWING word turns into "##"
**      continuations, so one smart quote corrupts two words. 16 of 27
**      tested characters produce different real ids this way.
**
** All three are measured at approximately zero cost on THIS repo (14
** divergent words in 37,363 across every .md file; the whole git log is
** 100.00% conformant) because the house style writes "--" and stays
** ASCII. They matter for pasted chat transcripts, word-processor text and
** non-English content.
**
** ---------------------------------------------------------------------
** VIKI_TOKENIZER_CONFORMANT: the fixes, landed dark, DEFAULT OFF
** ---------------------------------------------------------------------
** Defining VIKI_TOKENIZER_CONFORMANT swaps basic_tokenize() for a
** UTF-8-aware transcription of the reference pipeline -- _clean_text,
** _tokenize_chinese_chars, whitespace split, lower, _run_strip_accents,
** _run_split_on_punc -- and adds WordpieceTokenizer's
** max_input_chars_per_word=100 rule.
**
** *** DO NOT FLIP THIS FLAG IN A RELEASE BUILD ON ITS OWN. ***
** Turning it on changes the token ids for existing content, therefore the
** embedding of that content, therefore what EVERY PEER computes for the
** same (content_hash, model_id, chunk_params) -- which D-11 says is one
** shared value. That is a viki-manifest EPOCH BUMP plus a new model_id
** and a fleet-wide re-embed, not a local build option. It should ride
** along with the chunking epoch bump, never spend one of its own: on this
** corpus its measured retrieval gain is zero, so it cannot pay for a
** fleet-wide re-embed by itself. What it buys is that the bump, when it
** comes for a reason that CAN pay for it, is pre-written and proven.
**
** With the flag OFF nothing above changes: the legacy basic_tokenize() is
** preserved verbatim and is the only one compiled, so "flag off is
** byte-identical" holds by construction rather than by test. It is tested
** anyway -- see test/tokenizer-conformance.sh.
**
** SCOPE OF THE CONFORMANT PATH, so nobody has to re-measure it: complete
** over Unicode for punctuation (P*), token-breaking space (Zs plus Zl/Zp
** -- see cp_is_space), control (Cc/Cf/Cs/Co), combining-mark dropping
** (Mn), simple lowercase, the reference's eight CJK ranges, and canonical
** decomposition for Latin, Greek, Cyrillic, kana and Hangul. Verified by
** sweeping all 1,112,032 non-surrogate codepoints through this file and
** diffing the ids against the reference: 0 divergences outside the two
** deliberate gaps below, both of which fail loud ([UNK]) rather than
** silently wrong.
**   - CJK Compatibility Ideographs (U+F900..U+FAFF, U+2F800..U+2FA1F)
**     keep their compatibility codepoint instead of canonically
**     decomposing to the unified one. 1,002 codepoints, of which only 86
**     actually yield a different token id -- for the rest both sides
**     produce [UNK], because this vocab holds just 244 CJK characters.
**   - category Cn (unassigned) is not treated as a control character:
**     825,303 codepoints that the reference silently DELETES become
**     [UNK] here. Chasing this exactly would mean baking one UCD
**     version's assigned-set into the tokenizer, and which version is
**     "right" is whichever one the model was trained under -- unknowable
**     and unstable. No assigned text is affected.
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
/* The same, from bytes. A filesystem is not available in wasm or inside a
** sandboxed robot, so loading by path is the special case. */
viki_vocab *viki_vocab_load_mem(const char *zText, size_t nText);
void viki_vocab_free(viki_vocab *v);

/* Tokenizes zText into WordPiece token ids, framed with [CLS] ... [SEP],
** truncated to maxLen ids total (including CLS/SEP). Writes up to maxLen
** ids to outIds, returns the actual count written (>= 2 if vocab has
** CLS/SEP, 0 on total failure). */
int viki_tokenize(const viki_vocab *v, const char *zText, int *outIds, int maxLen);

#endif
