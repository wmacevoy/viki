/*
** viki_search.c -- search methods. Each is one const table (viki_store.h).
** A method's "not found" means nothing unless it has been seen to find
** something, so every search runs the method's self-check first (Q-3).
*/
#include "viki_store_int.h"
#include <string.h>

/* instr() compares bytes when both sides are BLOBs (A-4a). */
#define GREP_MATCH "instr(body,?1)>0"

/* Every method's rows: the hit's id, then the node of the reader's outline it
** is in, or NULL for the whole assertion (OL-4). */

/* grep's node is the smallest one holding the first match. */
static VkStatus grep_open(VkStore *p, VkBytes q, sqlite3_stmt **ppOut){
    int rc;
    if( q.n==0 ) return vk_refuse(p, VK_EINVAL, "Q-6: grep needs a non-empty query");
    rc = vk_prepare(p,
        "SELECT h.id, (SELECT n.rowid FROM vk_onode n WHERE n.target=h.id AND n.author=?2"
        "  AND n.lo<=h.pos AND n.hi>=h.pos+length(?1) ORDER BY n.depth DESC LIMIT 1)"
        " FROM (SELECT a.id, instr(" VK_BODY("a.") ",?1)-1 AS pos FROM assertion a"
        "  WHERE a.body IS NOT NULL AND " VK_VALID("a") ") h WHERE h.pos>=0", ppOut);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(*ppOut, 1, q);
    vk_bind_bytes(*ppOut, 2, vk_outline_author(p));
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

/* ---- regex (Q-6) ----
** vk_regexp (viki_regexp.c) over every readable body, exhaustive like grep. The
** engine cannot say where a match is, so a hit's node is the deepest node of the
** reader's outline whose span matches on its own. */
static VkStatus regex_open(VkStore *p, VkBytes q, sqlite3_stmt **ppOut){
    sqlite3_stmt *pSt = 0;
    int rc;
    if( q.n==0 ) return vk_refuse(p, VK_EINVAL, "Q-6: regex needs a non-empty pattern");
    /* A pattern that does not compile is the caller's error, said now, not a
    ** failed step in the middle of the hits. */
    rc = vk_prepare(p, "SELECT vk_regexp(?1, '')", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, q);
    rc = vk_step(pSt);
    if( rc!=SQLITE_ROW ){
        VkStatus st = rc==SQLITE_ERROR
            ? vk_refuse(p, VK_EINVAL, "Q-6: the regex does not compile") : vk_sql_status(p, rc);
        sqlite3_finalize(pSt);
        return st;
    }
    sqlite3_finalize(pSt);
    rc = vk_prepare(p,
        "SELECT h.id, (SELECT n.rowid FROM vk_onode n WHERE n.target=h.id AND n.author=?2"
        "  AND vk_regexp(?1, substr(h.b, n.lo+1, n.hi-n.lo)) ORDER BY n.depth DESC LIMIT 1)"
        " FROM (SELECT a.id, " VK_BODY("a.") " AS b FROM assertion a"
        "  WHERE a.body IS NOT NULL AND " VK_VALID("a") ") h WHERE vk_regexp(?1, h.b)", ppOut);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(*ppOut, 1, q);
    vk_bind_bytes(*ppOut, 2, vk_outline_author(p));
    return VK_OK;
}

static VkStatus regex_probe(VkStore *p, const char *zPattern, int *pnHit){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT vk_regexp(?1, x'61200020706c616e74656420776f7264')", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    sqlite3_bind_text(pSt, 1, zPattern, -1, SQLITE_STATIC);
    rc = vk_step(pSt);
    if( rc!=SQLITE_ROW ){
        VkStatus st = vk_sql_status(p, rc);
        sqlite3_finalize(pSt);
        return st;
    }
    *pnHit = sqlite3_column_int(pSt, 0);
    sqlite3_finalize(pSt);
    return VK_OK;
}

/* The planted body is "a \0 planted word": a match past a NUL byte, and an
** anchor that must refuse, so an engine that stops at the NUL or ignores "^"
** reads as blind. */
static VkStatus regex_selfcheck(VkStore *p){
    int nHit = 0;
    VkStatus st = regex_probe(p, "pl[a-z]+ed\\b", &nHit);
    if( st!=VK_OK ) return st;
    if( !nHit ) return VK_BLIND;
    st = regex_probe(p, "^planted", &nHit);
    if( st!=VK_OK ) return st;
    return nHit ? VK_BLIND : VK_OK;
}

const VkSearchMethod vkRegex = { "regex", 1, regex_open, regex_selfcheck };

VkStatus vk_search_open(VkStore *p, const VkSearchMethod *m, VkBytes q, VkSearch *s){
    VkStatus st;
    s->p = p; s->pMethod = m; s->pSt = 0; s->pNode = 0;
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

VkStatus vk_search_hit(VkSearch *s, VkHit *out){
    int rc;
    memset(out, 0, sizeof(*out));
    if( s->pSt && sqlite3_column_count(s->pSt)>=4 && sqlite3_column_type(s->pSt, 2)!=SQLITE_NULL ){
        out->bRange = 1;                       /* vector: the span that matched */
        out->lo = sqlite3_column_int64(s->pSt, 2);
        out->hi = sqlite3_column_int64(s->pSt, 3);
        return VK_OK;
    }
    if( !s->pSt || sqlite3_column_count(s->pSt)<2 || sqlite3_column_type(s->pSt, 1)==SQLITE_NULL ) return VK_OK;
    if( !s->pNode ){
        rc = vk_prepare(s->p, "SELECT lo, hi, depth, path FROM vk_onode WHERE rowid=?1", &s->pNode);
        if( rc!=SQLITE_OK ) return vk_sql_status(s->p, rc);
    }
    sqlite3_reset(s->pNode);
    sqlite3_bind_int64(s->pNode, 1, sqlite3_column_int64(s->pSt, 1));
    rc = vk_step(s->pNode);
    if( rc==SQLITE_DONE ) return VK_OK;      /* the node went with a refold mid-search: whole */
    if( rc!=SQLITE_ROW ) return vk_sql_status(s->p, rc);
    out->bNode = 1;
    out->lo = sqlite3_column_int64(s->pNode, 0);
    out->hi = sqlite3_column_int64(s->pNode, 1);
    out->depth = sqlite3_column_int(s->pNode, 2);
    out->path.p = sqlite3_column_text(s->pNode, 3);
    out->path.n = (uint32_t)sqlite3_column_bytes(s->pNode, 3);
    return VK_OK;
}

VkStatus vk_search_coverage(VkSearch *s, VkCoverage *out){
    sqlite3_stmt *pSt = 0;
    VkStatus st = VK_OK;
    /* E-2: what the method actually holds for each readable row. grep and regex
    ** read the bodies themselves; bm25 holds what its index holds; vector holds
    ** a row only with a vector under the chosen model. */
    const char *zHeld = "1";
#ifndef VK_OMIT_VECTOR   /* ENV-3 */
    int bVec = s->pMethod==&vkVector;
#else
    int bVec = 0;
#endif
    char zSql[600];
    int rc;
    if( bVec ) zHeld = "EXISTS(SELECT 1 FROM vk_vec v WHERE v.seq=a.seq AND v.model=?1)";
    else if( s->pMethod==&vkBm25 ) zHeld = "EXISTS(SELECT 1 FROM vk_bm25_docsize d WHERE d.id=a.seq)";
    sqlite3_snprintf(sizeof zSql, zSql,
        "SELECT coalesce(sum(%s),0), %s, count(*) FROM assertion a WHERE a.body IS NOT NULL AND " VK_VALID("a"),
        zHeld, bVec ? "0"
          : "coalesce(sum(NOT EXISTS(SELECT 1 FROM vk_onode o WHERE o.target=a.id AND o.author=?1)),0)");
    rc = vk_prepare(s->p, zSql, &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(s->p, rc);
    vk_bind_bytes(pSt, 1, bVec ? vk_vector_model(s->p) : vk_outline_author(s->p));
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ){
        out->zMethod = s->pMethod->zName;
        out->bExhaustive = s->pMethod->bExhaustive;
        out->nSearched = sqlite3_column_int64(pSt, 0);
        out->nWhole = sqlite3_column_int64(pSt, 1);
        out->nUnread = sqlite3_column_int64(pSt, 2) - out->nSearched;
    }else{
        st = vk_sql_status(s->p, rc);
    }
    sqlite3_finalize(pSt);
    return st;
}

void vk_search_close(VkSearch *s){
    sqlite3_finalize(s->pSt);
    sqlite3_finalize(s->pNode);
    s->pSt = 0;
    s->pNode = 0;
}


/* ---- bm25 (Q-6) ----
** The index is an FTS5 table over assertion's own bodies (external content):
** it keeps no second copy of the text. secure-delete (SQLite 3.44+) removes an
** erased body's words from the index's pages, not only from its answers (Q-5,
** W-3). A contentless_delete table was tried first and does not: it marks the
** row deleted and leaves its terms readable in the leaf (core/DESIGN_v3.md).
** Triggers keep the index current through every path that writes, erases or
** forgets. Each hands FTS5 the old body itself, so nothing re-reads assertion
** after the row has changed: the order trap src/ fell into (CONTRIBUTING.md,
** chunk_fts) cannot happen here. */

/* Every row is in the index, an erased one as an empty document: FTS5 checks an
** external-content index row for row against its content (integrity-check).
** The content is a view of each body as written, so a body stored as a delta
** (DS-1) is indexed as its text, and re-encoding one, whole to delta or back,
** changes nothing the index sees: only writing, erasing and deleting do. */
static const char *const azBm25[] = {
    "CREATE VIEW vk_bm25_src(seq, body) AS SELECT seq, " VK_BODY("") " FROM assertion"
    " UNION ALL SELECT -n.rowid, " VK_NODE_TEXT("n.rowid", VK_TARGET_BODY("n.target")) " FROM vk_onode n",
    "CREATE VIRTUAL TABLE vk_bm25 USING fts5(body, content='vk_bm25_src', content_rowid='seq')",
    "INSERT INTO vk_bm25(vk_bm25, rank) VALUES('secure-delete', 1)",
    /* A target's nodes were indexed empty while it was absent: now its text. */
    "CREATE TRIGGER vk_bm25_ai AFTER INSERT ON assertion"
    " BEGIN INSERT INTO vk_bm25(rowid, body) VALUES(new.seq, " VK_BODY("new.") ");"
    " INSERT INTO vk_bm25(vk_bm25, rowid, body) SELECT 'delete', -n.rowid, NULL FROM vk_onode n"
    "  WHERE n.target=new.id AND new.log=x'';"
    " INSERT INTO vk_bm25(rowid, body) SELECT -n.rowid, " VK_NODE_TEXT("n.rowid", VK_BODY("new.")) " FROM vk_onode n"
    "  WHERE n.target=new.id AND new.log=x''; END",
    "CREATE TRIGGER vk_bm25_au AFTER UPDATE OF body ON assertion"
    " WHEN old.body IS NOT NULL AND new.body IS NULL"
    " BEGIN INSERT INTO vk_bm25(vk_bm25, rowid, body) VALUES('delete', old.seq, " VK_BODY("old.") ");"
    " INSERT INTO vk_bm25(rowid, body) VALUES(new.seq, NULL);"
    " INSERT INTO vk_bm25(vk_bm25, rowid, body) SELECT 'delete', -n.rowid, " VK_NODE_TEXT("n.rowid", VK_BODY("old.")) ""
    "  FROM vk_onode n WHERE n.target=old.id AND old.log=x'';"
    " INSERT INTO vk_bm25(rowid, body) SELECT -n.rowid, NULL FROM vk_onode n WHERE n.target=old.id AND old.log=x''; END",
    "CREATE TRIGGER vk_bm25_ad AFTER DELETE ON assertion"
    " BEGIN INSERT INTO vk_bm25(vk_bm25, rowid, body) VALUES('delete', old.seq, " VK_BODY("old.") ");"
    " INSERT INTO vk_bm25(vk_bm25, rowid, body) SELECT 'delete', -n.rowid, " VK_NODE_TEXT("n.rowid", VK_BODY("old.")) ""
    "  FROM vk_onode n WHERE n.target=old.id AND old.log=x'';"
    " INSERT INTO vk_bm25(rowid, body) SELECT -n.rowid, NULL FROM vk_onode n WHERE n.target=old.id AND old.log=x''; END",
    "INSERT INTO vk_bm25(rowid, body) SELECT seq, " VK_BODY("") " FROM assertion",
    "INSERT INTO vk_bm25(rowid, body) SELECT -n.rowid, " VK_NODE_TEXT("n.rowid", VK_TARGET_BODY("n.target")) " FROM vk_onode n",
};

static int run(VkStore *p, const char *zSql){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, zSql, &pSt);
    if( rc==SQLITE_OK ){
        rc = vk_step(pSt);
        if( rc==SQLITE_DONE || rc==SQLITE_ROW ) rc = SQLITE_OK;
    }
    sqlite3_finalize(pSt);
    return rc;
}

/* *pb is 1 if the index exists. */
static int bm25_exists(VkStore *p, int *pb){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT 1 FROM sqlite_schema WHERE type='table' AND name='vk_bm25'", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    rc = vk_step(pSt);
    *pb = rc==SQLITE_ROW;
    sqlite3_finalize(pSt);
    return (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
}

int vk_bm25_on(VkStore *p, int *pb){ return bm25_exists(p, pb); }

VkStatus vk_bm25_enable(VkStore *p){
    int b = 0, rc;
    size_t i;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: an index of what is read requires r");
    rc = bm25_exists(p, &b);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( b ) return VK_OK;
    rc = run(p, "SAVEPOINT vk_bm25");
    for(i=0; rc==SQLITE_OK && i<sizeof(azBm25)/sizeof(azBm25[0]); i++) rc = run(p, azBm25[i]);
    if( rc==SQLITE_OK ) rc = run(p, "RELEASE vk_bm25");
    if( rc==SQLITE_OK ) return VK_OK;
    {
        /* The undo goes straight to SQLite: it must not be one of the failures the test build injects. */
        VkStatus st = vk_sql_status(p, rc);
        sqlite3_exec(vk_store_db(p), "ROLLBACK TO vk_bm25; RELEASE vk_bm25", 0, 0, 0);
        return st;
    }
}

/* The query's words, each quoted and OR'd: FTS5's MATCH is an AND, and
** its syntax is not the caller's to run. *pz is sqlite3_malloc'd. */
static int bm25_query(VkBytes q, char **pz){
    sqlite3_str *s = sqlite3_str_new(0);
    uint32_t i = 0, nWord = 0;
    while( i<q.n ){
        uint32_t j;
        while( i<q.n && (q.p[i]==' ' || q.p[i]=='\t' || q.p[i]=='\n' || q.p[i]=='\r') ) i++;
        if( i==q.n ) break;
        if( nWord++ ) sqlite3_str_appendall(s, " OR ");
        sqlite3_str_appendchar(s, 1, '"');
        for(j=i; j<q.n && !(q.p[j]==' ' || q.p[j]=='\t' || q.p[j]=='\n' || q.p[j]=='\r'); j++){
            if( q.p[j]=='"' ) sqlite3_str_appendchar(s, 1, '"');
            sqlite3_str_appendchar(s, 1, (char)q.p[j]);
        }
        sqlite3_str_appendchar(s, 1, '"');
        i = j;
    }
    if( sqlite3_str_errcode(s) ){ sqlite3_free(sqlite3_str_finish(s)); return SQLITE_NOMEM; }
    *pz = sqlite3_str_finish(s);
    if( nWord==0 ){ sqlite3_free(*pz); *pz = 0; return SQLITE_EMPTY; }
    return SQLITE_OK;
}

static VkStatus bm25_open(VkStore *p, VkBytes q, sqlite3_stmt **ppOut){
    char *z = 0;
    int rc = bm25_query(q, &z);
    if( rc==SQLITE_EMPTY ) return vk_refuse(p, VK_EINVAL, "Q-6: bm25 needs at least one word");
    if( rc!=SQLITE_OK ) return VK_ENOMEM;
    /* Whole bodies with no outline by the reader's author, and that author's
    ** nodes, ranked together: one index, so their scores compare (OL-4). */
    rc = vk_prepare(p,
        "SELECT id, node FROM ("
        " SELECT a.id AS id, NULL AS node, bm25(vk_bm25) AS score FROM vk_bm25 JOIN assertion a ON a.seq=vk_bm25.rowid"
        "  WHERE vk_bm25 MATCH ?1 AND vk_bm25.rowid>0 AND a.body IS NOT NULL AND " VK_VALID("a")
        "  AND NOT EXISTS(SELECT 1 FROM vk_onode o WHERE o.target=a.id AND o.author=?2)"
        " UNION ALL"
        " SELECT n.target, n.rowid, bm25(vk_bm25) FROM vk_bm25 JOIN vk_onode n ON n.rowid=-vk_bm25.rowid"
        "  JOIN assertion a ON a.id=n.target AND a.log=x''"
        "  WHERE vk_bm25 MATCH ?1 AND vk_bm25.rowid<0 AND n.author=?2 AND a.body IS NOT NULL AND " VK_VALID("a")
        ") ORDER BY score, id, node", ppOut);
    if( rc!=SQLITE_OK ){ sqlite3_free(z); return vk_sql_status(p, rc); }
    sqlite3_bind_text(*ppOut, 1, z, -1, sqlite3_free);
    vk_bind_bytes(*ppOut, 2, vk_outline_author(p));
    return VK_OK;
}

/* A planted row in a temporary index of its own, queried through the same
** query builder, so a broken builder or a missing FTS5 reads as blind. */
static VkStatus bm25_probe(VkStore *p, const char *zWord, int *pnHit){
    sqlite3_stmt *pSt = 0;
    char *z = 0;
    VkBytes q;
    int rc;
    q.p = (const uint8_t*)zWord; q.n = (uint32_t)strlen(zWord);
    rc = bm25_query(q, &z);
    if( rc!=SQLITE_OK ) return VK_ENOMEM;
    rc = vk_prepare(p, "SELECT count(*) FROM temp.vk_bm25_probe WHERE vk_bm25_probe MATCH ?1", &pSt);
    if( rc!=SQLITE_OK ){ sqlite3_free(z); return vk_sql_status(p, rc); }
    sqlite3_bind_text(pSt, 1, z, -1, sqlite3_free);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ) *pnHit = sqlite3_column_int(pSt, 0);
    sqlite3_finalize(pSt);
    return rc==SQLITE_ROW ? VK_OK : vk_sql_status(p, rc);
}

static VkStatus bm25_selfcheck(VkStore *p){
    int b = 0, nHit = 0, rc;
    VkStatus st;
    rc = bm25_exists(p, &b);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( !b ) return VK_BLIND;                    /* not yet indexed (Q-2) */
    rc = run(p, "CREATE VIRTUAL TABLE IF NOT EXISTS temp.vk_bm25_probe USING fts5(b)");
    if( rc==SQLITE_OK ) rc = run(p, "INSERT INTO temp.vk_bm25_probe(rowid, b)"
                                    " SELECT 1, 'a planted word' WHERE NOT EXISTS (SELECT 1 FROM temp.vk_bm25_probe)");
    if( rc!=SQLITE_OK ) return VK_BLIND;
    st = bm25_probe(p, "planted", &nHit);
    if( st!=VK_OK ) return st;
    if( nHit!=1 ) return VK_BLIND;
    st = bm25_probe(p, "missing", &nHit);
    if( st!=VK_OK ) return st;
    return nHit ? VK_BLIND : VK_OK;
}

const VkSearchMethod vkBm25 = { "bm25", 0, bm25_open, bm25_selfcheck };

