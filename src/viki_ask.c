#include "viki_ask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIKI_RRF_K 60.0      /* standard RRF damping constant */

/* How many FTS rows to fetch per candidate slot. LIMIT counts ROWS, but the
** candidate pool wants distinct chunks, and a cache holding E model epochs of
** the same content hands BM25 up to E rows for one chunk (see leg_hit below).
** Over-fetching by this factor and stopping once poolSize DISTINCT chunks are
** in hand keeps a bounded LIMIT -- which is what lets SQLite's sorter discard
** rows as it goes, since ORDER BY bm25() has to score and sort the whole MATCH
** set either way. A cache with more than this many epochs of one chunk simply
** yields fewer candidates than requested; it never yields wrong scores. */
#define VIKI_FTS_EPOCH_SLACK 4

/* FTS5's default MATCH syntax treats space-separated barewords as an
** IMPLICIT AND -- every term must appear, or the row doesn't match at
** all (not even a low score; excluded outright). That's wrong for a
** natural-language "ask viki" query: real queries contain stopwords and
** phrasing that won't literally appear in a matching chunk ("horses at
** the water trough" fails to match text that says "horses...near the
** water trough" purely because "at" isn't present). Discovered empirically
** while smoke-testing -- see FINDINGS.md.
**
** Fix: build an explicit OR-of-terms query. bm25() then ranks rows that
** match MORE/rarer terms higher, same as any standard BM25 search (Lucene/
** Elasticsearch default to this "should match any, score by how many"
** shape for exactly this reason) -- rather than requiring all terms.
** Each term is double-quoted as an FTS5 string literal (embedded quotes
** doubled) so punctuation in the raw query can't be misparsed as FTS5
** query syntax (NEAR, column filters, etc.). */
static char *build_or_query(const char *zQuery){
    size_t cap = strlen(zQuery) * 3 + 16; /* generous: worst case every char quoted */
    char *out = malloc(cap);
    size_t pos = 0;
    const char *p = zQuery;
    int first = 1;

    if( !out ) return NULL;
    out[0] = '\0';

    while( *p ){
        const char *start;
        size_t termlen;
        while( *p && (unsigned char)*p <= ' ' ) p++;
        if( !*p ) break;
        start = p;
        while( *p && (unsigned char)*p > ' ' ) p++;
        termlen = (size_t)(p - start);

        if( !first ){
            memcpy(out + pos, " OR ", 4);
            pos += 4;
        }
        first = 0;

        out[pos++] = '"';
        for( size_t i = 0; i < termlen; i++ ){
            if( start[i] == '"' ) out[pos++] = '"'; /* double embedded quotes */
            out[pos++] = start[i];
        }
        out[pos++] = '"';
        out[pos] = '\0';
    }

    if( first ){
        free(out);
        return NULL;
    }
    return out;
}

static viki_ask_result *find_or_add(viki_ask_result *pool, int *n, const char *hash, int chunk_ix){
    int i;
    for( i = 0; i < *n; i++ ){
        if( pool[i].chunk_ix == chunk_ix && strcmp(pool[i].hash, hash) == 0 ) return &pool[i];
    }
    if( *n >= VIKI_CANDIDATE_POOL ) return NULL;
    memset(&pool[*n], 0, sizeof(viki_ask_result));
    /* memset leaves cos at 0.0, which is a LEGAL cosine (orthogonal) and would
    ** read as a real, mediocre score. The sentinel says "never measured". */
    pool[*n].cos = VIKI_COS_NONE;
    strncpy(pool[*n].hash, hash, 64);
    pool[*n].chunk_ix = chunk_ix;
    (*n)++;
    return &pool[*n - 1];
}

/* How many characters of chunk_text the vector leg shows as an excerpt.
** Unlike the FTS leg it has no snippet() to build a match-centred window
** -- there is no match to centre on -- so it shows a raw PREFIX, and a
** prefix of a chunk is cut short whenever the chunk is longer than this.
** That cut is reported (VIKI_FRAG_CUT); it is not the same fact as the
** chunk being a slice of a longer document. */
#define VIKI_VEC_EXCERPT_CHARS 140

/* Records one hit from retrieval leg `leg` and advances *pRank to the rank
** this hit occupies within that leg.
**
** Ranks count DISTINCT (content_hash, chunk_ix) chunks, not rows: a chunk this
** leg has already scored neither scores again nor consumes a rank. That is the
** fix for a real double-counting bug. chunk_fts holds one row per
** (content_hash, model_id, chunk_ix), so re-indexing the same content under a
** second model_id -- the NORMAL steady state of D-11 cache sharing, where a
** no-model peer writes model_id='none' rows and a model-having peer writes its
** own model_id's rows into the same latest-wins uv blob -- puts two rows for
** one chunk in front of BM25. find_or_add() merges them into one candidate,
** correctly (chunk_text does not vary by model; only the embedding does), and
** the old code then added the BM25 leg's 1/(k+rank) contribution once per row.
** Measured on a two-epoch cache: rrf=0.0489 = 1/61 + 1/62 + 1/61 for a hit a
** single-epoch cache scores 0.0328 = 1/61 + 1/62 -- the keyword leg counted
** twice, and every score shifted by an amount that depends on how many epochs
** a peer happened to push rather than on the query.
**
** WHY dedupe here rather than adding "WHERE model_id = <asker's model>" to the
** FTS query: the asker's model_id is not a legal filter for the keyword leg.
** A chunk may exist ONLY under model_id='none' (indexed by a peer with no
** model), and `viki ask` with no model at all -- where there is no model_id to
** filter by in the first place -- must still search a cache that a model-having
** peer built, which is the entire point of `viki cache pull` (D-11/D-12).
** Filtering would drop exactly the rows the pull was for, converting a scoring
** bug into a silent recall bug. BM25 also does not depend on model_id at all:
** the only indexed column is chunk_text, identical across epochs, so the extra
** rows carry zero extra information and collapsing them loses nothing. The
** vector leg is the opposite case -- embeddings from two models are not
** comparable, so run_vector MUST filter by model_id, and does; viki_chunk's
** primary key then already makes its rows unique per (content_hash, chunk_ix).
** It still goes through this guard so "each leg contributes at most once" is
** enforced by the merge code itself rather than by trusting each leg's SQL.
**
** Both legs feed rows in their own best-first order (ORDER BY bm25() /
** cosine DESC), so the first row a leg reports for a chunk is also that
** chunk's best rank in that leg -- which is the one that gets scored.
**
** bTextIsPrefix says the caller's `text` is ALREADY a truncated prefix of
** chunk_text (the vector leg's substr()). It is passed in rather than
** guessed because SQL truncation is invisible from here: substr(x,1,140)
** of a 140-character chunk and of a 4000-character one look identical. */
static void leg_hit(viki_ask_result *pool, int *n, unsigned leg, int *pRank,
                     const char *hash, int chunk_ix, const char *text,
                     int bTextIsPrefix, double cos){
    viki_ask_result *c = find_or_add(pool, n, hash, chunk_ix);
    if( c && (c->legs & leg) ) return;   /* duplicate row for an already-scored chunk */
    (*pRank)++;
    /* c==NULL means the chunk is new and the candidate pool is full. The rank
    ** is still consumed: rank is this leg's position in ITS result list, and
    ** must not depend on how much room the pool had left. */
    if( !c ) return;
    c->legs |= leg;
    /* Recorded only by the vector leg; the FTS leg passes VIKI_COS_NONE
    ** because bm25() is not a cosine and the two must not be conflated. */
    if( leg == VIKI_LEG_VEC ) c->cos = cos;
    c->rrf += 1.0 / (VIKI_RRF_K + *pRank);
    if( text && !c->snippet[0] ){
        /* Two ways the string a surface will display is shorter than the
        ** excerpt the SQL produced -- the caller's substr(), and this
        ** fixed buffer -- and they are the same fact to a reader, so they
        ** set the same bit. The flag is set HERE, beside the copy, because
        ** it must describe the bytes actually stored: whichever leg
        ** reaches a chunk first supplies the excerpt, and only that leg's
        ** truncation is the one on show. */
        if( bTextIsPrefix || strlen(text) > sizeof(c->snippet) - 1 ){
            c->frag |= VIKI_FRAG_CUT;
        }
        strncpy(c->snippet, text, sizeof(c->snippet) - 1);
    }
}

static int cmp_result(const void *a, const void *b){
    double da = ((const viki_ask_result*)a)->rrf, db = ((const viki_ask_result*)b)->rrf;
    if( da > db ) return -1;
    if( da < db ) return 1;
    return 0;
}

static void run_fts(sqlite3 *db, const char *ftsQuery, int poolSize, viki_ask_result *pool, int *n){
    sqlite3_stmt *st;
    int rank = 0;

    /* The ' ... ' here is FTS5's own INTRA-CHUNK elision marker: it appears
    ** where snippet() dropped text from inside this chunk to fit its 24-token
    ** window. That is a different fact from the VIKI_FRAG_* fragment markers,
    ** which say the CHUNK is a slice of a longer document -- one is about
    ** what snippet() left out, the other about what `viki index` cut apart.
    ** Both can be true of one hit and both are then shown, in disjoint
    ** notations (see VIKI_MARK_HEAD in viki_ask.h); neither is ever
    ** substituted for the other, and this leg never sets VIKI_FRAG_CUT for
    ** elision snippet() has already marked itself. */
    if( sqlite3_prepare_v2(db,
            "SELECT content_hash, chunk_ix, snippet(chunk_fts, 0, '[', ']', ' ... ', 24) "
            "FROM chunk_fts WHERE chunk_fts MATCH ?1 ORDER BY bm25(chunk_fts) LIMIT ?2",
            -1, &st, NULL) != SQLITE_OK ){
        return;
    }
    sqlite3_bind_text(st, 1, ftsQuery, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, poolSize * VIKI_FTS_EPOCH_SLACK);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *snippet = (const char*)sqlite3_column_text(st, 2);
        leg_hit(pool, n, VIKI_LEG_FTS, &rank, hash, chunk_ix, snippet, 0, VIKI_COS_NONE);
        if( rank >= poolSize ) break; /* poolSize distinct chunks, not rows */
    }
    sqlite3_finalize(st);
}

static void run_vector(sqlite3 *db, const float *qvec, int dim, const char *modelId,
                        int poolSize, viki_ask_result *pool, int *n){
    sqlite3_stmt *st;
    int rank = 0;

    /* Column 3 asks the db whether the substr() in column 2 actually threw
    ** anything away, because the caller cannot tell afterwards. SQLite's
    ** substr()/length() both count CHARACTERS on a TEXT value, so the two
    ** are measured in the same unit and the comparison is exact rather than
    ** a byte-length approximation. */
    if( sqlite3_prepare_v2(db,
            /* Column 4 SELECTs the similarity the ORDER BY already computes.
            ** It was previously thrown away, which is why nothing downstream
            ** could tell a strong match from the best of a bad lot. */
            "SELECT content_hash, chunk_ix, substr(chunk_text,1,?5), length(chunk_text) > ?5, "
            "       ndvss_cosine_similarity_f(?2, embedding, ?3) "
            "FROM viki_chunk WHERE model_id=?1 AND embedding IS NOT NULL "
            "ORDER BY 5 DESC LIMIT ?4",
            -1, &st, NULL) != SQLITE_OK ){
        fprintf(stderr, "viki ask: vector query prepare failed: %s\n", sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_text(st, 1, modelId, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 2, qvec, (int)(sizeof(float) * (size_t)dim), SQLITE_STATIC);
    sqlite3_bind_int(st, 3, dim);
    sqlite3_bind_int(st, 4, poolSize);
    sqlite3_bind_int(st, 5, VIKI_VEC_EXCERPT_CHARS);

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *excerpt = (const char*)sqlite3_column_text(st, 2);
        int bCut = sqlite3_column_int(st, 3);
        double cos = sqlite3_column_double(st, 4);
        leg_hit(pool, n, VIKI_LEG_VEC, &rank, hash, chunk_ix, excerpt, bCut, cos);
    }
    sqlite3_finalize(st);
}

/* THE LITERAL LEG (QUEUE 42). Both existing legs are DENSITY-BIASED: bm25()
** rewards term frequency and the vector leg rewards topical concentration, so
** a document that treats a subject at length takes every slot and a document
** that states the same fact ONCE, in passing, loses. Passing mentions are
** exactly where a partially-applied update hides -- a number restated as
** background in a doc about something else. Measured on the corpus at 61d2b7e,
** where CLAUDE.md carried a stale "~0.5 s" that FINDINGS.md already refuted:
** two legs put all five top hits in FINDINGS.md and the stale claim at rank 6;
** adding this leg moves it to rank 2 and pulls the third site in at rank 4.
**
** It is NOT the raw query -- a natural-language sentence matches nothing
** literally. Only the query's HARD tokens are used: identifiers, acronyms,
** paths, versions, anything carrying a digit. Those are precisely what the
** porter stemmer mangles and what a 384-dim embedding averages away, and they
** are how two sites that phrase a claim completely differently (prose vs a
** markdown table) still refer to the same quantity BY NAME.
**
** IT ALWAYS FIRES. An earlier version gated it on the query containing a hard
** token, which was wrong twice: it is cheap enough not to need gating (one
** scan, the same order as the vector leg, which already scans everything --
** measured below), and the gate silently dropped the case it was best at. A
** rare ALL-LOWERCASE word -- `ecomment`, `sockdrawer`, `framed_next` without
** its underscore -- is a perfect literal anchor and no capital or digit
** announces it. So every token competes; hard ones merely get the slots first.
**
** Slot order is hard-tokens-first, then longest-remaining, because the term
** budget is small and a rare long word outranks a short common one as an
** anchor. Tokens under 3 characters and a compact stoplist are dropped: they
** appear in nearly every chunk, so they contribute no ordering information
** while consuming a slot and an instr() per row. */
#define VIKI_LIT_MAX_TERMS 8

/* Strips surrounding punctuation a writer puts AROUND a token rather than in
** it -- quotes, backticks, brackets, trailing commas -- while leaving the
** token's own punctuation alone, because `_` and `.` and `-` are most of what
** makes a token hard in the first place. */
static size_t lit_trim(const char *start, size_t len, const char **pOut){
    static const char *EDGE = "\"'`(),;:[]{}<>*!?";
    while( len && strchr(EDGE, start[0]) ){ start++; len--; }
    while( len && strchr(EDGE, start[len-1]) ) len--;
    /* A trailing '.' is sentence punctuation; an interior one is a filename. */
    while( len && start[len-1] == '.' ) len--;
    *pOut = start;
    return len;
}

/* A token is HARD if it carries identity rather than prose: a digit
** (3.53.4, D-11, FTS5), an internal structural character (viki_ask.c,
** sqlite3_blob_read, x'<64 hex>'), or two or more capitals (KDF, RRF,
** SQLCipher). An all-lowercase word with no punctuation is ordinary prose and
** is left to the two ranked legs, which handle it better than this one can. */
static int lit_is_hard(const char *z, size_t n){
    size_t i;
    int nUpper = 0, nDigit = 0, nStruct = 0, nAlpha = 0;
    if( n < 2 ) return 0;
    for( i = 0; i < n; i++ ){
        unsigned char c = (unsigned char)z[i];
        if( c >= 'A' && c <= 'Z' ) nUpper++;
        else if( c >= 'a' && c <= 'z' ) nAlpha++;
        else if( c >= '0' && c <= '9' ) nDigit++;
        else if( c=='_' || c=='-' || c=='/' || c=='.' || c==':' ) nStruct++;
    }
    if( nUpper + nAlpha + nDigit == 0 ) return 0;   /* pure punctuation */
    return nDigit > 0 || nStruct > 0 || nUpper >= 2;
}

/* Words that appear in so many chunks that matching them orders nothing.
** Deliberately SHORT: this is not a linguistic stoplist, it is a list of
** terms whose literal presence carries no ranking information on any corpus,
** and anything genuinely rare must survive it. */
static int lit_is_stop(const char *z, size_t n){
    static const char *azStop[] = {
        "the","and","for","that","with","this","from","are","was","not","but",
        "its","one","all","any","can","will","has","have","had","then","than",
        "when","what","where","who","how","some","more","most","other","into",
        "out","over","under","about","only","same","such","each","per","see",
        "use","used","using","been","being","does","did","here","there","their",
        "would","could","should","must","may","might","you","your","they","them",
        "does","doing","why","which","while","also","just","like","very","much",
        "cost","costs","get","gets", NULL
    };
    int i;
    for( i = 0; azStop[i]; i++ ){
        if( strlen(azStop[i]) == n && strncmp(azStop[i], z, n) == 0 ) return 1;
    }
    return 0;
}

/* Fills azOut with lowercased copies of the query's most anchoring tokens.
** Returns the count; caller frees each entry. Lowercased here rather than in
** SQL so the per-row cost is one instr() against an already-lowered needle.
**
** Two passes so the small term budget goes to the best anchors: hard tokens
** first, then the longest of what is left. */
static int lit_collect(const char *zQuery, char **azOut, int n, int bHardOnly,
                        size_t minLen){
    const char *p = zQuery;
    while( *p && n < VIKI_LIT_MAX_TERMS ){
        const char *start, *tok;
        size_t len, tlen, i;
        int dup = 0;
        while( *p && (unsigned char)*p <= ' ' ) p++;
        if( !*p ) break;
        start = p;
        while( *p && (unsigned char)*p > ' ' ) p++;
        len = (size_t)(p - start);
        tlen = lit_trim(start, len, &tok);
        if( tlen < 3 ) continue;
        if( bHardOnly ){
            if( !lit_is_hard(tok, tlen) ) continue;
        }else{
            if( tlen < minLen ) continue;
            if( lit_is_stop(tok, tlen) ) continue;
        }
        /* Hand-rolled rather than strncasecmp() so this file needs no
        ** <strings.h>: viki builds under MSYS as well as POSIX, and the
        ** stored term is already lowercased, so only the token needs folding. */
        for( i = 0; i < (size_t)n; i++ ){
            size_t k;
            if( strlen(azOut[i]) != tlen ) continue;
            for( k = 0; k < tlen; k++ ){
                unsigned char c = (unsigned char)tok[k];
                if( c >= 'A' && c <= 'Z' ) c = (unsigned char)(c - 'A' + 'a');
                if( azOut[i][k] != (char)c ) break;
            }
            if( k == tlen ){ dup = 1; break; }
        }
        if( dup ) continue;
        azOut[n] = malloc(tlen + 1);
        if( !azOut[n] ) break;
        for( i = 0; i < tlen; i++ ){
            unsigned char c = (unsigned char)tok[i];
            azOut[n][i] = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        }
        azOut[n][tlen] = '\0';
        n++;
    }
    return n;
}

static int select_literal_terms(const char *zQuery, char **azOut){
    int n = lit_collect(zQuery, azOut, 0, 1, 0);   /* pass 1: hard tokens */
    size_t minLen;
    /* pass 2: fill remaining slots longest-first, without sorting the query */
    for( minLen = 12; minLen >= 3 && n < VIKI_LIT_MAX_TERMS; minLen-- ){
        n = lit_collect(zQuery, azOut, n, 0, minLen);
    }
    return n;
}

/* Counts how many chunks each term occurs in, so run_literal can weight a
** rare term above a common one. ONE scan with nTerm aggregates, not nTerm
** scans. A term matching nothing, or matching everything, is given weight 0:
** neither orders anything, and dropping them here means run_literal's
** expression never has to special-case them.
**
** This exists because counting MATCHED TERMS alone is not enough, which the
** probe caught. For "how does framed_next parse the counted framing" over a
** corpus with one long document about framing: every chunk of that document
** matches `framing`, `counted` and `parse` for a score of 3, and the single
** chunk that actually contains `framed_next` also scores 3 -- so the tie
** breaks on volume and the unique identifier is buried by the very document
** it should have beaten. Weighting by 1/df puts a term occurring in one
** chunk an order of magnitude above one occurring in fifteen. */
static void literal_weights(sqlite3 *db, char **azTerm, int nTerm, double *aW){
    sqlite3_stmt *st = NULL;
    char *zSql = sqlite3_mprintf("SELECT count(*)");
    int i, nTotal = 0;

    for( i = 0; i < nTerm && zSql; i++ ){
        zSql = sqlite3_mprintf("%z, sum(instr(lower(chunk_text), ?%d) > 0)", zSql, i + 1);
    }
    if( zSql ) zSql = sqlite3_mprintf("%z FROM viki_chunk", zSql);
    for( i = 0; i < nTerm; i++ ) aW[i] = 0.0;
    if( !zSql ) return;

    if( sqlite3_prepare_v2(db, zSql, -1, &st, NULL) == SQLITE_OK ){
        for( i = 0; i < nTerm; i++ ) sqlite3_bind_text(st, i + 1, azTerm[i], -1, SQLITE_STATIC);
        if( sqlite3_step(st) == SQLITE_ROW ){
            nTotal = sqlite3_column_int(st, 0);
            for( i = 0; i < nTerm; i++ ){
                int df = sqlite3_column_int(st, i + 1);
                /* 0 orders nothing; nTotal orders nothing either -- a term in
                ** every chunk separates no two chunks. */
                aW[i] = (df > 0 && df < nTotal) ? 1.0 / (double)df : 0.0;
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_free(zSql);
    (void)nTotal;
}

/* Ranks by the RARITY-WEIGHTED sum of the terms a chunk contains, so a chunk
** naming a one-of-a-kind identifier outranks one that merely shares the
** query's common words. Ties break on (content_hash, chunk_ix) so a run is
** reproducible -- three test files parse this ordering by position.
**
** Over-fetches by the same epoch slack as the FTS leg and for the same reason:
** viki_chunk holds one row per (content_hash, model_id, chunk_ix), so a cache
** carrying two model epochs of one chunk offers this leg two rows for it.
** leg_hit()/find_or_add() merge them, but the LIMIT counts rows. */
static void run_literal(sqlite3 *db, char **azTerm, int nTerm, int poolSize,
                         viki_ask_result *pool, int *n){
    sqlite3_stmt *st = NULL;
    char *zSql = NULL;
    int rank = 0, i;

    double aW[VIKI_LIT_MAX_TERMS];

    if( nTerm < 1 ) return;
    literal_weights(db, azTerm, nTerm, aW);

    zSql = sqlite3_mprintf(
        "SELECT content_hash, chunk_ix, excerpt, cut FROM ("
        "  SELECT content_hash, chunk_ix, substr(chunk_text,1,%d) AS excerpt,"
        "         length(chunk_text) > %d AS cut, (0.0",
        VIKI_VEC_EXCERPT_CHARS, VIKI_VEC_EXCERPT_CHARS);
    for( i = 0; i < nTerm && zSql; i++ ){
        /* Weight inlined as a literal rather than bound: it is a double this
        ** code just computed, not user input, and it keeps the bind indices
        ** aligned one-to-one with the terms. */
        if( aW[i] <= 0.0 ) continue;
        zSql = sqlite3_mprintf("%z + (instr(lower(chunk_text), ?%d) > 0) * %.17g",
                                zSql, i + 1, aW[i]);
    }
    if( zSql ){
        char *zNext = sqlite3_mprintf(
            "%z) AS hits FROM viki_chunk) WHERE hits > 0"
            " ORDER BY hits DESC, content_hash, chunk_ix LIMIT %d",
            zSql, poolSize * VIKI_FTS_EPOCH_SLACK);
        zSql = zNext;
    }
    if( !zSql ) return;

    if( sqlite3_prepare_v2(db, zSql, -1, &st, NULL) != SQLITE_OK ){
        sqlite3_free(zSql);
        return;
    }
    sqlite3_free(zSql);
    for( i = 0; i < nTerm; i++ ){
        sqlite3_bind_text(st, i + 1, azTerm[i], -1, SQLITE_STATIC);
    }

    while( sqlite3_step(st) == SQLITE_ROW ){
        const char *hash = (const char*)sqlite3_column_text(st, 0);
        int chunk_ix = sqlite3_column_int(st, 1);
        const char *excerpt = (const char*)sqlite3_column_text(st, 2);
        int bCut = sqlite3_column_int(st, 3);
        leg_hit(pool, n, VIKI_LEG_LIT, &rank, hash, chunk_ix, excerpt, bCut, VIKI_COS_NONE);
        if( rank >= poolSize ) break;   /* poolSize distinct chunks, not rows */
    }
    sqlite3_finalize(st);
}

static void fill_sources(sqlite3 *db, viki_ask_result *pool, int n){
    int i;
    for( i = 0; i < n; i++ ){
        sqlite3_stmt *stPath;
        strcpy(pool[i].source, "(source path unknown)");
        if( sqlite3_prepare_v2(db, "SELECT path FROM viki_source WHERE content_hash=?1 LIMIT 1",
                -1, &stPath, NULL) == SQLITE_OK ){
            sqlite3_bind_text(stPath, 1, pool[i].hash, -1, SQLITE_STATIC);
            if( sqlite3_step(stPath) == SQLITE_ROW ){
                const char *path = (const char*)sqlite3_column_text(stPath, 0);
                if( path ) strncpy(pool[i].source, path, sizeof(pool[i].source) - 1);
            }
            sqlite3_finalize(stPath);
        }
    }
}

/* Decides, for each candidate, whether it is a FRAGMENT of a longer
** document, and records the document's chunk count. Display-side only:
** nothing here writes, and chunk_text is untouched.
**
** The rule is positional. A chunk is a fragment at its HEAD when
** chunk_ix > 0 -- that needs no query at all -- and at its TAIL when some
** later chunk of the same content exists.
**
** WHY max() IS TAKEN OVER EVERY model_id RATHER THAN THE ASKER'S: a
** viki_ask_result has no model_id to filter by, and must not acquire one.
** The FTS leg deliberately does not filter by model_id (see leg_hit), so
** a hit can come from a chunk that exists only under a peer's epoch. In
** every cache anyone has built, the count is the same either way: chunk
** boundaries are a function of the text, not of the model, so two epochs
** of one content_hash hold the same chunk_ix set. It could only diverge
** if the chunker itself changed -- which is an EPOCH BUMP by D-11, not a
** local tweak -- and then max() over all epochs over-marks (claims text
** follows when this epoch ended here) rather than under-marks. That is
** the right way round to be wrong: a spurious "there is more" costs a
** reader nothing, while a missing one is the provenance defect this whole
** mechanism exists to fix. Same reasoning for an extent we cannot read at
** all (a chunk_fts row whose viki_chunk row is gone, or a failed
** prepare): report chunk_count 0 for "unknown" and mark the tail, because
** "I could not prove this chunk ends the document" must never render as
** "this chunk ends the document". */
static void fill_fragment_flags(sqlite3 *db, viki_ask_result *pool, int n){
    sqlite3_stmt *st = NULL;
    int i;

    if( sqlite3_prepare_v2(db, "SELECT max(chunk_ix) FROM viki_chunk WHERE content_hash=?1",
                           -1, &st, NULL) != SQLITE_OK ){
        st = NULL;
    }
    for( i = 0; i < n; i++ ){
        int maxIx = -1;   /* -1 == extent unknown */
        if( st ){
            sqlite3_reset(st);
            sqlite3_bind_text(st, 1, pool[i].hash, -1, SQLITE_STATIC);
            if( sqlite3_step(st) == SQLITE_ROW
             && sqlite3_column_type(st, 0) != SQLITE_NULL ){
                maxIx = sqlite3_column_int(st, 0);
            }
        }
        if( pool[i].chunk_ix > 0 ) pool[i].frag |= VIKI_FRAG_HEAD;
        if( maxIx < 0 ){
            pool[i].chunk_count = 0;
            pool[i].frag |= VIKI_FRAG_TAIL;
        }else{
            pool[i].chunk_count = maxIx + 1;
            if( pool[i].chunk_ix < maxIx ) pool[i].frag |= VIKI_FRAG_TAIL;
        }
    }
    if( st ) sqlite3_finalize(st);
}

void viki_ask_defaults(viki_ask_opts *o){
    o->minCos = VIKI_MIN_COS_DEFAULT;
}

int viki_ask_query(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb,
                    viki_ask_result *results, int maxResults){
    viki_ask_opts o;
    viki_ask_defaults(&o);
    return viki_ask_query_opts(db, zQuery, topK, emb, results, maxResults, &o, NULL);
}

int viki_ask_query_opts(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb,
                    viki_ask_result *results, int maxResults,
                    const viki_ask_opts *opts, viki_ask_info *pInfo){
    char *ftsQuery;
    viki_ask_result pool[VIKI_CANDIDATE_POOL];
    int n = 0;
    int poolSize = topK * 4 < VIKI_CANDIDATE_POOL ? topK * 4 : VIKI_CANDIDATE_POOL;
    int nOut;

    if( poolSize < 1 ) poolSize = 1;

    ftsQuery = build_or_query(zQuery);
    if( ftsQuery ){
        run_fts(db, ftsQuery, poolSize, pool, &n);
        free(ftsQuery);
    }

    {
        char *azTerm[VIKI_LIT_MAX_TERMS];
        int nTerm = select_literal_terms(zQuery, azTerm);
        int i;
        run_literal(db, azTerm, nTerm, poolSize, pool, &n);
        for( i = 0; i < nTerm; i++ ) free(azTerm[i]);
    }

    if( emb ){
        float *qvec = malloc(sizeof(float) * (size_t)viki_embedder_dim(emb));
        if( viki_embed(emb, zQuery, qvec) == 0 ){
            run_vector(db, qvec, viki_embedder_dim(emb), viki_embedder_model_id(emb), poolSize, pool, &n);
        }
        free(qvec);
    }

    fill_sources(db, pool, n);
    fill_fragment_flags(db, pool, n);
    qsort(pool, (size_t)n, sizeof(viki_ask_result), cmp_result);

    nOut = n < maxResults ? n : maxResults;
    nOut = nOut < topK ? nOut : topK;

    {
        double best = VIKI_COS_NONE;
        int i;
        for( i = 0; i < n; i++ ) if( pool[i].cos > best ) best = pool[i].cos;
        if( pInfo ){
            pInfo->bestCos = best;
            pInfo->lowConfidence = 0;
            pInfo->nSuppressed = 0;
        }
        /* Suppress the WHOLE set, not individual hits. The question being
        ** answered is "is there anything here worth acting on", and a set
        ** whose best member is weak has no strong members by definition.
        ** Only applies when the vector leg actually ran: with no model there
        ** is no comparable absolute signal, and bm25()'s magnitude depends on
        ** corpus statistics, so a floor on it would mean something different
        ** on every repository. */
        if( best > VIKI_COS_NONE && opts && opts->minCos > 0.0 && best < opts->minCos ){
            if( pInfo ){ pInfo->lowConfidence = 1; pInfo->nSuppressed = nOut; }
            return 0;
        }
    }

    if( nOut > 0 ) memcpy(results, pool, sizeof(viki_ask_result) * (size_t)nOut);
    return nOut;
}

/* Strips leading and trailing whitespace from an excerpt, in place, for
** display only. Both ends acquire whitespace that means nothing here: a
** chunk_text slice ends with the newline that ended its last line, and
** FTS5's snippet() frames its elision marker with spaces (' ... '), so
** the excerpt routinely arrives with a space at each end. Left alone,
** the markers drift off the text they qualify -- the tail one onto a
** line of its own, where it reads as a separate remark rather than as
** "and the document goes on from here".
**
** Whitespace is the only thing this may ever remove, and only from a
** rendering that already reflows the excerpt (raw newlines, only the
** first line indented). Anyone who needs the chunk byte-for-byte asks
** /api/chunk. It operates on viki_cmd_ask's own copy of the results, so
** the cache is untouched and /api/ask still reports the excerpt exactly
** as retrieved. */
static void trim_excerpt(char *z){
    size_t n = strlen(z);
    size_t i = 0;
    while( n > 0 && (unsigned char)z[n - 1] <= ' ' ) z[--n] = '\0';
    while( z[i] && (unsigned char)z[i] <= ' ' ) i++;
    if( i ) memmove(z, z + i, n - i + 1);
}

int viki_cmd_ask(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb){
    return viki_cmd_ask_opts(db, zQuery, topK, emb, NULL);
}

int viki_cmd_ask_opts(sqlite3 *db, const char *zQuery, int topK, viki_embedder *emb,
                      const viki_ask_opts *optsIn){
    viki_ask_result results[VIKI_CANDIDATE_POOL];
    viki_ask_opts opts;
    viki_ask_info info;
    int n, i;

    if( optsIn ) opts = *optsIn; else viki_ask_defaults(&opts);

    if( emb ){
        fprintf(stderr, "viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=%s)\n\n",
                viki_embedder_model_id(emb));
    }else{
        fprintf(stderr,
            "viki ask: degraded mode (BM25 keyword search only -- no embedding "
            "model available; see FINDINGS.md / VIKI_DESIGN.md rung 1/2)\n\n");
    }

    n = viki_ask_query_opts(db, zQuery, topK, emb, results, VIKI_CANDIDATE_POOL, &opts, &info);

    for( i = 0; i < n; i++ ){
        /* content_hash#chunk_ix FIRST, source path last. KICKOFF.md
        ** deliverable 2 asks for "results with source content_hash, snippet,
        ** score", and VIKI_DESIGN.md's agent contract has agents citing
        ** content_hashes so answers link back to source artifacts -- until now
        ** the CLI printed neither, only the viki_source path, which is a
        ** best-effort join that prints "(source path unknown)" for any chunk
        ** whose path row is stale/missing. Same value and same semantics as
        ** /api/ask's "hash" field, and `<hash>#<ix>` are exactly the two
        ** parameters /api/chunk?hash=&ix= wants, so a hit named on the CLI can
        ** be fetched in full from `viki serve` without a translation step. The
        ** ragged, possibly-empty, possibly-space-containing source path goes
        ** last so it cannot shift any field a script reads by position.
        **
        ** The FRAGMENT markers go on the EXCERPT line, never the header
        ** line. The header is a citation -- `<hash>#<ix>` is exactly what
        ** /api/chunk?hash=&ix= takes, and three test files parse the line
        ** by position -- while the excerpt is the thing that misreads as a
        ** complete text and therefore the thing that has to say otherwise.
        ** Order after the excerpt is inner-to-outer: VIKI_MARK_CUT is
        ** about this excerpt, VIKI_MARK_TAIL about the document around the
        ** chunk. Both can appear; they are not alternatives. */
        trim_excerpt(results[i].snippet);
        printf("[%d] rrf=%.4f  %s#%d  %s\n    %s%s%s%s\n\n",
               i + 1, results[i].rrf, results[i].hash, results[i].chunk_ix,
               results[i].source,
               (results[i].frag & VIKI_FRAG_HEAD) ? VIKI_MARK_HEAD " " : "",
               results[i].snippet,
               (results[i].frag & VIKI_FRAG_CUT)  ? " " VIKI_MARK_CUT  : "",
               (results[i].frag & VIKI_FRAG_TAIL) ? " " VIKI_MARK_TAIL : "");
    }
    if( n == 0 ){
        /* Two different answers, and an assistant must not confuse them:
        ** nothing was retrieved at all, versus something was retrieved and
        ** was not good enough to act on. */
        if( info.lowConfidence ){
            fprintf(stderr,
                "(no confident match -- best cosine %.4f is below the %.2f floor; "
                "%d weaker hit(s) withheld. Re-run with --min-cos 0 to see them.)\n",
                info.bestCos, opts.minCos, info.nSuppressed);
        }else{
            fprintf(stderr, "(no matches)\n");
        }
    }else if( info.bestCos > VIKI_COS_NONE ){
        fprintf(stderr, "viki ask: best cosine %.4f (floor %.2f)\n", info.bestCos, opts.minCos);
    }
    return 0;
}
