/*
** viki.h -- the public C ABI. PROPOSED; nothing behind it is built yet.
**
** THE C ABI IS THE INTERFACE (../oopc's rule). The CLI, the HTTP API, MCP,
** the wasm edge and any Swift/Dart shim are BINDINGS to this header, written
** in their own language with no per-verb glue on this side. viki already
** half-believes this -- viki_ask_query() is one implementation and the CLI
** and /api/ask are thin callers, which is why they provably cannot disagree.
** This generalises that from one function to the whole surface.
**
** ---------------------------------------------------------------------
** CONTEXT: THREE OPTIONS, AND WHEN EACH IS RIGHT
**
**   explicit parameters   fine, and the default. Use them when the caller is
**                         one frame away and naming the thing is honest.
**   external / global     what viki does today, via getenv(). See below.
**   retain / recall       pushing state opaquely to nested scope, when the
**                         consumer is many frames down and must not grow a
**                         parameter -- especially through POLYMORPHIC
**                         plumbing, where the context would otherwise have
**                         to appear in every vtable slot of every type.
**
** viki currently has no middle setting: ten values (VIKI_FOSSIL_REPO,
** VIKI_FOSSIL_BIN, VIKI_FOSSILSEE_LIB, VIKI_MODEL_DIR, VIKI_NO_FORK, ...)
** are read by getenv() in four different files, at whatever depth needs
** them. That is the retain/recall pattern with no type, no scope, no
** nesting and process lifetime.
**
** ../retain-recall/ports/c/retain.h opens with the same problem one layer
** down: src/sort.c declares compare() and swap() as plain globals, so it can
** only ever sort ONE array. viki reads its repository from the environment,
** so it can only ever serve ONE TRIBE. SCOPES 4 defines *the verse* as the
** set of tribes reachable from one device, and on iOS that is one process --
** so this is not tidiness, it is the ceiling on the platform where multiple
** tribes are the entire point.
**
** ---------------------------------------------------------------------
** THE WIN
**
**     viki_note("the gate latch sticks below freezing");
**
** One argument: the message. No db handle, no embedder, no model id, no
** directory. Compare today's entry point, which is
**
**     viki_cmd_capture(".", zText, zPlace, zType, zWho, zDue, zState, zChannel)
**
** -- eight parameters, the first of them a directory string, and every one
** of them plumbing the caller had to know about. After a tribe is retained,
** ANY code at ANY depth can note, ask, or grep without being handed a thing.
** That is what makes viki callable from inside a connector, a callback, or a
** library that has never heard of it.
*/
#ifndef VIKI_H
#define VIKI_H

#include <stddef.h>
#include "retain.h"    /* ../retain-recall/ports/c/retain.h, vendored */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- runtime type ---------------------------------------------------
**
** ../oopc's three words per class, zero per object: an object reaches its
** type through the vtable it already points at, so it carries no type tag.
** viki needs this for the same reason oopc does -- `viki why` and the raw
** SQL rung both hand back heterogeneous assertions, and a caller must be
** able to ask what one IS without a tag field on every row. */
typedef struct VikiTypeStruct VikiType;
struct VikiTypeStruct {
    const char     *zName;
    const VikiType *pParent;
    size_t          szObject;
};
int viki_isa(const VikiType *pMe, const VikiType *pOf);

/* ---- status ---------------------------------------------------------
**
** A CONTRACT THAT DOES NOT STATE HOW IT FAILS HAS NOT STATED ANYTHING
** (SOFTWARE-ENGINEERING-2 SS 5). Every call below returns one of these, and
** the one that matters is VIKI_ENOTRIBE: calling viki_note() with nothing
** retained is an ERROR, never a silent no-op. A memory system that quietly
** drops what it was told is worse than one that is absent, because the
** caller believes it was remembered. */
typedef enum {
    VIKI_OK = 0,
    VIKI_ENOTRIBE,     /* nothing retained -- see RETAIN_BEGIN below */
    VIKI_EDEGRADED,    /* served, but without the model: BM25 only */
    VIKI_EREADONLY,    /* the tribe was opened read-only */
    VIKI_ENOFORK,      /* needed a subprocess in a build that forbids it */
    VIKI_EIO, VIKI_ESQL, VIKI_ENOMEM, VIKI_EINVAL
} VikiStatus;

const char *viki_errmsg(void);   /* last error on this thread, never NULL */

/* ---- the retained context ------------------------------------------- */

typedef struct VikiTribeStruct VikiTribe;

#define VIKI_RDONLY   0x01
#define VIKI_NOFORK   0x02   /* the iOS build; see viki_fork_forbidden() */
#define VIKI_NOMODEL  0x04   /* force degraded mode */

VikiTribe *viki_tribe_open(const char *zRepo, const char *zKey, unsigned mFlags);
void       viki_tribe_close(VikiTribe*);

RETAIN_DECLARE(VikiTribe);
/* RETAIN_DEFINE(VikiTribe) lives in viki_tribe.c -- one definition. */

/*  VikiTribe *t = viki_tribe_open(zRepo, zKey, 0);
**  RETAIN_BEGIN(VikiTribe, t, guard);
**      viki_note("gate latch sticks below freezing");
**      some_deep_callback();          // may also viki_note(); knows nothing of t
**  RETAIN_END(guard);
**
** Nesting is the point, not a bonus: a second RETAIN_BEGIN inside the first
** operates on another tribe and restores the outer one on scope exit, which
** is the property getenv() cannot express at all. */

/* ---- the verbs. Every one takes ONLY its own argument. ---------------
**
** These are SCOPES SS 2's two rungs, unchanged: CURATED conveniences plus the
** RAW escape hatch. None of them decides anything -- viki answers questions
** and judgement lives in the caller (assistant/README.md). */

typedef struct VikiHits      VikiHits;       /* ask / grep / muse results */
typedef struct VikiRows      VikiRows;       /* raw sql result */
typedef struct VikiLedger    VikiLedger;     /* promises */
typedef struct VikiCoverage  VikiCoverage;
typedef struct VikiChain     VikiChain;      /* why: supersession both ways */

/* THE WIN. Returns the note's content hash in zIdOut when it is non-NULL,
** because an id you cannot cite is not provenance. */
VikiStatus viki_note (const char *zText);
VikiStatus viki_noteid(const char *zText, char *zIdOut, size_t nId);
VikiStatus viki_notef(const char *zFmt, ...);   /* printf form, for log sites */

VikiStatus viki_ask     (const char *zQuery, int k, VikiHits **ppOut);
VikiStatus viki_grep    (const char *zRegex, int k, VikiHits **ppOut);
VikiStatus viki_muse    (int k, unsigned seed,       VikiHits **ppOut);
VikiStatus viki_promises(const char *zWho, const char *zHorizon, VikiLedger**);
VikiStatus viki_why     (const char *zNoteId,        VikiChain **ppOut);
VikiStatus viki_coverage(VikiCoverage **ppOut);
VikiStatus viki_sql     (const char *zSql,           VikiRows  **ppOut);
VikiStatus viki_index   (const char *zDir, int *pnItems, int *pnChunked);

/* ---- polymorphism, and why the context is retained rather than passed
**
** viki has two families written out by hand. Both are the ordinary pattern;
** ../oopc is one disciplined way to spell it in C (prefix layout, static
** const vtables, EVERY SLOT TAKES THE ROOT TYPE so no function-pointer cast
** ever arises, static_assert pinning each layer).
**
** THIS IS THE "ESPECIALLY POLYMORPHIC PLUMBING" CASE. Note what is absent
** from every slot below: no sqlite3*, no viki_embedder*, no model id, no
** counters. If the context were a parameter it would have to appear in each
** slot of each vtable of each type -- and it is precisely that pressure that
** produced nine hand-written extractors with a five-parameter tail in three
** drifted shapes. Retained, the slots state only what actually varies. */

typedef struct VikiExtractorStruct VikiExtractor;
typedef struct VikiRecord          VikiRecord;   /* one framed row */
typedef struct VikiText            VikiText;

struct VikiExtractorVftbl {
    const VikiType *type;
    const char *(*sql) (const VikiExtractor*);
    VikiStatus  (*path)(const VikiExtractor*, const VikiRecord*, char*, size_t);
    VikiStatus  (*text)(const VikiExtractor*, const VikiRecord*, VikiText*);
};
/* The fixed algorithm -- run sql, frame it, loop, compose a path, index the
** text, report authority -- is written ONCE over VikiExtractor*, instead of
** nine times. Measured cost of the nine: the NUL exclusion appears four
** times (viki_index.c:1077, :1472, :1888, :1985) and `return it.seenEof`
** seven times. */

typedef struct VikiAssertStruct VikiAssert;

struct VikiAssertVftbl {
    const VikiType *type;
    const char *(*key)      (const VikiAssert*);   /* what it competes on */
    int         (*rank)     (const VikiAssert*, const VikiAssert*);
    const char *(*text)     (const VikiAssert*);   /* what gets embedded */
    VikiStatus  (*emit)     (const VikiAssert*, VikiText *pArtifact);
};
/* viki_note and cal_event are this type twice. resolve() -- given the
** assertions on one key, which is current -- is then written once instead of
** twice-going-on-three. emit() is the load-bearing slot: truth is a Fossil
** artifact and the table is a projection (D-10). Today NEITHER writes an
** artifact, which is how viki ended up with three truth stores. */

VikiStatus viki_resolve(VikiAssert **apIn, int nIn, VikiAssert ***papOut, int *pnOut);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_H */
