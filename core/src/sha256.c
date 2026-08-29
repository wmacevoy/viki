/*
** sha256.c -- content_hash keying for viki's local cache (VIKI_DESIGN.md).
**
** Standalone FIPS 180-4 SHA-256 (single-shot, no streaming API needed --
** every caller already has the whole buffer in memory). Not a security
** boundary -- content_hash is purely an internal cache key, never
** compared against or exposed as a Fossil artifact hash (see
** FINDINGS.md) -- but the algorithm is fully spec'd and this is a
** straightforward transcription of it, verified against known test
** vectors and cross-checked with `shasum -a 256`. Vendored instead of
** linking a crypto library so viki's build has no dependency on
** vendor/fossil-see (or any other project) at all -- see FINDINGS.md's
** entry on decoupling the build.
*/
#include "sha256.h"
#include <string.h>

typedef struct {
    unsigned int state[8];
    unsigned long long bitlen;
    unsigned char buf[64];
    unsigned int buflen;
} sha256_ctx;

static const unsigned int K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static unsigned int rotr(unsigned int x, int n){ return (x >> n) | (x << (32 - n)); }

static void sha256_transform(sha256_ctx *ctx, const unsigned char block[64]){
    unsigned int w[64];
    unsigned int a, b, c, d, e, f, g, h;
    int i;

    for( i = 0; i < 16; i++ ){
        w[i] = ((unsigned int)block[i*4] << 24) | ((unsigned int)block[i*4+1] << 16) |
               ((unsigned int)block[i*4+2] << 8) | (unsigned int)block[i*4+3];
    }
    for( i = 16; i < 64; i++ ){
        unsigned int s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        unsigned int s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for( i = 0; i < 64; i++ ){
        unsigned int S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        unsigned int temp1 = h + S1 + ch + K[i] + w[i];
        unsigned int S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx){
    static const unsigned int iv[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    memcpy(ctx->state, iv, sizeof iv);
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha256_update(sha256_ctx *ctx, const unsigned char *data, size_t len){
    size_t i = 0;
    while( i < len ){
        size_t take = 64 - ctx->buflen;
        if( take > len - i ) take = len - i;
        memcpy(ctx->buf + ctx->buflen, data + i, take);
        ctx->buflen += (unsigned int)take;
        i += take;
        ctx->bitlen += (unsigned long long)take * 8;
        if( ctx->buflen == 64 ){
            sha256_transform(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, unsigned char out[32]){
    unsigned long long bitlen = ctx->bitlen;
    unsigned char pad = 0x80;
    int i;

    sha256_update(ctx, &pad, 1);
    pad = 0x00;
    while( ctx->buflen != 56 ) sha256_update(ctx, &pad, 1);

    for( i = 7; i >= 0; i-- ){
        unsigned char b = (unsigned char)((bitlen >> (i * 8)) & 0xff);
        sha256_update(ctx, &b, 1);
    }

    for( i = 0; i < 8; i++ ){
        out[i*4]   = (unsigned char)(ctx->state[i] >> 24);
        out[i*4+1] = (unsigned char)(ctx->state[i] >> 16);
        out[i*4+2] = (unsigned char)(ctx->state[i] >> 8);
        out[i*4+3] = (unsigned char)(ctx->state[i]);
    }
}

void viki_sha256_hex(const void *data, size_t len, char hexout[65]){
    static const char digits[] = "0123456789abcdef";
    sha256_ctx ctx;
    unsigned char hash[32];
    int i;

    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char*)data, len);
    sha256_final(&ctx, hash);

    for( i = 0; i < 32; i++ ){
        hexout[i*2]   = digits[(hash[i] >> 4) & 0xf];
        hexout[i*2+1] = digits[hash[i] & 0xf];
    }
    hexout[64] = '\0';
}
