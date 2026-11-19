/*
** novector.c -- REQ3/ENV-3: core built with VK_OMIT_VECTOR and without
** core/src/viki_vector.c links, and stores and searches by every other method.
** build-check.sh builds and runs it; that it links at all is half the test.
*/
#include <stdio.h>
#include <string.h>
#include "viki_store.h"

static VkBytes B(const char *z){ VkBytes b; b.p = (const uint8_t*)z; b.n = (uint32_t)strlen(z); return b; }

static int hits(VkStore *st, const VkSearchMethod *m, const char *zQ){
    VkSearch s; VkId id; int n = 0;
    if( vk_search_open(st, m, B(zQ), &s)!=VK_OK ){ vk_search_close(&s); return -1; }
    while( vk_search_next(&s, id)==VK_OK ) n++;
    vk_search_close(&s);
    return n;
}

static int64_t fixed_now(void *pCtx){ (void)pCtx; return 1798761600; }   /* 2027-01-01T00:00:00Z */

int main(void){
    static const VkClock clock = { fixed_now, 0 };
    static const VkSyncPolicy never = { VK_SYNC_NEVER, 0, 0 };
    sqlite3 *db = 0;
    VkStore *st = 0;
    VkGrant g;
    VkAssertion a;
    VkId id;
    int ok;
    g.principal = B("alice"); g.rights = VK_ERASE|VK_READ|VK_SUPERSEDE|VK_ORIGINATE;
    if( sqlite3_open(":memory:", &db)!=SQLITE_OK || vk_open(db, B("alice"), &g, 1, &clock, &never, &st)!=VK_OK ){
        printf("FAIL ENV-3 a store opens without vectors\n");
        return 1;
    }
    memset(&a, 0, sizeof(a));
    a.author = B("alice"); a.kind = B("note"); a.akey = B("k"); a.ts = B("2026-11-14T00:00:00Z");
    a.body = B("a needle in the hay");
    ok = vk_put(st, &a, id)==VK_OK && vk_bm25_enable(st)==VK_OK
      && hits(st, &vkGrep, "needle")==1 && hits(st, &vkRegex, "ne+dle")==1 && hits(st, &vkBm25, "needle")==1;
    vk_close(st);
    sqlite3_close(db);
    printf("%s ENV-3 a build without vectors stores and searches by grep, regex and bm25\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
