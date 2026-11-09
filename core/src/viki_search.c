/*
** viki_search.c -- search methods. Each is one const table (viki_store.h).
** A method's "not found" means nothing unless it has been seen to find
** something, so every search runs the method's self-check first (Q-3).
*/
#include "viki_store_int.h"
#include <string.h>

/* instr() compares bytes when both sides are BLOBs (A-4a). */
#define GREP_MATCH "instr(body,?1)>0"

static VkStatus grep_open(VkStore *p, VkBytes q, sqlite3_stmt **ppOut){
    int rc;
    if( q.n==0 ) return vk_refuse(p, VK_EINVAL, "Q-6: grep needs a non-empty query");
    rc = vk_prepare(p,
        "SELECT a.id FROM assertion a WHERE a.body IS NOT NULL AND "
        VK_VALID("a") " AND instr(a.body,?1)>0", ppOut);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(*ppOut, 1, q);
    return VK_OK;
}

static VkStatus grep_probe(VkStore *p, const char *zNeedle, int *pnHit){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p,
        "SELECT 1 FROM (SELECT x'00ff706c616e746564' AS body) WHERE " GREP_MATCH, &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    sqlite3_bind_blob(pSt, 1, zNeedle, 7, SQLITE_STATIC);
    rc = vk_step(pSt);
    *pnHit = rc==SQLITE_ROW;
    if( rc!=SQLITE_ROW && rc!=SQLITE_DONE ){
        VkStatus st = vk_sql_status(p, rc);
        sqlite3_finalize(pSt);
        return st;
    }
    sqlite3_finalize(pSt);
    return VK_OK;
}

static VkStatus grep_selfcheck(VkStore *p){
    int nHit = 0;
    VkStatus st = grep_probe(p, "planted", &nHit);
    if( st!=VK_OK ) return st;
    if( !nHit ) return VK_BLIND;
    st = grep_probe(p, "missing", &nHit);
    if( st!=VK_OK ) return st;
    return nHit ? VK_BLIND : VK_OK;
}

const VkSearchMethod vkGrep = { "grep", 1, grep_open, grep_selfcheck };

VkStatus vk_search_open(VkStore *p, const VkSearchMethod *m, VkBytes q, VkSearch *s){
    VkStatus st;
    s->p = p; s->pMethod = m; s->pSt = 0;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: search requires r");
    st = m->selfcheck(p);
    if( st!=VK_OK ) return st;
    return m->open(p, q, &s->pSt);
}

VkStatus vk_search_next(VkSearch *s, VkId out){
    int rc = vk_step(s->pSt);
    if( rc==SQLITE_DONE ) return VK_DONE;
    if( rc!=SQLITE_ROW ) return vk_sql_status(s->p, rc);
    memcpy(out, sqlite3_column_blob(s->pSt, 0), VK_ID_BYTES);
    return VK_OK;
}

VkStatus vk_search_coverage(VkSearch *s, VkCoverage *out){
    sqlite3_stmt *pSt = 0;
    VkStatus st = VK_OK;
    int rc = vk_prepare(s->p,
        "SELECT count(*) FROM assertion a WHERE a.body IS NOT NULL AND " VK_VALID("a"), &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(s->p, rc);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        out->zMethod = s->pMethod->zName;
        out->bExhaustive = s->pMethod->bExhaustive;
        out->nSearched = sqlite3_column_int64(pSt, 0);
    }else{
        st = vk_sql_status(s->p, rc);
    }
    sqlite3_finalize(pSt);
    return st;
}

void vk_search_close(VkSearch *s){
    sqlite3_finalize(s->pSt);
    s->pSt = 0;
}
