/*
** viki_core.h -- the whole public surface.
**
** THE CONTRACT IS SQLITE. Not Fossil, not a filesystem, not a network. The
** caller hands us an open sqlite3* and gets assertions, retrieval and merge
** back. That one decision carries the encryption key, the VFS, the file
** location and the platform's storage rules out of scope at once -- the host
** opened the connection, so the host already chose all four.
**
** FOSSIL IS ANCESTRY, NOT A DEPENDENCY: content addressing, grow-only rows,
** union-is-merge, resolution at read time. None of its code.
*/
#ifndef VIKI_CORE_H
#define VIKI_CORE_H

#include <stddef.h>
#include "sqlite3.h"
#include "retain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIKI_ID_HEX 64            /* sha256 hex, without the NUL */

/* A contract that does not state how it fails has not stated anything.
** VIKI_ENOCTX is the one that matters: viki_note() with nothing retained is
** an ERROR, never a silent no-op -- a memory that quietly drops what it was
** told is worse than one that is absent. */
typedef enum {
    VIKI_OK = 0,
    VIKI_ENOCTX, VIKI_ESQL, VIKI_ENOMEM, VIKI_EINVAL, VIKI_ENOTFOUND,
    VIKI_EBUSY            /* a listener tried to mutate; see viki_watch */
} VikiStatus;
const char *viki_errmsg(void);

/* ---- runtime type (oopc: three words per class, zero per object) ----- */
typedef struct VikiTypeStruct VikiType;
struct VikiTypeStruct {
    const char *zName;
    const VikiType *pParent;
    size_t szObject;
};
int viki_isa(const VikiType *pMe, const VikiType *pOf);

/* ---- retained context: TWO types, TWO stacks, TWO lifetimes ----------
** Not one context struct. A caller that only wants viki_note() supplies only
** a store and is never asked to produce an embedder it will not use.
**
** DEGRADED MODE IS THE ABSENCE OF A TYPE, not a NULL to remember: with no
** VikiEmbed retained, viki_ask() runs keyword + literal and says so. */
/* A DIARY is one store. A CONTEXT is a SET of them, retained as one thing --
** not a stack of separate retains, because an agent uses all of them at once
** rather than having the inner shadow the outer.
**
**   core      the shared substrate: the model, the vocabulary, whatever the
**             tribe agrees on. Usually read-only to this peer.
**   private   yours. WRITES GO HERE unless a diary is named.
**   opened    every diary in play, INCLUDING core and private.
**
** ASK IS PER DIARY, and that is a decision rather than a limitation. Fusing
** results across diaries would blend a private answer into a tribe answer and
** lose which said it -- and "which diary told me this" is exactly what a
** memory must not lose. It also lets the rules differ per diary: a different
** chunking, a different model, a different policy, all of which stop making
** sense the moment one pool is ranked. */
typedef struct {
    const char *zName;
    sqlite3    *db;
    unsigned    mFlags;         /* VIKI_D_RDONLY */
} VikiDiary;
#define VIKI_D_RDONLY 0x01

#define VIKI_MAX_DIARIES 8
typedef struct {
    VikiDiary *pCore;                       /* may be the same as pPrivate  */
    VikiDiary *pPrivate;                    /* the write target             */
    VikiDiary *apOpen[VIKI_MAX_DIARIES];    /* includes core and private    */
    int        nOpen;
} VikiDiaries;

/* The common case: one diary, which is both core and private. */
void viki_diaries_one(VikiDiaries *pOut, VikiDiary *pOne);
/* Adds a diary to `opened`. Returns VIKI_EINVAL past VIKI_MAX_DIARIES. */
VikiStatus viki_diaries_add(VikiDiaries *p, VikiDiary *pD);
/* The open diary of that name, or NULL. NULL name means the private one. */
VikiDiary *viki_diary(const char *zName);

/* THE EMBEDDER IS A HOST CALLBACK, and that follows from "no filesystem":
** a model is a file, and loading it is the host's business. wasm supplies
** onnxruntime-web, iOS supplies CoreML, the CLI supplies ONNX Runtime; core
** never opens anything. `zEpoch` is the cache epoch -- (model, chunking) --
** and vectors from different epochs never meet. */
typedef int (*viki_embed_fn)(void *pApp, const char *zText,
                             float *aOut, int nDim);
typedef struct {
    viki_embed_fn xEmbed;
    void         *pApp;
    int           nDim;
    const char   *zModel;   /* THE MODEL. Chunking is no longer folded in --
                            ** the chunk's extent is in its key, so it cannot
                            ** collide, and D-11's model_id means the model. */
} VikiEmbed;

/* WHICH POLICY DREW THE LINES. Passing a different one to viki_reindex() adds
** a SECOND set of ranges over the same assertions without disturbing the
** first, and viki_ask() searches all of them -- a fine chunking finds the
** precise sentence, a coarse one keeps enough context to be judged. Overlap
** costs nothing now: overlapping ranges are just ranges.
**
** `zName` is provenance, not identity. Two policies that happen to produce
** the same boundaries produce the same rows, which is correct. */
typedef struct {
    const char *zName;
    int         nLines;     /* lines per chunk                              */
    int         nOverlap;   /* lines shared with the previous chunk         */
} VikiChunking;

/* The measured default: 40 lines with 10 overlapping. Ported, not re-chosen
** (recall@1 0.256 -> 0.349, MRR 0.381 -> 0.424 at +26% chunks; 20 cost +77%
** chunks for nothing). */
extern const VikiChunking vikiChunkDefault;

RETAIN_DECLARE(VikiDiaries);
RETAIN_DECLARE(VikiEmbed);

/* Creates the schema if absent and registers viki_cos() on this connection.
** Does NOT open, key, or locate anything. */
VikiStatus viki_attach(sqlite3 *db);

/* ---- assertions (oopc) ----------------------------------------------
** ONE type, because viki_note, cal_event and provenance are the same thing
** written three times: a grow-only row on an identity key, resolved at read
** time, losers retained.
**
** Every slot takes VikiAssert* -- oopc rule 1 -- so no function-pointer cast
** arises and a mistyped slot is a compile error. Note what is ABSENT: no
** sqlite3*, no embedder, no counters. That is what retaining buys. */
typedef struct VikiAssertStruct VikiAssert;

struct VikiAssertVftbl {
    const VikiType *type;
    const char *(*key) (const VikiAssert*);  /* what it competes on          */
    const char *(*rank)(const VikiAssert*);  /* LEXICAL sort key; max wins   */
    const char *(*text)(const VikiAssert*);  /* what gets chunked/embedded   */
    const char *(*canon)(const VikiAssert*); /* the bytes the id is a hash OF*/
};

struct VikiAssertStruct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];  /* filled by viki_put()                 */
    const char *zTs;                 /* ISO-8601 UTC; lexical order is time  */
    const char *zSupersedes;         /* another assertion's id, or NULL      */
};

/* THE RANK IS COMPUTED AT WRITE TIME AND STORED. That is the whole payoff of
** making this polymorphic: `rank()` yields a sortable string, and then ONE
** SQL statement resolves every type. Today this logic exists twice, in
** different SQL, and resolves differently in each -- a superseded note leaves
** the ledger, a superseded calendar assertion stays. Both are defensible;
** having no shared type to state them against is not. */
VikiStatus viki_put(VikiAssert *p);                 /* grow-only; fills zId  */
VikiStatus viki_get(const char *zId, char **pzBody);/* caller frees          */

/* The current assertion on a key: highest rank among those nothing
** supersedes. Returns VIKI_ENOTFOUND if the key has none. */
VikiStatus viki_current(const char *zKey, char *zIdOut, char **pzBody);

/* ---- merge: the whole of "sync" that belongs in a library ------------
** Union another store. Identity is a content hash, so this is INSERT OR
** IGNORE and there is no conflict resolution to get wrong. HOW pOther
** arrived -- HTTP, iCloud, AirDrop, a USB stick -- is the host's business,
** which is why there is no network in this header. Chunks are NOT merged:
** they are a projection and are rebuilt (D-10). */
VikiStatus viki_merge(sqlite3 *pOther, int *pnAdded);

/* ---- withdrawal ------------------------------------------------------
** THE STORE IS GROW-ONLY AND THESE ARE THE DELIBERATE EXCEPTIONS. Neither is
** a merge operation and neither propagates: a peer that has the assertion
** still has it, so forgetting is LOCAL. Anything else would be a tombstone
** protocol, which is a different and much larger design.
**
** viki_forget() exists because "I pasted a credential into a note" is a real
** thing that happens, and a memory with no way to unsay something is one
** people stop telling things to. */
VikiStatus viki_forget(const char *zId);

/* Destroys the content of everything a stored 'redact' assertion names, and
** is run automatically at the end of every merge. Exposed so a host can
** sweep after writing a tombstone locally. See apply_redactions() for what
** redaction costs (the store is a 2P-Set, not a G-Set) and what it cannot
** promise (a peer that never merges again keeps its copy). */
VikiStatus viki_redact_apply(int *pnRemoved);

/* Drops a dead model's ranges. The assertions are untouched -- this is
** reclaiming space from a model that is no longer pinned. */
VikiStatus viki_prune_model(const char *zModel, int *pnDropped);

/* Rebuild the chunk/FTS/vector projection for anything not yet projected at
** the retained epoch. Safe to call repeatedly; it is incremental. */
VikiStatus viki_reindex(const VikiChunking *pCh, int *pnChunked);

/* ---- retrieval ------------------------------------------------------- */
typedef struct {
    char   zId[VIKI_ID_HEX+1];
    int    lo, hi;        /* the RANGE, which is the citable extent          */
    double score;
    char  *zText;         /* owned by the VikiHits; computed, never stored   */
    char   zChunking[32]; /* which policy drew these lines -- provenance     */
} VikiHit;
typedef struct VikiHits {
    int      n;
    int      bDegraded;   /* 1 when no embedder was retained: no vector leg */
    VikiHit *a;
} VikiHits;

/* Asks ONE diary. zDiary NULL means the private one. To search several, ask
** several times -- the caller then knows which diary answered, which is the
** point. */
VikiStatus viki_ask_in(const char *zDiary, const char *zQuery, int k, VikiHits**);
#define viki_ask(q,k,pp) viki_ask_in(0,(q),(k),(pp))
void       viki_hits_free(VikiHits*);

/* ---- the convenience that is the whole point ------------------------- */
VikiStatus viki_note(const char *zText);
VikiStatus viki_noteid(const char *zText, char *zIdOut);   /* >= 65 bytes */

/* ---- identity and signing -------------------------------------------
**
** CONTENT ADDRESSING ALREADY GIVES INTEGRITY. An assertion's id is the hash
** of what it says, so tampering produces a DIFFERENT assertion rather than a
** corrupted one. What it does not give is AUTHORITY: anyone holding the diary
** can write anything into it, and the store cannot tell you who did.
** Signatures supply exactly that missing half and nothing else.
**
** PLUGGABLE BY CONSTRUCTION, for the same reason the embedder is: core links
** no crypto, so signing and verifying are HOST callbacks. That is what lets
** the mechanism differ per platform without core knowing:
**
**   a human on a laptop   the private key lives in the Secure Enclave / TPM
**                         and a THUMBPRINT authorises one signature. xSign
**                         calls the platform; the key never enters this
**                         process, let alone this library.
**   an agent              its identity is an assertion IN YOUR OWN DIARY --
**                         a name and a public key you wrote down -- and it
**                         signs with the key you issued it. Its authority is
**                         then traceable to an entry you can read.
**   a headless peer       a file, an env var, an HSM. Core cannot tell.
**
** SIGNATURES ARE THEIR OWN ROWS, not a column, because SEVERAL identities may
** sign ONE assertion. Countersigning is then union-merge like everything
** else, and a signed copy can never be shadowed by an unsigned one -- which
** it would be if the signature rode on the assertion and INSERT OR IGNORE
** kept whichever arrived first.
**
** VERIFICATION NEEDS NO SECRET. Checking a signature uses only the signer's
** recorded public key, so a peer that holds no private material can still
** establish who said what. */
typedef struct {
    const char *zSigner;   /* the signing identity's assertion id           */
    void       *pApp;
    /* aSig is at most VIKI_SIG_MAX bytes. Return non-zero to decline --
    ** declining is not an error: it is how a cancelled thumbprint prompt or
    ** a locked keychain reports itself, and the assertion is then stored
    ** UNSIGNED rather than lost. */
    int (*xSign)  (void *pApp, const char *zId,
                   unsigned char *aSig, int *pnSig);
    int (*xVerify)(void *pApp, const char *zPubKey, const char *zId,
                   const unsigned char *aSig, int nSig);
} VikiIdentity;
#define VIKI_SIG_MAX 128

RETAIN_DECLARE(VikiIdentity);

/* ---- the built-in Ed25519 signer -------------------------------------
** SQLCipher and LibreSSL are bedrock, so core ships a default signer rather
** than leaving every host to write an EVP program. The CALLBACKS above stay
** authoritative: a platform keystore fills VikiIdentity itself and never
** touches any of this. Included is not mandatory.
**
** The key file is a name line then a 32-byte seed as 64 hex characters, mode
** 600 -- the name is IN it because viki's identity is (public key, name), so
** a rename is a different claim and must be visible. */
typedef struct VikiIdKey VikiIdKey;
VikiIdKey  *viki_ed25519_generate(const char *zName, char *zSeedHexOut); /* >=65 */
VikiIdKey  *viki_ed25519_load(const char *zPath, char *zErr, size_t nErr);
void        viki_ed25519_free(VikiIdKey*);
const char *viki_ed25519_pub (const VikiIdKey*);
const char *viki_ed25519_name(const VikiIdKey*);
int viki_ed25519_sign  (void *pApp, const char *zId, unsigned char *aSig, int *pnSig);
int viki_ed25519_verify(void *pApp, const char *zPubHex, const char *zId,
                        const unsigned char *aSig, int nSig);
/* Records the identity and fills xSign/xVerify/pApp. zIdOut receives the
** identity's assertion id, which the caller assigns to zSigner. */
VikiStatus viki_identity_ed25519(VikiIdKey*, VikiIdentity *pOut, char *zIdOut);

/* An identity IS an assertion -- kind "identity", canon (name, public key) --
** so identities merge, are searchable, and travel with the diary. Recording
** one is not the same as trusting it; see viki_signed(). */
VikiStatus viki_identity_put(const char *zName, const char *zPubKey,
                             char *zIdOut);

/* What core can say about a signature. All four are FACTS. Whether a verified
** signer is a TRUSTED one is a judgment, and judgments live in the caller --
** the same line coverage draws by reporting last-seen times and no
** thresholds. */
typedef enum {
    VIKI_SIG_NONE = 0,   /* nobody signed it                                */
    VIKI_SIG_OK,         /* verified against the signer's recorded key      */
    VIKI_SIG_BAD,        /* a signature is present and does NOT verify      */
    VIKI_SIG_UNKNOWN     /* signed by an identity this diary does not hold  */
} VikiSigState;

/* Reports the strongest state found across every signature on the assertion,
** and which signer produced it. */
VikiStatus viki_signed(const char *zId, VikiSigState *pState,
                       char *zSignerOut);

/* Countersign an assertion that is already stored -- the retained identity
** adds its own row. */
VikiStatus viki_countersign(const char *zId);

/* ---- blobs: an assertion whose TEXT is not its BYTES -----------------
**
** THE TWO VTABLE SLOTS WERE ALWAYS DIFFERENT THINGS, and a blob is what makes
** the difference load-bearing:
**
**     canon()   the bytes identity is computed over
**     text()    what gets chunked and embedded
**
** For a note they are the same string. For 23 MB of int8 ONNX coefficients
** they cannot be: the bytes have no semantic content, so embedding them
** yields a vector of noise, and no chunking of them means anything. What is
** worth embedding is a DESCRIPTION -- "all-MiniLM-L6-v2, int8, 384-dim
** sentence embeddings" -- so the blob's single range covers the description
** and its vector is the description's.
**
** That is the general shape, not a special case for models: a PDF's text is
** its extracted text, an image's is its caption or OCR, a recording's is its
** transcript. THE PAYLOAD IS ADDRESSED; THE DESCRIPTION IS SEARCHED.
**
** Identity is the caller's content hash, not a rehash here: the host already
** has it (D-12 pins the model's checksum), and re-hashing 23 MB inside a put
** would be paying twice for a number that must match the pin anyway. */
VikiStatus viki_blob_put(const char *zDesc, const char *zContentHash,
                         const void *pBytes, sqlite3_int64 nBytes,
                         char *zIdOut);

/* Borrowed, valid until the next core call on this store. NULL *ppBytes with
** VIKI_OK means the assertion exists but carries no payload. */
VikiStatus viki_blob_get(const char *zId, const void **ppBytes,
                         sqlite3_int64 *pnBytes);

/* Finds a blob by DESCRIPTION PREFIX across EVERY OPEN DIARY, newest first.
** This is the one read that deliberately spans the set rather than naming a
** diary: the model lives in `core` while notes live in `private`, and a
** caller asking for "model.onnx" should not have to know which. */
VikiStatus viki_blob_find(const char *zPrefix, const void **ppBytes,
                          sqlite3_int64 *pnBytes, const char **pzDiary);

/* ---- observability ---------------------------------------------------
**
** A HOST MUST NOT HAVE TO POLL, AND MUST NOT HAVE TO READ THE TABLES.
** These are SEMANTIC events, not row events: "an assertion arrived", not
** "a row was inserted into viki_assert". The schema is core's business and
** is free to change -- ranges replaced ordinals once already -- so anything
** a listener is told is expressed in the vocabulary of the API.
**
** WHEN THEY FIRE, AND WHY IT IS NOT IMMEDIATELY.
**
** Core runs inside SAVEPOINTs on a connection it does not own, so the host
** may roll back afterwards. Firing during the operation would announce
** changes that never happened -- and a listener that wrote to a UI, sent a
** notification, or told a peer could not take it back. So events are BUFFERED
** and flushed on COMMIT, and DISCARDED on rollback. viki_attach() installs
** SQLite's commit/rollback hooks for exactly this.
**
** A listener therefore never sees an uncommitted change, and never misses a
** committed one.
**
** REENTRANCY: a listener MUST NOT call a mutating viki_* function. Doing so
** is refused with VIKI_EBUSY rather than deadlocking or corrupting the
** buffer. Reads are fine. */
typedef enum {
    VIKI_EV_PUT = 1,      /* an assertion was stored                        */
    VIKI_EV_SUPERSEDED,   /* ...and it superseded another (zOther is which) */
    VIKI_EV_FORGOTTEN,    /* an assertion was removed                       */
    VIKI_EV_MERGED,       /* a merge completed; zId is NULL, n is the count */
    VIKI_EV_PROJECTED     /* reindex added ranges; zId is NULL, n is count  */
} VikiEventKind;

typedef struct {
    VikiEventKind kind;
    const char   *zId;    /* the assertion, or NULL for bulk events         */
    const char   *zKey;   /* what it competes on, when known                */
    const char   *zOther; /* the superseded id, for VIKI_EV_SUPERSEDED      */
    int           n;      /* count, for bulk events                         */
} VikiEvent2;

typedef void (*viki_watch_fn)(void *pApp, const VikiEvent2 *pEv);

/* Watches are per-STORE, not global: they live on the sqlite3* so two stores
** in one process notify independently. */
VikiStatus viki_watch(sqlite3 *db, viki_watch_fn x, void *pApp, int *pnToken);
VikiStatus viki_unwatch(sqlite3 *db, int nToken);

/* ---- reading without knowing the schema ------------------------------
** Enough that a caller never needs to name a table. If something cannot be
** asked here, that is a gap in the API rather than a reason to reach past
** it -- core's own probe is written against these, which is what keeps the
** claim honest. */
typedef enum {
    VIKI_N_ASSERT = 1,    /* assertions stored                              */
    VIKI_N_CURRENT,       /* RESOLVED WINNERS, one per key -- what
                          ** viki_current() returns. NOT the same as
                          ** "nothing supersedes it": an update that wins
                          ** by RANK sets no supersedes link.            */
    VIKI_N_RANGE,         /* chunk ranges                                   */
    VIKI_N_VECTOR,        /* ...carrying a vector                           */
    VIKI_N_CHUNKING,      /* distinct chunking policies present             */
    VIKI_N_MODEL          /* distinct models present                        */
} VikiCountWhat;

/* zFilter is optional and matches an assertion KIND ("note", "event", ...)
** or, for range counts, a model name. NULL counts everything. */
VikiStatus viki_count(VikiCountWhat what, const char *zFilter, int *pn);

/* Iterate assertions, newest rank first. zKind and zKey are optional
** filters. Returning non-zero from the callback stops the walk. */
typedef int (*viki_assert_row)(void *pApp, const char *zId, const char *zKind,
                               const char *zKey, const char *zTs,
                               const char *zBody, int bCurrent);
VikiStatus viki_each(const char *zKind, const char *zKey,
                     viki_assert_row x, void *pApp);

/* ---- the raw rung (SCOPES 1b: a curated verb is never the only door) -- */
typedef int (*viki_row)(void*, int nCol, const char *const *azVal);
VikiStatus viki_sql(const char *zSql, viki_row x, void *pApp);

/* The note type, so a caller can put one directly rather than via viki_note. */
extern const struct VikiAssertVftbl vikiNoteVftbl;
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;
    const char *zText;
    const char *zKey;
} VikiNote;

#ifdef __cplusplus
}
#endif
#endif /* VIKI_CORE_H */
