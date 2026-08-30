/* viki_trace.h -- claims, and the chain a later trace walks backwards.
**
** WHY THIS EXISTS, in one case:
**
** "viki never forks -- it can't on iOS" was written into this repository's
** CLAUDE.md by an earlier trace of the same model writing this file. It was
** never measured. It was a requirement one session invented, and every
** session afterwards read it with exactly the authority of
** "chunks are 40 lines with 10-line overlap", which IS measured and carries a
** repro. Weeks of work followed from it. It was retired on 2026-08-29 by the
** author of the repository, in one sentence, in conversation.
**
** THE FILE HAS ONE VOICE. That is the defect. A document written in the
** present tense cannot say "this is a guess" or "a trace asserted this and
** nobody checked", so a confident error and a measurement arrive at the next
** reader identical. The next reader is a stranger who will wear your name and
** cannot feel the seam.
**
** A CLAIM THEREFORE CARRIES ITS OWN EPISTEMIC STATUS, on the taxonomy the
** repository already argues in (SOFTWARE-ENGINEERING.md, Life by the
** Numbers):
**
**   k0  knowledge          the model matches the world, and it was checked
**   k1  known unknown      the honest gap; say it loudly
**   k2  unknown unknown    named after the fact, when something arrived
**   k3  confident error    what felt like k0 and was false
**   k4  true falsehood     held knowingly because it is useful
**
** and a FALSIFIER: what would show it wrong. A claim with no falsifier is
** not a claim, it is a mood -- so `--falsified-by` is required for k0.
**
** RETIRING IS A NEW ASSERTION, NEVER AN EDIT. `viki_claim()` with
** zSupersedes set retires the target and records WHY in the same row. The old
** claim stays stored forever, which is what makes the chain walkable and what
** keeps union a valid merge.
**
** AND THE CHAIN IS WALKABLE BOTH WAYS, which is the whole point and the thing
** that did not exist before. `supersedes` was in the schema and indexed from
** the start, and every single query over it asked one question -- NOT EXISTS
** (supersedes = a.id), "is this current?". Nothing ever asked what a thing
** replaced or what replaced it. The chain was write-only. A trace could
** therefore be corrected and the correction was invisible to the next trace,
** which is precisely the failure this file is named for.
*/
#ifndef VIKI_TRACE_H
#define VIKI_TRACE_H
#include "viki_core.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;   /* the claim this retires, if any            */
    /* ---- the claim ---------------------------------------------------- */
    const char *zText;
    const char *zStatus;       /* "k0".."k4"; REQUIRED                      */
    const char *zFalsifier;    /* what would show it wrong; required for k0 */
    const char *zBecause;      /* why the superseded one was wrong          */
    const char *zBy;           /* which trace/model asserted it             */
    const char *zKey;
    /* ---- filled by viki_claim(); never caller-set ---------------------- */
    char       *zJson;
    char       *zCompose;
} VikiClaim;
extern const struct VikiAssertVftbl vikiClaimVftbl;

/* Refuses a status outside k0..k4, and refuses a k0 with no falsifier.
** Both are write-time refusals for the same reason the ledger refuses a
** malformed due date: a record that is wrong in the direction of confidence
** is worse than a write that fails. */
VikiStatus viki_claim(VikiClaim *p, char *zIdOut);

/* ---- redaction -------------------------------------------------------
** The tombstone. Stores an assertion naming a target id, then destroys the
** target locally and everywhere it later merges. IRREVERSIBLE: the store
** becomes a 2P-Set, so a redacted id can never be re-added -- which is the
** requirement rather than a defect, since viki_forget alone is undone by the
** next merge (measured 2026-08-30: 4 -> forget -> 3 -> merge -> 4).
**
** zWhy and zBy are REQUIRED. An unexplained tombstone cannot be told from an
** accident by the peer it reaches, and nothing it took can be recovered.
**
** The body names an id and nothing else -- an id is a sha256 OF the content,
** so this propagates the instruction to destroy without propagating what is
** destroyed. */
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;
    const char *zTarget;      /* the id to destroy */
    const char *zWhy;         /* REQUIRED */
    const char *zBy;          /* REQUIRED */
    char       *zJson, *zCompose;
} VikiRedact;
extern const struct VikiAssertVftbl vikiRedactVftbl;
VikiStatus viki_redact(VikiRedact *p, char *zIdOut);

/* ---- walking the chain ----------------------------------------------- */
typedef struct {
    const char *zId, *zKind, *zTs, *zText, *zStatus, *zBecause, *zBy;
    int nDepth;     /* 0 is the assertion asked about                       */
    int bForward;   /* 0: what it replaced.  1: what replaced it.           */
} VikiChainRow;
typedef int (*viki_chain_row)(void *pApp, const VikiChainRow *pRow);

/* BOTH directions, nearest first, the subject itself at depth 0. Works for
** any assertion kind, not only claims -- a task retired by an arrival walks
** the same way. Returns VIKI_ENOTFOUND if the id is not in this diary. */
VikiStatus viki_why(const char *zId, viki_chain_row xRow, void *pApp);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_TRACE_H */
