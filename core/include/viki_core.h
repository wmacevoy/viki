/*
** viki_core.h -- the whole public surface. PROPOSED; nothing is built.
**
** THE CONTRACT IS SQLITE. Not Fossil, not a filesystem, not a network. The
** caller hands us an open sqlite3* and gets assertions, retrieval and time
** back. That single decision carries the encryption key, the VFS, the file
** location and the platform's storage rules out of scope at once -- the host
** opened the connection, so the host already chose all four.
**
** FOSSIL IS ANCESTRY, NOT A DEPENDENCY. Content addressing, grow-only rows,
** union-is-merge, and resolution at read time are its ideas and they are
** kept. Its code, schema, manifest format, wire protocol and CLI are not.
*/
#ifndef VIKI_CORE_H
#define VIKI_CORE_H

#include <stddef.h>
#include <stdint.h>
#include "sqlite3.h"
#include "retain.h"     /* ../retain-recall/ports/c/retain.h */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- status ---------------------------------------------------------
** A contract that does not state how it fails has not stated anything.
** VIKI_ENOCTX is the one that matters: viki_note() with nothing retained is
** an ERROR, never a silent no-op. A memory system that quietly drops what it
** was told is worse than one that is absent. */
typedef enum {
    VIKI_OK = 0,
    VIKI_ENOCTX,      /* nothing retained */
    VIKI_EDEGRADED,   /* answered, but with no embedding model: BM25 + literal */
    VIKI_ESQL, VIKI_ENOMEM, VIKI_EINVAL, VIKI_ENOTFOUND
} VikiStatus;
const char *viki_errmsg(void);

/* ---- runtime type (oopc: three words per class, zero per object) ----- */
typedef struct VikiTypeStruct VikiType;
struct VikiTypeStruct {
    const char     *zName;
    const VikiType *pParent;
    size_t          szObject;
};
int viki_isa(const VikiType*, const VikiType*);

/* ---- the retained context -------------------------------------------
** Two types, two stacks, two lifetimes -- NOT one context struct. A caller
** that only wants viki_note() supplies only a store; nothing forces it to
** produce an embedder it will not use.
**
** AND DEGRADED MODE IS THE ABSENCE OF A TYPE, not a NULL to remember. viki
** has always required "no model" to be a working path rather than a failure;
** here that is simply !RETAINED(VikiEmbed). */
typedef struct { sqlite3 *db; } VikiStore;   /* the caller's connection */
typedef struct VikiEmbedStruct VikiEmbed;    /* model + epoch, opaque      */

RETAIN_DECLARE(VikiStore);
RETAIN_DECLARE(VikiEmbed);

/* Creates the schema if absent. Does NOT open, key, or locate anything. */
VikiStatus viki_attach(sqlite3 *db);

/*  VikiStore s = { db };
**  RETAIN_BEGIN(VikiStore, &s, g1);
**      viki_note("the gate latch sticks below freezing");   // store is enough
**      RETAIN_BEGIN(VikiEmbed, emb, g2);
**          viki_ask("what did I say about the gate?", 5, &hits);  // hybrid
**      RETAIN_END(g2);
**      viki_ask("...", 5, &hits);                  // BM25 + literal only
**  RETAIN_END(g1);
*/

/* ---- assertions (oopc) ----------------------------------------------
** ONE type, because viki_note, cal_event and provenance are the same thing
** written three times: a grow-only row on an identity key, resolved at read
** time, losers retained.
**
** Every slot takes VikiAssert* at every level -- oopc rule 1 -- so no
** function-pointer cast ever arises and a mistyped slot is a compile error.
** Note what is ABSENT from every slot: no sqlite3*, no embedder, no counters.
** That is what retaining the context buys, and why the nine hand-written
** extractors in viki_index.c had a five-parameter tail in three drifted
** shapes. */
typedef struct VikiAssertStruct VikiAssert;
typedef struct { const uint8_t *p; size_t n; } VikiBytes;

struct VikiAssertVftbl {
    const VikiType *type;
    const char *(*key)  (const VikiAssert*);                  /* competes on   */
    int         (*rank) (const VikiAssert*, const VikiAssert*);/* which wins   */
    const char *(*text) (const VikiAssert*);                  /* what embeds   */
    VikiStatus  (*canon)(const VikiAssert*, VikiBytes *pOut); /* id = hash OF  */
};
struct VikiAssertStruct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[65];        /* sha256 hex of canon() -- identity IS content */
    const char *zTs;            /* ISO-8601 UTC; lexical order is time order    */
    const char *zSupersedes;    /* another id, or NULL                          */
};

VikiStatus viki_put(const VikiAssert*);                     /* grow-only       */
VikiStatus viki_get(const char *zId, VikiAssert**);
/* Given the assertions on one key, which is current. Written ONCE, over the
** root type -- today this exists twice, in different SQL, and resolves
** differently in each (a superseded note LEAVES the ledger, a superseded
** calendar assertion STAYS). Both are defensible; having no shared type to
** state them against is not. */
VikiStatus viki_resolve(const char *zKey, VikiAssert ***papOut, int *pnOut);

/* ---- merge: the whole of "sync" that belongs in a library ------------
** Union two stores. Because identity is a content hash, this is
** INSERT OR IGNORE and there is no conflict resolution to get wrong.
** HOW the other store arrived -- HTTP, iCloud, AirDrop, a USB stick -- is
** the host's business, which is why there is no network in this header. */
VikiStatus viki_merge(sqlite3 *pOther, int *pnAdded);

/* ---- retrieval: viki's first genuinely new thing --------------------- */
typedef struct VikiHits VikiHits;
VikiStatus viki_ask (const char *zQuery, int k, VikiHits**);  /* bm25+lit+vec */
VikiStatus viki_grep(const char *zRegex, int k, VikiHits**);
VikiStatus viki_muse(int k, unsigned seed,      VikiHits**);
void       viki_hits_free(VikiHits*);

/* ---- time: viki's second ---------------------------------------------
** Stored AS WRITTEN with an IANA TZID, never as an offset. Occurrence
** expansion is deliberately a READ-time projection: it depends on zone,
** window and tzdata version, and is therefore not shareable. */
typedef struct VikiOccur VikiOccur;
VikiStatus viki_when(const char *zFromIso, const char *zToIso,
                     const char *zTzid, VikiOccur**, int *pn);

/* ---- the convenience that is the whole point ------------------------- */
VikiStatus viki_note (const char *zText);
VikiStatus viki_noteid(const char *zText, char *zIdOut, size_t nId);
VikiStatus viki_notef(const char *zFmt, ...);

/* ---- the raw rung ---------------------------------------------------
** SCOPES 1b: a curated verb must never be the only door. The vector function
** is registered on the caller's connection by viki_attach(), so this is
** where an agent reaches it. */
typedef int (*viki_row)(void*, int nCol, const char *const*, const int*);
VikiStatus viki_sql(const char *zSql, viki_row, void*);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_CORE_H */
