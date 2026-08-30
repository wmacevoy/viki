/* viki_vcs.c -- file and checkin kinds. See viki_vcs.h for what was already
** there (version history) and what was actually missing (grouping).
**
** Writes through viki_put(), reads through viki_sql(), same two doors as
** every other subtype here.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viki_core.h"
#include "viki_vcs.h"

/* ---- file ------------------------------------------------------------ */
static const VikiType vikiFileType = { "file", 0, sizeof(VikiFile) };

/* akey IS THE PATH, and that is the whole design. Two assertions sharing a
** path are two versions of one document; the newer supersedes the older and
** viki_current(path) answers "what does this file say now". */
static const char *flKey (const VikiAssert *p){
    const VikiFile *f = (const VikiFile*)p;
    return f->zPath ? f->zPath : p->zId;
}
static const char *flRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *flText (const VikiAssert *p){
    const VikiFile *f = (const VikiFile*)p;
    return f->zCompose ? f->zCompose : (f->zContent ? f->zContent : "");
}
/* canon is the CONTENT, not a wrapper -- so two peers that store the same
** bytes at the same path with the same timestamp agree on the id, and a
** re-store of unchanged content is a no-op rather than a new version. */
static const char *flCanon(const VikiAssert *p){
    const VikiFile *f = (const VikiFile*)p;
    return f->zJson ? f->zJson : "";
}
const struct VikiAssertVftbl vikiFileVftbl = {
    &vikiFileType, flKey, flRank, flText, flCanon
};

struct bld { char *z; int ok; int n; };
static int bldRow(void *pApp, int nCol, const char *const *az){
    struct bld *b = (struct bld*)pApp;
    if( nCol < 2 ) return 0;
    b->n++;
    b->ok = (az[0] && az[0][0]=='1');
    if( az[1] ){ size_t k = strlen(az[1]); b->z = malloc(k+1);
                 if( b->z ) memcpy(b->z, az[1], k+1); }
    return 0;
}

VikiStatus viki_file(VikiFile *p, char *zIdOut){
    struct bld b; char *zSql; VikiStatus rc;
    if( !p || !p->zPath || !p->zPath[0] || !p->zContent ) return VIKI_EINVAL;
    p->vftbl = &vikiFileVftbl; p->zJson = 0; p->zCompose = 0;
    zSql = sqlite3_mprintf(
      "SELECT 1, json_object('kind','file','path',%Q,'content',%Q,'who',%Q)",
      p->zPath, p->zContent, p->zWho ? p->zWho : "");
    if( !zSql ) return VIKI_ENOMEM;
    memset(&b,0,sizeof b);
    rc = viki_sql(zSql, bldRow, &b); sqlite3_free(zSql);
    if( rc!=VIKI_OK || !b.z ){ free(b.z); return rc==VIKI_OK ? VIKI_ENOMEM : rc; }
    p->zJson = b.z;

    /* AUTO-VERSION. Unless the caller named a parent, this supersedes the
    ** current assertion on this path -- which is what makes `viki file` behave
    ** like a VCS rather than an append log.
    **
    ** AND STORING UNCHANGED CONTENT IS A NO-OP. Without this check a second
    ** store of identical bytes would chain a new version onto the old one
    ** (same content, different supersedes -> different id), so a nightly
    ** import would manufacture a version per run and the history would be
    ** noise. Compare canon; if equal, the file has not changed. */
    if( !p->zSupersedes ){
        char zCur[VIKI_ID_HEX+1]; char *zBody = 0;
        if( viki_current(p->zPath, zCur, &zBody)==VIKI_OK ){
            if( zBody && strcmp(zBody, p->zJson)==0 ){
                free(zBody); free(p->zJson); p->zJson = 0;
                if( zIdOut ) memcpy(zIdOut, zCur, VIKI_ID_HEX+1);
                return VIKI_OK;            /* unchanged: not a new version */
            }
            free(zBody);
            p->zSupersedes = sqlite3_mprintf("%s", zCur);
            if( !p->zSupersedes ){ free(p->zJson); p->zJson=0; return VIKI_ENOMEM; }
            p->zCompose = 0;               /* freed below with the rest */
        }
    }

    /* the path is IN the embedded text: a version you cannot retrieve by name
    ** is one the next reader has to already know about. */
    p->zCompose = sqlite3_mprintf("%s\n%s", p->zPath, p->zContent);
    if( !p->zCompose ){ free(p->zJson); p->zJson=0; return VIKI_ENOMEM; }
    rc = viki_put((VikiAssert*)p);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, p->zId, VIKI_ID_HEX+1);
    free(p->zJson); p->zJson=0;
    sqlite3_free(p->zCompose); p->zCompose=0;
    return rc;
}

/* ---- checkin --------------------------------------------------------- */
static const VikiType vikiCheckinType = { "checkin", 0, sizeof(VikiCheckin) };

/* A check-in competes on its BRANCH, so the newest check-in on a branch is
** what viki_current(branch) returns, and supersedes points at the parent. */
static const char *ckKey (const VikiAssert *p){
    const VikiCheckin *c = (const VikiCheckin*)p;
    return (c->zBranch && c->zBranch[0]) ? c->zBranch : "trunk";
}
static const char *ckRank (const VikiAssert *p){ return p->zTs ? p->zTs : ""; }
static const char *ckText (const VikiAssert *p){
    const VikiCheckin *c = (const VikiCheckin*)p;
    return c->zCompose ? c->zCompose : (c->zComment ? c->zComment : "");
}
static const char *ckCanon(const VikiAssert *p){
    const VikiCheckin *c = (const VikiCheckin*)p;
    return c->zJson ? c->zJson : "";
}
const struct VikiAssertVftbl vikiCheckinVftbl = {
    &vikiCheckinType, ckKey, ckRank, ckText, ckCanon
};

VikiStatus viki_checkin(VikiCheckin *p, char *zIdOut){
    struct bld b; char *zSql; VikiStatus rc; sqlite3_str *pM; char *zMembers;
    int i;
    if( !p || !p->zComment || !p->zComment[0] ) return VIKI_EINVAL;
    p->vftbl = &vikiCheckinVftbl; p->zJson = 0; p->zCompose = 0;

    /* MEMBERS ARE PART OF canon(), so a check-in naming a different set of
    ** file versions is a different check-in. Without that, two commits with
    ** the same message and time would collide and one would vanish. */
    pM = sqlite3_str_new(0);
    sqlite3_str_appendall(pM, "json_array(");
    for(i=0;i<p->nMember;i++)
        sqlite3_str_appendf(pM, "%s%Q", i?",":"", p->azMember[i] ? p->azMember[i] : "");
    sqlite3_str_appendall(pM, ")");
    zMembers = sqlite3_str_finish(pM);
    if( !zMembers ) return VIKI_ENOMEM;

    zSql = sqlite3_mprintf(
      "SELECT 1, json_object('kind','checkin','comment',%Q,'who',%Q,"
      "'branch',%Q,'members',%s)",
      p->zComment, p->zWho ? p->zWho : "",
      (p->zBranch && p->zBranch[0]) ? p->zBranch : "trunk", zMembers);
    sqlite3_free(zMembers);
    if( !zSql ) return VIKI_ENOMEM;
    memset(&b,0,sizeof b);
    rc = viki_sql(zSql, bldRow, &b); sqlite3_free(zSql);
    if( rc!=VIKI_OK || !b.z ){ free(b.z); return rc==VIKI_OK ? VIKI_ENOMEM : rc; }
    p->zJson = b.z;
    p->zCompose = sqlite3_mprintf("%s\n%d file(s)%s%s", p->zComment, p->nMember,
                                  (p->zWho && p->zWho[0]) ? "\nby: " : "",
                                  (p->zWho && p->zWho[0]) ? p->zWho : "");
    if( !p->zCompose ){ free(p->zJson); p->zJson=0; return VIKI_ENOMEM; }
    rc = viki_put((VikiAssert*)p);
    if( rc==VIKI_OK && zIdOut ) memcpy(zIdOut, p->zId, VIKI_ID_HEX+1);
    free(p->zJson); p->zJson=0;
    sqlite3_free(p->zCompose); p->zCompose=0;
    return rc;
}

struct memb { viki_member_row x; void *pApp; int n; int stop; };
static int membRow(void *pApp, int nCol, const char *const *az){
    struct memb *m = (struct memb*)pApp;
    if( m->stop || nCol < 1 || !az[0] ) return 0;
    if( m->x && m->x(m->pApp, m->n, az[0]) ) m->stop = 1;
    m->n++;
    return 0;
}

VikiStatus viki_checkin_members(const char *zId, viki_member_row xRow, void *pApp){
    struct memb m; char *zSql; VikiStatus rc;
    if( !zId || !zId[0] ) return VIKI_EINVAL;
    zSql = sqlite3_mprintf(
      "SELECT value FROM viki_assert, json_each(json_extract(body,'$.members'))"
      " WHERE id=%Q AND kind='checkin' AND json_valid(body)", zId);
    if( !zSql ) return VIKI_ENOMEM;
    memset(&m,0,sizeof m); m.x = xRow; m.pApp = pApp;
    rc = viki_sql(zSql, membRow, &m);
    sqlite3_free(zSql);
    if( rc!=VIKI_OK ) return rc;
    return m.n ? VIKI_OK : VIKI_ENOTFOUND;
}
