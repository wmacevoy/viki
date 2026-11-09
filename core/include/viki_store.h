/*
** viki_store.h -- the assertion store of REQUIREMENTS_v3: write, resolve,
** merge, erase, search. The C interface is the interface; every other
** language binds to this.
**
** The caller opens and keys the sqlite3* and hands it over borrowed; core
** opens no file and reads no clock (REQ3/SHAPE-6). Bytes are pointer plus
** length, never NUL-terminated (A-4a). Anything marked "borrowed" stays
** valid only until the next call on the object it came from.
**
** One VkStore per connection, one thread at a time.
*/
#ifndef VIKI_STORE_H
#define VIKI_STORE_H

#include <stdint.h>
#include <sqlite3.h>

#define VK_ID_BYTES    32
#define VK_RANK_BYTES  16
/* invented: limits that keep every operation in fixed memory (ENV-2). */
#define VK_MAX_LINKS   16
#define VK_MAX_FIELD   4096
/* A log position fits 32 bits, so a row has at most this many skip links. */
#define VK_MAX_SKIP    32
#define VK_MAX_POS     0xFFFFFFFFu

typedef enum {
    VK_OK = 0,
    VK_EXISTS,     /* already present: a no-op, reported as one (A-5) */
    VK_DONE,       /* a cursor is exhausted */
    VK_NOTFOUND,
    VK_FORKED,     /* more than one arm; current() will not choose (R-1, R-5) */
    VK_BLIND,      /* a search method failed its self-check (Q-3) */
    VK_EINVAL,     /* refused: malformed input */
    VK_EDENIED,    /* refused: the principal lacks the right (D-3) */
    VK_ENOMEM,
    VK_ESQL
} VkStatus;

typedef struct { const uint8_t *p; uint32_t n; } VkBytes;
typedef uint8_t VkId[VK_ID_BYTES];

/* Rights, per principal and diary (D-3). */
#define VK_ERASE     1
#define VK_READ      2
#define VK_SUPERSEDE 4
#define VK_ORIGINATE 8

typedef struct { VkBytes principal; int rights; } VkGrant;

typedef struct {
    VkBytes author, kind, akey, ts, reference;
    uint8_t rank[VK_RANK_BYTES];
    const VkId *supersedes;  uint32_t nSupersedes;   /* any order, no repeats */
    const VkId *derivedFrom; uint32_t nDerivedFrom;
    VkBytes body;
    /* An erased assertion has no body, only the hash its id was made from. */
    int erased;
    uint8_t bodyHash[VK_ID_BYTES];
    /* LOG-1, LOG-2. Empty log, position 0 and no skips: not in a log.
    ** vk_append fills pos and skip. */
    VkBytes log;
    uint64_t pos;
    const VkId *skip;  uint32_t nSkip;   /* level order: pos-1, pos-2, pos-4 ... */
} VkAssertion;

typedef struct VkStore VkStore;

/* The id of an assertion: pure, and needs no store. */
void vk_assertion_id(const VkAssertion *a, VkId out);

/* grants and principal are borrowed for the life of the store. A later
** version reads grants from the diary; for now the host supplies them. */
VkStatus vk_open(sqlite3 *db, VkBytes principal, const VkGrant *grants,
                 uint32_t nGrants, VkStore **ppOut);
int  vk_close(VkStore *p);              /* the sqlite3_finalize-style rc */
const char *vk_errmsg(const VkStore *p); /* names the requirement refused */
int vk_sqlite_code(const VkStore *p);    /* the SQLite code behind the last VK_ESQL */

/* Write. VK_OK, or VK_EXISTS if already held; the id is set for both. */
VkStatus vk_put(VkStore *p, const VkAssertion *a, VkId idOut);
/* D-3f: a->supersedes is ignored; supersedes every current arm of a->akey. */
VkStatus vk_put_chain(VkStore *p, const VkAssertion *a, VkId idOut);
/* LOG-1: the next position of (a->author, a->log), with its skip links.
** a->pos and a->skip are ignored. VK_EINVAL if a position it must link to is
** neither held nor carried by a range tombstone. */
VkStatus vk_append(VkStore *p, const VkAssertion *a, VkId idOut);
/* Writes a tombstone. The target's content goes; its frame stays. */
VkStatus vk_erase(VkStore *p, const VkId target, VkBytes ts,
                  const uint8_t rank[VK_RANK_BYTES], VkId idOut);

/* W-15: a range tombstone over positions from..to of (target, log). Those
** rows go entirely. VK_EINVAL if an id it must carry is not held, or W-17
** refuses the range. */
VkStatus vk_forget(VkStore *p, VkBytes log, VkBytes target, uint64_t from, uint64_t to,
                   VkBytes ts, const uint8_t rank[VK_RANK_BYTES], VkId idOut);

/* The lowest and highest positions of (author, log) held as rows. VK_NOTFOUND
** if there are none. */
VkStatus vk_log_span(VkStore *p, VkBytes log, VkBytes author, uint64_t *pLow, uint64_t *pHigh);

/* Read. A VkRow holds a statement: vk_row_close it after VK_OK. */
typedef struct {
    VkId id;
    VkBytes body;      /* borrowed; empty when erased */
    int erased;
    int nArms;         /* more than 1 means a fork was resolved (R-2, R-5) */
    sqlite3_stmt *pSt;
} VkRow;
VkStatus vk_current(VkStore *p, VkBytes akey, VkRow *out);
VkStatus vk_resolve(VkStore *p, VkBytes akey, VkRow *out);
void vk_row_close(VkRow *r);

/* Walk every row in arrival order, one at a time. */
typedef struct {
    VkStore *p;
    sqlite3_stmt *pRow, *pEdge;
    VkAssertion a;     /* borrowed until the next vk_cursor_next */
    VkId id;
    int64_t seq;
    VkId links[2][VK_MAX_LINKS];
    VkId skip[VK_MAX_SKIP];
} VkCursor;
VkStatus vk_cursor_open(VkStore *p, int64_t afterSeq, VkCursor *c);
VkStatus vk_cursor_next(VkCursor *c);    /* VK_OK or VK_DONE */
void vk_cursor_close(VkCursor *c);

/* Merge: one row at a time, re-checking each id on arrival. */
typedef struct {
    int64_t added, present, quarantined;
    int64_t covered;   /* W-16: arrived inside a range tombstone, not stored */
    int incomplete;    /* M-3 */
} VkMergeReport;
VkStatus vk_ingest(VkStore *dst, const VkAssertion *a, const VkId id,
                   VkMergeReport *rep);
VkStatus vk_merge(VkStore *dst, VkStore *src, VkMergeReport *rep);

/* Recompute every stored id from stored bytes; *pnBad counts mismatches. */
VkStatus vk_verify(VkStore *p, int64_t *pnBad);
/* A digest of what is held: equal sets, equal digests. */
VkStatus vk_digest(VkStore *p, VkId out);

/* Search: the one parameterized family. A method is a const table; a build
** without a method simply does not link its table (ENV-3). */
typedef struct VkSearchMethod {
    const char *zName;
    int bExhaustive;   /* its "not found" covers every readable assertion */
    /* Prepares a statement whose rows have the hit's id in column 0. */
    VkStatus (*open)(VkStore *p, VkBytes query, sqlite3_stmt **ppOut);
    /* VK_OK only if the method finds a planted case and rejects a planted
    ** miss (Q-3). */
    VkStatus (*selfcheck)(VkStore *p);
} VkSearchMethod;

extern const VkSearchMethod vkGrep;

typedef struct {
    VkStore *p;
    const VkSearchMethod *pMethod;
    sqlite3_stmt *pSt;
} VkSearch;

/* What a search covered (Q-2). */
typedef struct {
    const char *zMethod;
    int bExhaustive;
    int64_t nSearched;
} VkCoverage;

/* VK_BLIND if the method fails its self-check. */
VkStatus vk_search_open(VkStore *p, const VkSearchMethod *m, VkBytes query,
                        VkSearch *s);
VkStatus vk_search_next(VkSearch *s, VkId out);   /* VK_OK or VK_DONE */
VkStatus vk_search_coverage(VkSearch *s, VkCoverage *out);
void vk_search_close(VkSearch *s);

#endif
