/* viki_store_int.h -- what viki_store.c shares with viki_search.c. */
#ifndef VIKI_STORE_INT_H
#define VIKI_STORE_INT_H

#include "viki_store.h"

#define VK_TOMBSTONE "tombstone"
#define VK_RANGE     "tombstone.range"
#define VK_OUTLINE   "outline"
#define VK_TOMB_KINDS "(CAST('" VK_TOMBSTONE "' AS BLOB),CAST('" VK_RANGE "' AS BLOB))"

/* An assertion counts only if its author held the right its frame needs
** (D-3). It is checked at fold time because rows arrive by merge, where a
** refusal is not available: stored and ignored (R-4a). */
#define VK_VALID(a) \
    "(CASE WHEN " a ".kind IN " VK_TOMB_KINDS " THEN vk_rights(" a ".author)&1" \
    " WHEN EXISTS(SELECT 1 FROM edge e0 WHERE e0.child=" a ".id AND e0.rel=0)" \
    " THEN vk_rights(" a ".author)&4 ELSE vk_rights(" a ".author)&8 END)"

/* Every SQLite prepare and step in the library goes through these, so the
** test build can fail each one in turn. */
int  vk_prepare(VkStore *p, const char *zSql, sqlite3_stmt **ppOut);
int  vk_step(sqlite3_stmt *pSt);
VkStatus vk_sql_status(VkStore *p, int rc);
VkStatus vk_refuse(VkStore *p, VkStatus rc, const char *zWhy);
void vk_bind_bytes(sqlite3_stmt *pSt, int i, VkBytes b);
int  vk_has_right(const VkStore *p, int right);
sqlite3 *vk_store_db(VkStore *p);

/* DS-1: the older body tgt as a delta from the newer src, and back. Outputs
** are sqlite3_malloc'd. apply returns SQLITE_CORRUPT for a malformed delta. */
int vk_delta_make(const uint8_t *src, uint32_t nSrc, const uint8_t *tgt, uint32_t nTgt,
                  uint8_t **pOut, uint32_t *pnOut);
int vk_delta_apply(const uint8_t *src, uint32_t nSrc, const uint8_t *d, uint32_t nD,
                   uint8_t **pOut, uint32_t *pnOut);
/* A body as stored: whole when dbase is NULL, else a delta from row dbase. */
#define VK_BODY(t) "vk_body(" t "body," t "dbase)"

/* OL-4. The target's body, by id (outlines name unlogged targets). */
#define VK_TARGET_BODY(idExpr) \
    "(SELECT " VK_BODY("tb.") " FROM assertion tb WHERE tb.id=" idExpr " AND tb.log=x'')"
/* A node's own text: its segments of the given body, in order. */
#define VK_NODE_TEXT(nodeExpr, bodyExpr) \
    "(SELECT group_concat(substr(" bodyExpr ", s.lo+1, s.hi-s.lo), '' ORDER BY s.lo)" \
    " FROM vk_oseg s WHERE s.node=" nodeExpr ")"
VkBytes vk_outline_author(const VkStore *p);
VkBytes vk_vector_model(const VkStore *p);
/* vk_cosine(a, b): the cosine of two float32 vectors; NULL if their lengths differ. */
void vk_cosine_func(sqlite3_context *ctx, int argc, sqlite3_value **argv);
int vk_has_held_body(VkStore *p, const uint8_t *id, sqlite3_int64 *pSeq, sqlite3_int64 *pLen);
int vk_bm25_on(VkStore *p, int *pb);   /* *pb is 1 if the bm25 index exists */

#ifdef VK_TEST_FAULTS
int vk_test_fault(void);
#endif

#endif
