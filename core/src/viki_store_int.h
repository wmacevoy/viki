/* viki_store_int.h -- what viki_store.c shares with viki_search.c. */
#ifndef VIKI_STORE_INT_H
#define VIKI_STORE_INT_H

#include "viki_store.h"

#define VK_TOMBSTONE "tombstone"

/* An assertion counts only if its author held the right its frame needs
** (D-3). It is checked at fold time because rows arrive by merge, where a
** refusal is not available: stored and ignored (R-4a). */
#define VK_VALID(a) \
    "(CASE WHEN " a ".kind=CAST('" VK_TOMBSTONE "' AS BLOB) THEN vk_rights(" a ".author)&1" \
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

#ifdef VK_TEST_FAULTS
int vk_test_fault(void);
#endif

#endif
