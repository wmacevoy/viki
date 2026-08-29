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
    VIKI_ENOCTX, VIKI_ESQL, VIKI_ENOMEM, VIKI_EINVAL, VIKI_ENOTFOUND
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
typedef struct { sqlite3 *db; } VikiStore;

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
    const char   *zEpoch;
} VikiEmbed;

RETAIN_DECLARE(VikiStore);
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

/* Drops a dead embedding epoch's projection. The assertions are untouched --
** this is reclaiming space from a model that is no longer pinned. */
VikiStatus viki_prune_epoch(const char *zEpoch, int *pnDropped);

/* Rebuild the chunk/FTS/vector projection for anything not yet projected at
** the retained epoch. Safe to call repeatedly; it is incremental. */
VikiStatus viki_reindex(int *pnChunked);

/* ---- retrieval ------------------------------------------------------- */
typedef struct {
    char   zId[VIKI_ID_HEX+1];
    int    ix;
    double score;
    char  *zText;         /* owned by the VikiHits */
} VikiHit;
typedef struct VikiHits {
    int      n;
    int      bDegraded;   /* 1 when no embedder was retained: no vector leg */
    VikiHit *a;
} VikiHits;

VikiStatus viki_ask(const char *zQuery, int k, VikiHits **ppOut);
void       viki_hits_free(VikiHits*);

/* ---- the convenience that is the whole point ------------------------- */
VikiStatus viki_note(const char *zText);
VikiStatus viki_noteid(const char *zText, char *zIdOut);   /* >= 65 bytes */

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
