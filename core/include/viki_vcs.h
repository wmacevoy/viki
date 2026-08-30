/* viki_vcs.h -- versioned content, and the one thing fossil had that viki did not.
**
** MEASURED FIRST, 2026-08-30: viki ALREADY had version control. Supersession
** is the version chain -- put two assertions on one subject and both are
** stored, only the newest is current, and `viki why` walks the chain in both
** directions newest-first. History, current-version and rollback were never
** missing; they fall out of machinery that already existed and was already
** tested.
**
** So a versioned document is a KIND, not a feature:
**
**   file      akey = path, canon = content. Superseding the previous
**             assertion on that path IS that path's history. `viki why`
**             on any version is `fossil timeline` for that one file.
**
** WHAT WAS ACTUALLY MISSING IS GROUPING. Fossil's check-in bundles several
** file versions into one atomic thing you can name, and viki had no bundle --
** every assertion stood alone. That is the whole gap, and it is one concept:
**
**   checkin   supersedes its PARENT CHECK-IN, so `viki why` walks commit
**             history for free. Its body names the member file ids. Two
**             children of one parent is a branch, and needs no new code.
**
** WHAT IS STILL NOT FOSSIL, said plainly rather than discovered later: no
** diff (blobs are content-addressed; use any differ), no rename detection,
** and a check-in names its members rather than a tree -- so a directory is
** not an object here.
*/
#ifndef VIKI_VCS_H
#define VIKI_VCS_H
#include "viki_core.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- a versioned document ------------------------------------------- */
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;   /* the previous version OF THIS PATH */
    const char *zPath;         /* REQUIRED -- becomes akey */
    const char *zContent;      /* REQUIRED -- becomes canon, so the id is
                               ** the hash of the content in its path */
    const char *zWho;
    char       *zJson, *zCompose;
} VikiFile;
extern const struct VikiAssertVftbl vikiFileVftbl;
VikiStatus viki_file(VikiFile *p, char *zIdOut);

/* ---- a check-in: the group ------------------------------------------ */
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;   /* the PARENT check-in, or NULL for the root */
    const char *zComment;      /* REQUIRED */
    const char *zWho;
    const char *zBranch;
    const char **azMember;     /* file assertion ids */
    int          nMember;
    char       *zJson, *zCompose;
} VikiCheckin;
extern const struct VikiAssertVftbl vikiCheckinVftbl;
VikiStatus viki_checkin(VikiCheckin *p, char *zIdOut);

/* The member ids of a check-in, in order. Caller frees nothing; the callback
** borrows. */
typedef int (*viki_member_row)(void *pApp, int nIx, const char *zMemberId);
VikiStatus viki_checkin_members(const char *zCheckinId,
                                viki_member_row xRow, void *pApp);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_VCS_H */
