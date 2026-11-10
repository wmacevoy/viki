/*
** viki_delta.c -- reverse deltas for DS-1: an older body rebuilt from a newer one.
**
** Pure: bytes in, bytes out, no SQL. The algorithm is the textbook rolling-hash
** match (rsync's): the source (the newer body) is indexed in fixed blocks, the
** target (the older body) is scanned with a rolling hash, and each match is
** extended both ways. It works on any bytes (A-4a), not only on lines.
**
** Format: varint target length, then ops. An op is a varint (len<<1 | copy):
** a copy is followed by a varint source offset; a literal by its len bytes.
** Every length and offset is checked on apply, so a malformed delta is refused
** (SQLITE_CORRUPT), never read past.
*/
#include "viki_store_int.h"
#include <string.h>

#define BLOCK 16
#define MULT  0x01000193u

static uint32_t block_hash(const uint8_t *p){
    uint32_t h = 0;
    int i;
    for(i=0; i<BLOCK; i++) h = h*MULT + p[i];
    return h;
}

static uint8_t *put_varint(uint8_t *z, uint64_t v){
    while( v>=0x80 ){ *z++ = (uint8_t)(v | 0x80); v >>= 7; }
    *z++ = (uint8_t)v;
    return z;
}

/* 0 when the varint runs past the end or past 32 bits. */
static const uint8_t *get_varint(const uint8_t *z, const uint8_t *zEnd, uint64_t *pV){
    uint64_t v = 0;
    int shift = 0;
    while( z<zEnd ){
        uint8_t c = *z++;
        v |= (uint64_t)(c & 0x7f) << shift;
        if( !(c & 0x80) ){
            if( v>0xffffffffu ) return 0;
            *pV = v;
            return z;
        }
        shift += 7;
        if( shift>28 ) return 0;
    }
    return 0;
}

static uint8_t *put_literal(uint8_t *z, const uint8_t *p, uint32_t n){
    if( n==0 ) return z;
    z = put_varint(z, (uint64_t)n<<1);
    memcpy(z, p, n);
    return z + n;
}

int vk_delta_make(const uint8_t *src, uint32_t nSrc, const uint8_t *tgt, uint32_t nTgt,
                  uint8_t **pOut, uint32_t *pnOut){
    uint32_t nBlock = nSrc/BLOCK, nSlot = 1, mask, i, lit = 0, k;
    uint32_t *aSlot = 0;
    uint32_t top = 1;
    uint64_t nMax;
    uint8_t *out, *z;
    *pOut = 0; *pnOut = 0;
    /* Worst case: every byte a literal, and per match a literal header (5) and a copy (10). */
    nMax = (uint64_t)nTgt + 16*((uint64_t)nTgt/BLOCK + 2) + 16;
    out = (uint8_t*)sqlite3_malloc64(nMax);
    if( !out ) return SQLITE_NOMEM;
    z = put_varint(out, nTgt);
    if( nBlock>0 && nTgt>=BLOCK ){
        while( nSlot < 2*nBlock ) nSlot <<= 1;
        mask = nSlot-1;
        aSlot = (uint32_t*)sqlite3_malloc64((uint64_t)nSlot*sizeof(uint32_t));
        if( !aSlot ){ sqlite3_free(out); return SQLITE_NOMEM; }
        memset(aSlot, 0, (size_t)nSlot*sizeof(uint32_t));
        for(k=0; k<nBlock; k++) aSlot[block_hash(src + k*BLOCK) & mask] = k*BLOCK + 1;
        for(k=1; k<BLOCK; k++) top *= MULT;      /* MULT^(BLOCK-1), to roll out a byte */
        i = 0;
        {
            uint32_t h = block_hash(tgt);
            while( i+BLOCK <= nTgt ){
                uint32_t cand = aSlot[h & mask];
                if( cand && memcmp(src + cand-1, tgt + i, BLOCK)==0 ){
                    uint32_t s = cand-1, len = BLOCK, back = 0;
                    while( i+len < nTgt && s+len < nSrc && src[s+len]==tgt[i+len] ) len++;
                    while( back < i-lit && back < s && src[s-1-back]==tgt[i-1-back] ) back++;
                    z = put_literal(z, tgt + lit, i-back-lit);
                    z = put_varint(z, ((uint64_t)(len+back)<<1) | 1);
                    z = put_varint(z, s-back);
                    i += len;
                    lit = i;
                    if( i+BLOCK <= nTgt ) h = block_hash(tgt + i);
                }else{
                    if( i+BLOCK < nTgt ) h = (h - tgt[i]*top)*MULT + tgt[i+BLOCK];
                    i++;
                }
            }
        }
        sqlite3_free(aSlot);
    }
    z = put_literal(z, tgt + lit, nTgt-lit);
    *pOut = out;
    *pnOut = (uint32_t)(z - out);
    return SQLITE_OK;
}

int vk_delta_apply(const uint8_t *src, uint32_t nSrc, const uint8_t *d, uint32_t nD,
                   uint8_t **pOut, uint32_t *pnOut){
    const uint8_t *zEnd = d + nD;
    uint64_t nTgt, op, off;
    uint32_t done = 0;
    uint8_t *out;
    *pOut = 0; *pnOut = 0;
    d = get_varint(d, zEnd, &nTgt);
    if( !d ) return SQLITE_CORRUPT;
    out = (uint8_t*)sqlite3_malloc64(nTgt ? nTgt : 1);
    if( !out ) return SQLITE_NOMEM;
    while( d<zEnd ){
        uint64_t len;
        d = get_varint(d, zEnd, &op);
        if( !d ) goto corrupt;
        len = op>>1;
        if( len==0 || len > nTgt-done ) goto corrupt;
        if( op & 1 ){
            d = get_varint(d, zEnd, &off);
            if( !d || off > nSrc || len > nSrc-off ) goto corrupt;
            memcpy(out+done, src+off, (size_t)len);
        }else{
            if( len > (uint64_t)(zEnd-d) ) goto corrupt;
            memcpy(out+done, d, (size_t)len);
            d += len;
        }
        done += (uint32_t)len;
    }
    if( done!=nTgt ) goto corrupt;
    *pOut = out;
    *pnOut = done;
    return SQLITE_OK;
corrupt:
    sqlite3_free(out);
    return SQLITE_CORRUPT;
}
