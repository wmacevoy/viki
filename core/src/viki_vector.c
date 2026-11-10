/*
** viki_vector.c -- the vector search method (Q-6). core embeds nothing: the
** host runs the model and hands over vectors, so core stays free of a model
** runtime (ENV-3, SHAPE-9) and opens no file. What core keeps is the
** projection, its deletion with the body (W-3), and the nearest-neighbour
** ranking, by cosine, over every vector under the chosen model.
**
** The ranking is a scan: one cosine per stored vector. At 384 dimensions and
** tens of thousands of spans that is milliseconds; an index is a later choice,
** made when a diary is measured to need one.
*/
#include "viki_store_int.h"
#include <math.h>
#include <string.h>

/* Little-endian float32 to float, whatever the host's order. */
static float f32_at(const unsigned char *b){
    uint32_t u = (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void vk_cosine_func(sqlite3_context *ctx, int argc, sqlite3_value **argv){
    const unsigned char *a = sqlite3_value_blob(argv[0]), *b = sqlite3_value_blob(argv[1]);
    int n = sqlite3_value_bytes(argv[0]), i;
    double dot = 0, na = 0, nb = 0;
    (void)argc;
    if( !a || !b || n!=sqlite3_value_bytes(argv[1]) || n%4 ){ sqlite3_result_null(ctx); return; }
    for(i=0; i<n; i+=4){
        double x = f32_at(a+i), y = f32_at(b+i);
        dot += x*y; na += x*x; nb += y*y;
    }
    sqlite3_result_double(ctx, (na>0 && nb>0) ? dot/(sqrt(na)*sqrt(nb)) : 0.0);
}

/* The dimension of the model's vectors so far, or 0 if it has none. */
static int model_dim(VkStore *p, VkBytes model, int64_t *pDim){
    sqlite3_stmt *pSt = 0;
    int rc = vk_prepare(p, "SELECT length(v)/4 FROM vk_vec WHERE model=?1 LIMIT 1", &pSt);
    if( rc!=SQLITE_OK ) return rc;
    vk_bind_bytes(pSt, 1, model);
    rc = vk_step(pSt);
    *pDim = rc==SQLITE_ROW ? sqlite3_column_int64(pSt, 0) : 0;
    sqlite3_finalize(pSt);
    return (rc==SQLITE_ROW || rc==SQLITE_DONE) ? SQLITE_OK : rc;
}

VkStatus vk_vector_put(VkStore *p, const VkId target, VkBytes model, uint32_t lo, uint32_t hi,
                       const float *v, uint32_t dim){
    sqlite3_stmt *pSt = 0;
    sqlite3_int64 seq = 0, len = 0;
    int64_t have = 0;
    unsigned char *blob;
    uint32_t i;
    int rc, found;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: an index of what is read requires r");
    if( model.n==0 || model.n>VK_MAX_FIELD ) return vk_refuse(p, VK_EINVAL, "Q-6: a vector names its model");
    if( dim==0 || dim>VK_MAX_DIM ) return vk_refuse(p, VK_EINVAL, "Q-6: a vector's dimension is 1 to VK_MAX_DIM");
    if( lo>=hi ) return vk_refuse(p, VK_EINVAL, "Q-6: a vector's span is not empty");
    found = vk_has_held_body(p, target, &seq, &len);
    if( found<0 ) return vk_sql_status(p, -found);
    if( !found ) return vk_refuse(p, VK_NOTFOUND, "Q-6: a vector is of a held, unerased body");
    if( (sqlite3_int64)hi>len ) return vk_refuse(p, VK_EINVAL, "Q-6: a vector's span lies inside its body");
    rc = model_dim(p, model, &have);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( have && have!=(int64_t)dim ) return vk_refuse(p, VK_EINVAL, "Q-6: one model, one dimension");
    blob = sqlite3_malloc((int)dim*4);
    if( !blob ) return VK_ENOMEM;
    for(i=0; i<dim; i++){
        uint32_t u;
        memcpy(&u, &v[i], 4);
        blob[4*i] = (unsigned char)u; blob[4*i+1] = (unsigned char)(u>>8);
        blob[4*i+2] = (unsigned char)(u>>16); blob[4*i+3] = (unsigned char)(u>>24);
    }
    rc = vk_prepare(p, "INSERT OR REPLACE INTO vk_vec(model, seq, lo, hi, v) VALUES(?1,?2,?3,?4,?5)", &pSt);
    if( rc!=SQLITE_OK ){ sqlite3_free(blob); return vk_sql_status(p, rc); }
    vk_bind_bytes(pSt, 1, model);
    sqlite3_bind_int64(pSt, 2, seq);
    sqlite3_bind_int64(pSt, 3, lo);
    sqlite3_bind_int64(pSt, 4, hi);
    sqlite3_bind_blob(pSt, 5, blob, (int)dim*4, sqlite3_free);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? VK_OK : vk_sql_status(p, rc);
}

VkStatus vk_vector_drop(VkStore *p, const VkId target, VkBytes model){
    sqlite3_stmt *pSt = 0;
    sqlite3_int64 seq = 0, len = 0;
    int rc, found;
    if( !vk_has_right(p, VK_READ) ) return vk_refuse(p, VK_EDENIED, "D-3a0: an index of what is read requires r");
    found = vk_has_held_body(p, target, &seq, &len);
    if( found<0 ) return vk_sql_status(p, -found);
    if( !found ) return vk_refuse(p, VK_NOTFOUND, "Q-6: a vector is of a held, unerased body");
    rc = vk_prepare(p, "DELETE FROM vk_vec WHERE model=?1 AND seq=?2", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(pSt, 1, model);
    sqlite3_bind_int64(pSt, 2, seq);
    rc = vk_step(pSt);
    sqlite3_finalize(pSt);
    return rc==SQLITE_DONE ? VK_OK : vk_sql_status(p, rc);
}

/* Rows: the hit's id, no outline node, then the span (vk_search_hit). */
static VkStatus vector_open(VkStore *p, VkBytes q, sqlite3_stmt **ppOut){
    int64_t dim = 0;
    int rc = model_dim(p, vk_vector_model(p), &dim);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( q.n==0 || q.n%4 || (int64_t)(q.n/4)!=dim )
        return vk_refuse(p, VK_EINVAL, "Q-6: the query is a vector of the model's dimension");
    rc = vk_prepare(p,
        "SELECT a.id, NULL, v.lo, v.hi FROM vk_vec v JOIN assertion a ON a.seq=v.seq"
        " WHERE v.model=?2 AND a.body IS NOT NULL AND " VK_VALID("a")
        " ORDER BY vk_cosine(v.v, ?1) DESC, a.seq, v.lo", ppOut);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    vk_bind_bytes(*ppOut, 1, q);
    vk_bind_bytes(*ppOut, 2, vk_vector_model(p));
    return VK_OK;
}

/* Blind with no model chosen, or none of its vectors held (Q-2). Otherwise the
** ranking itself is checked: a planted vector must rank its twin above an
** orthogonal one (Q-3). */
static VkStatus vector_selfcheck(VkStore *p){
    sqlite3_stmt *pSt = 0;
    int64_t dim = 0;
    int rc, ok = 0;
    if( vk_vector_model(p).n==0 ) return VK_BLIND;
    rc = model_dim(p, vk_vector_model(p), &dim);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    if( !dim ) return VK_BLIND;
    rc = vk_prepare(p, "SELECT vk_cosine(x'0000803f00000000', x'0000803f00000000')"
                       " > vk_cosine(x'0000803f00000000', x'000000000000803f')", &pSt);
    if( rc!=SQLITE_OK ) return vk_sql_status(p, rc);
    rc = vk_step(pSt);
    if( rc==SQLITE_ROW ) ok = sqlite3_column_int(pSt, 0);
    sqlite3_finalize(pSt);
    if( rc!=SQLITE_ROW ) return vk_sql_status(p, rc);
    return ok ? VK_OK : VK_BLIND;
}

const VkSearchMethod vkVector = { "vector", 0, vector_open, vector_selfcheck };
