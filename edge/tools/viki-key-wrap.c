/* viki-key-wrap -- wrap a secret (a tribe key) to one or more X25519
** recipients, and unwrap it with an identity.
**
** WIRE FORMAT IS age v1, DELIBERATELY, AND NOT OpenPGP. age's spec is about a
** page and is exactly the primitives LibreSSL already exposes; OpenPGP is
** packet framing, a dozen algorithms, subkeys and a trust model, and
** implementing it is a project rather than a file. The payoff of matching age
** byte-for-byte is that `age`/`rage` interoperate, so a member can use a key
** they already have and can recover a tribe key with a stock tool if this
** program is ever lost. That last property is the whole reason not to invent
** a format: a custodial scheme nobody else can read is a way to lose data.
**
** NO NEW DEPENDENCIES. X25519, ChaCha20-Poly1305, HMAC-SHA256 and RAND_bytes
** all come from the LibreSSL already linked by fossil-see (QUEUE 48). HKDF is
** implemented here from HMAC rather than through EVP_PKEY_HKDF -- it is twenty
** lines of RFC 5869, and the EVP ctx interface is more code and more ways to
** be subtly wrong than the thing it wraps.
**
** WHAT THIS GIVES AND WHAT IT DOES NOT. It gives distribution and recovery: a
** tribe key can be handed to N members, and an identity key can be wrapped to
** a recovery recipient kept offline. IT DOES NOT GIVE REVOCATION. Removing a
** member means minting a new tribe key, re-encrypting, and re-wrapping to
** everyone remaining; anyone who ever held the old key keeps reading any copy
** they kept. See QUEUE 48 -- that cost belongs in the design, not in a
** surprise.
**
** Usage:
**   viki-key-wrap keygen
**   viki-key-wrap wrap -r age1... [-r age1...] [-i in] [-o out]
**   viki-key-wrap unwrap -k AGE-SECRET-KEY-1... [-i in] [-o out]
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#define FILE_KEY_LEN 16
#define X25519_LEN   32
#define TAG_LEN      16
#define MAX_RECIP    16

static void die(const char *z){ fprintf(stderr, "viki-key-wrap: %s\n", z); exit(1); }

/* ---- base64, age's flavour: standard alphabet, NO padding ------------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64enc(const unsigned char *in, int n, char *out){
    int i, o = 0;
    for( i = 0; i < n; i += 3 ){
        unsigned v = (unsigned)in[i] << 16;
        int rem = n - i;
        if( rem > 1 ) v |= (unsigned)in[i+1] << 8;
        if( rem > 2 ) v |= in[i+2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        if( rem > 1 ) out[o++] = B64[(v >> 6) & 63];
        if( rem > 2 ) out[o++] = B64[v & 63];
    }
    out[o] = 0;
    return o;
}
static int b64dec(const char *in, unsigned char *out, int cap){
    int o = 0, bits = 0; unsigned v = 0;
    for( ; *in; in++ ){
        const char *p;
        if( *in == '=' || *in == '\n' || *in == '\r' ) continue;
        p = strchr(B64, *in);
        if( !p ) return -1;
        v = (v << 6) | (unsigned)(p - B64);
        bits += 6;
        if( bits >= 8 ){
            bits -= 8;
            if( o >= cap ) return -1;
            out[o++] = (unsigned char)((v >> bits) & 0xff);
        }
    }
    return o;
}

/* ---- bech32 (BIP-173), which is how age spells its keys --------------- */
static const char BECH[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
static unsigned bech_polymod(const unsigned char *v, int n){
    static const unsigned G[5] = {0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3};
    unsigned chk = 1; int i, j;
    for( i = 0; i < n; i++ ){
        unsigned b = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ v[i];
        for( j = 0; j < 5; j++ ) if( (b >> j) & 1 ) chk ^= G[j];
    }
    return chk;
}
/* hrp expanded + data + 6 zero checksum slots */
/* The checksum is defined over the LOWERCASE hrp even when the finished
** string is rendered in upper case (bech32 is case-insensitive but not
** case-mixing). age's secret keys are upper case, so passing
** "AGE-SECRET-KEY-" straight through produced a valid-looking key that the
** real `age` rejected with "invalid checksum" -- caught only because the
** interop test ran both directions. Lowercase here, and let the caller
** upper-case the whole finished string. */
static int bech_encode(const char *hrpIn, const unsigned char *data8, int n8, char *out){
    unsigned char d5[128], buf[256];
    char hrp[64];
    int n5 = 0, i, hl = (int)strlen(hrpIn), o = 0, bits = 0; unsigned acc = 0;
    if( hl >= (int)sizeof(hrp) ) return -1;
    for( i = 0; i < hl; i++ )
        hrp[i] = (char)(hrpIn[i] >= 'A' && hrpIn[i] <= 'Z' ? hrpIn[i] - 'A' + 'a' : hrpIn[i]);
    hrp[hl] = 0;
    for( i = 0; i < n8; i++ ){
        acc = (acc << 8) | data8[i]; bits += 8;
        while( bits >= 5 ){ bits -= 5; d5[n5++] = (unsigned char)((acc >> bits) & 31); }
    }
    if( bits ) d5[n5++] = (unsigned char)((acc << (5 - bits)) & 31);
    for( i = 0; i < hl; i++ ) buf[o++] = (unsigned char)(hrp[i] >> 5);
    buf[o++] = 0;
    for( i = 0; i < hl; i++ ) buf[o++] = (unsigned char)(hrp[i] & 31);
    memcpy(buf + o, d5, (size_t)n5); o += n5;
    memset(buf + o, 0, 6); o += 6;
    {
        unsigned chk = bech_polymod(buf, o) ^ 1;
        int p = 0;
        for( i = 0; i < hl; i++ ) out[p++] = hrp[i];
        out[p++] = '1';
        for( i = 0; i < n5; i++ ) out[p++] = BECH[d5[i]];
        for( i = 0; i < 6; i++ ) out[p++] = BECH[(chk >> (5 * (5 - i))) & 31];
        out[p] = 0;
        return p;
    }
}
static int bech_decode(const char *s, const char *hrp, unsigned char *out8, int cap){
    char low[256]; int i, n = (int)strlen(s), hl = (int)strlen(hrp), n5;
    unsigned char d5[128]; unsigned acc = 0; int bits = 0, o = 0;
    if( n >= (int)sizeof(low) ) return -1;
    for( i = 0; i < n; i++ ) low[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] - 'A' + 'a' : s[i]);
    low[n] = 0;
    { char lh[64]; int j; for( j = 0; j < hl; j++ ) lh[j] = (char)(hrp[j] >= 'A' && hrp[j] <= 'Z' ? hrp[j]-'A'+'a' : hrp[j]); lh[hl]=0;
      if( strncmp(low, lh, (size_t)hl) != 0 || low[hl] != '1' ) return -1; }
    n5 = n - hl - 1 - 6;
    if( n5 < 0 || n5 > (int)sizeof(d5) ) return -1;
    for( i = 0; i < n5; i++ ){
        const char *p = strchr(BECH, low[hl + 1 + i]);
        if( !p ) return -1;
        d5[i] = (unsigned char)(p - BECH);
    }
    for( i = 0; i < n5; i++ ){
        acc = (acc << 5) | d5[i]; bits += 5;
        if( bits >= 8 ){ bits -= 8; if( o >= cap ) return -1; out8[o++] = (unsigned char)((acc >> bits) & 0xff); }
    }
    return o;
}

/* ---- HKDF-SHA256, RFC 5869, from HMAC -------------------------------- */
static void hkdf(const unsigned char *salt, int saltlen,
                 const unsigned char *ikm, int ikmlen,
                 const char *info, unsigned char *out, int outlen){
    unsigned char prk[32], t[32], buf[256];
    unsigned int prklen = 0, tlen = 0;
    int done = 0, i = 1;
    unsigned char zero[32];
    if( !salt ){ memset(zero, 0, sizeof(zero)); salt = zero; saltlen = 0; }
    HMAC(EVP_sha256(), salt, saltlen, ikm, (size_t)ikmlen, prk, &prklen);
    while( done < outlen ){
        int n = 0;
        if( tlen ){ memcpy(buf, t, tlen); n = (int)tlen; }
        memcpy(buf + n, info, strlen(info)); n += (int)strlen(info);
        buf[n++] = (unsigned char)i++;
        HMAC(EVP_sha256(), prk, (int)prklen, buf, (size_t)n, t, &tlen);
        {
            int take = outlen - done < (int)tlen ? outlen - done : (int)tlen;
            memcpy(out + done, t, (size_t)take);
            done += take;
        }
    }
}

/* ---- ChaCha20-Poly1305 ------------------------------------------------ */
static int aead(int enc, const unsigned char *key, const unsigned char *nonce,
                const unsigned char *in, int inlen, unsigned char *out){
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int len = 0, ok = 0, total = 0;
    unsigned char tag[TAG_LEN];
    if( !c ) return -1;
    if( EVP_CipherInit_ex(c, EVP_chacha20_poly1305(), NULL, NULL, NULL, enc) != 1 ) goto done;
    if( EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1 ) goto done;
    if( EVP_CipherInit_ex(c, NULL, NULL, key, nonce, enc) != 1 ) goto done;
    if( !enc ){
        memcpy(tag, in + inlen - TAG_LEN, TAG_LEN);
        inlen -= TAG_LEN;
        if( EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_SET_TAG, TAG_LEN, tag) != 1 ) goto done;
    }
    if( EVP_CipherUpdate(c, out, &len, in, inlen) != 1 ) goto done;
    total = len;
    if( EVP_CipherFinal_ex(c, out + total, &len) != 1 ) goto done;
    total += len;
    if( enc ){
        if( EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_AEAD_GET_TAG, TAG_LEN, out + total) != 1 ) goto done;
        total += TAG_LEN;
    }
    ok = 1;
done:
    EVP_CIPHER_CTX_free(c);
    return ok ? total : -1;
}

/* ---- X25519 ----------------------------------------------------------- */
static int x25519_pub(const unsigned char *sk, unsigned char *pk){
    EVP_PKEY *p = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, X25519_LEN);
    size_t n = X25519_LEN; int ok;
    if( !p ) return 0;
    ok = EVP_PKEY_get_raw_public_key(p, pk, &n) == 1;
    EVP_PKEY_free(p);
    return ok;
}
static int x25519_shared(const unsigned char *sk, const unsigned char *peer, unsigned char *out){
    EVP_PKEY *a = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, sk, X25519_LEN);
    EVP_PKEY *b = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer, X25519_LEN);
    EVP_PKEY_CTX *c = NULL; size_t n = X25519_LEN; int ok = 0, i, zero = 1;
    if( !a || !b ) goto done;
    c = EVP_PKEY_CTX_new(a, NULL);
    if( !c || EVP_PKEY_derive_init(c) != 1 ) goto done;
    if( EVP_PKEY_derive_set_peer(c, b) != 1 ) goto done;
    if( EVP_PKEY_derive(c, out, &n) != 1 ) goto done;
    /* An all-zero shared secret means a low-order peer point; the age spec
    ** requires rejecting it rather than proceeding with a known key. */
    for( i = 0; i < X25519_LEN; i++ ) if( out[i] ) { zero = 0; break; }
    ok = !zero;
done:
    EVP_PKEY_CTX_free(c); EVP_PKEY_free(a); EVP_PKEY_free(b);
    return ok;
}

/* ---- age v1 ----------------------------------------------------------- */
static void mac_header(const unsigned char *fileKey, const char *hdr, int hdrlen, unsigned char *out){
    unsigned char hk[32]; unsigned int n = 32;
    hkdf(NULL, 0, fileKey, FILE_KEY_LEN, "header", hk, 32);
    HMAC(EVP_sha256(), hk, 32, (const unsigned char*)hdr, (size_t)hdrlen, out, &n);
}

static int cmd_keygen(void){
    unsigned char sk[X25519_LEN], pk[X25519_LEN];
    char pub[128], sec[128];
    if( RAND_bytes(sk, X25519_LEN) != 1 ) die("RAND_bytes failed");
    if( !x25519_pub(sk, pk) ) die("x25519 public derivation failed");
    bech_encode("age", pk, X25519_LEN, pub);
    bech_encode("AGE-SECRET-KEY-", sk, X25519_LEN, sec);
    {   /* age spells the secret in UPPER case */
        char *p; for( p = sec; *p; p++ ) if( *p >= 'a' && *p <= 'z' ) *p = (char)(*p - 'a' + 'A');
    }
    printf("# public key: %s\n%s\n", pub, sec);
    return 0;
}

static unsigned char *readall(const char *zPath, int *pn){
    FILE *f = zPath ? fopen(zPath, "rb") : stdin;
    unsigned char *b = NULL; int cap = 0, n = 0;
    if( !f ) die("cannot open input");
    for(;;){
        int got;
        if( n + 65536 > cap ){ cap = cap ? cap * 2 : 131072; b = realloc(b, (size_t)cap); if( !b ) die("oom"); }
        got = (int)fread(b + n, 1, 65536, f);
        if( got <= 0 ) break;
        n += got;
    }
    if( zPath ) fclose(f);
    *pn = n;
    return b;
}

/* Wraps `plain` to every recipient, into a caller-supplied buffer. The
** memory forms exist because viki-identity stores wrapped TRIBE KEYS in a
** BLOB column: routing that through temp files would put plaintext secrets on
** disk, which is the one thing this whole layer exists to avoid. */
int age_wrap_mem(char **recips, int nRecip,
                 const unsigned char *plain, int plen,
                 unsigned char *out, int *pOutLen, int outCap){
    unsigned char fileKey[FILE_KEY_LEN], nonce[16], payKey[32], aeadNonce[12];
    char hdr[8192]; int hl = 0, i, o = 0;
    unsigned char mac[32]; char b64[512];

    if( RAND_bytes(fileKey, FILE_KEY_LEN) != 1 ) return 0;
    hl += sprintf(hdr + hl, "age-encryption.org/v1\n");
    for( i = 0; i < nRecip; i++ ){
        unsigned char rpk[X25519_LEN], esk[X25519_LEN], epk[X25519_LEN];
        unsigned char shared[X25519_LEN], salt[64], wk[32], body[FILE_KEY_LEN + TAG_LEN], z12[12];
        if( bech_decode(recips[i], "age", rpk, X25519_LEN) != X25519_LEN ) return 0;
        if( RAND_bytes(esk, X25519_LEN) != 1 ) return 0;
        if( !x25519_pub(esk, epk) ) return 0;
        if( !x25519_shared(esk, rpk, shared) ) return 0;
        memcpy(salt, epk, X25519_LEN); memcpy(salt + X25519_LEN, rpk, X25519_LEN);
        hkdf(salt, 64, shared, X25519_LEN, "age-encryption.org/v1/X25519", wk, 32);
        memset(z12, 0, sizeof(z12));
        if( aead(1, wk, z12, fileKey, FILE_KEY_LEN, body) != FILE_KEY_LEN + TAG_LEN ) return 0;
        b64enc(epk, X25519_LEN, b64);
        hl += sprintf(hdr + hl, "-> X25519 %s\n", b64);
        b64enc(body, FILE_KEY_LEN + TAG_LEN, b64);
        hl += sprintf(hdr + hl, "%s\n", b64);
    }
    hl += sprintf(hdr + hl, "---");
    mac_header(fileKey, hdr, hl, mac);
    b64enc(mac, 32, b64);
    hl += sprintf(hdr + hl, " %s\n", b64);

    if( RAND_bytes(nonce, 16) != 1 ) return 0;
    hkdf(nonce, 16, fileKey, FILE_KEY_LEN, "payload", payKey, 32);
    memset(aeadNonce, 0, 12); aeadNonce[11] = 1;
    if( hl + 16 + plen + TAG_LEN > outCap ) return 0;
    memcpy(out + o, hdr, (size_t)hl); o += hl;
    memcpy(out + o, nonce, 16); o += 16;
    {
        int n = aead(1, payKey, aeadNonce, plain, plen, out + o);
        if( n != plen + TAG_LEN ) return 0;
        o += n;
    }
    *pOutLen = o;
    return 1;
}

static unsigned char *readall(const char *zPath, int *pn);

static int cmd_wrap(char **recips, int nRecip, const char *zIn, const char *zOut){
    unsigned char *plain, out[65536 + 8192];
    int plen, olen = 0;
    FILE *f;
    plain = readall(zIn, &plen);
    if( plen > 65536 ) die("payload > 64KiB: this tool wraps KEYS, not files");
    if( !age_wrap_mem(recips, nRecip, plain, plen, out, &olen, (int)sizeof(out)) )
        die("wrap failed");
    f = zOut ? fopen(zOut, "wb") : stdout;
    if( !f ) die("cannot open output");
    fwrite(out, 1, (size_t)olen, f);
    if( zOut ) fclose(f);
    free(plain);
    return 0;
}

/* Unwraps in memory. Returns 1 on success. `sk` is the raw 32-byte X25519
** secret, so callers holding a key in memory never have to render it as text. */
int age_unwrap_mem(const unsigned char *sk, const unsigned char *buf, int n,
                   unsigned char *out, int *pOutLen, int outCap){
    int i, hdrEnd = -1, got = 0;
    unsigned char fileKey[FILE_KEY_LEN];

    for( i = 0; i + 3 < n; i++ ){
        if( buf[i]=='-' && buf[i+1]=='-' && buf[i+2]=='-' && buf[i+3]==' '
            && (i == 0 || buf[i-1] == '\n') ){
            int j = i; while( j < n && buf[j] != '\n' ) j++;
            hdrEnd = j + 1; break;
        }
    }
    if( hdrEnd < 0 ) return 0;

    for( i = 0; i + 10 < hdrEnd && !got; i++ ){
        char shareB64[128], bodyB64[128];
        unsigned char epk[X25519_LEN], shared[X25519_LEN], salt[64], wk[32];
        unsigned char body[FILE_KEY_LEN + TAG_LEN], mypk[X25519_LEN], z12[12];
        int a, b, c, d;
        if( memcmp(buf + i, "-> X25519 ", 10) != 0 ) continue;
        if( i != 0 && buf[i-1] != '\n' ) continue;
        a = i + 10; b = a;
        while( b < hdrEnd && buf[b] != '\n' ) b++;
        if( b - a >= (int)sizeof(shareB64) ) continue;
        memcpy(shareB64, buf + a, (size_t)(b - a)); shareB64[b-a] = 0;
        c = b + 1; d = c; while( d < hdrEnd && buf[d] != '\n' ) d++;
        if( d - c >= (int)sizeof(bodyB64) ) continue;
        memcpy(bodyB64, buf + c, (size_t)(d - c)); bodyB64[d-c] = 0;
        if( b64dec(shareB64, epk, X25519_LEN) != X25519_LEN ) continue;
        if( b64dec(bodyB64, body, sizeof(body)) != FILE_KEY_LEN + TAG_LEN ) continue;
        if( !x25519_pub(sk, mypk) ) return 0;
        if( !x25519_shared(sk, epk, shared) ) continue;
        memcpy(salt, epk, X25519_LEN); memcpy(salt + X25519_LEN, mypk, X25519_LEN);
        hkdf(salt, 64, shared, X25519_LEN, "age-encryption.org/v1/X25519", wk, 32);
        memset(z12, 0, sizeof(z12));
        if( aead(0, wk, z12, body, FILE_KEY_LEN + TAG_LEN, fileKey) == FILE_KEY_LEN ) got = 1;
    }
    if( !got ) return 0;

    {   /* the header MAC is checked before anything in the header is trusted */
        int macLineStart = -1, j;
        unsigned char want[32], have[32]; char b64[128];
        for( j = hdrEnd - 1; j > 0; j-- ) if( buf[j-1] == '\n' && buf[j] == '-' ){ macLineStart = j; break; }
        if( macLineStart < 0 ) return 0;
        mac_header(fileKey, (const char*)buf, macLineStart + 3, have);
        {
            int k = macLineStart + 4, e = k;
            while( e < hdrEnd && buf[e] != '\n' ) e++;
            if( e - k >= (int)sizeof(b64) ) return 0;
            memcpy(b64, buf + k, (size_t)(e - k)); b64[e-k] = 0;
            if( b64dec(b64, want, 32) != 32 ) return 0;
        }
        if( memcmp(want, have, 32) != 0 ) return 0;
    }

    {
        unsigned char payKey[32], aeadNonce[12];
        int clen = n - hdrEnd - 16, plen;
        if( clen < TAG_LEN || clen - TAG_LEN > outCap ) return 0;
        hkdf(buf + hdrEnd, 16, fileKey, FILE_KEY_LEN, "payload", payKey, 32);
        memset(aeadNonce, 0, 12); aeadNonce[11] = 1;
        plen = aead(0, payKey, aeadNonce, buf + hdrEnd + 16, clen, out);
        if( plen < 0 ) return 0;
        *pOutLen = plen;
    }
    return 1;
}

static int cmd_unwrap(const char *zKey, const char *zIn, const char *zOut){
    unsigned char sk[X25519_LEN], *buf, out[65536];
    int n, olen = 0;
    if( bech_decode(zKey, "AGE-SECRET-KEY-", sk, X25519_LEN) != X25519_LEN )
        die("bad identity (expected AGE-SECRET-KEY-1...)");
    buf = readall(zIn, &n);
    if( !age_unwrap_mem(sk, buf, n, out, &olen, (int)sizeof(out)) )
        die("no identity matched, or the file is tampered/corrupt");
    memset(sk, 0, sizeof(sk));
    {
        FILE *f = zOut ? fopen(zOut, "wb") : stdout;
        if( !f ) die("cannot open output");
        fwrite(out, 1, (size_t)olen, f);
        if( zOut ) fclose(f);
    }
    free(buf);
    return 0;
}

#ifndef VIKI_AGE_NO_MAIN
int main(int argc, char **argv){
    char *recips[MAX_RECIP]; int nRecip = 0, i;
    const char *zIn = NULL, *zOut = NULL, *zKey = NULL;
    if( argc < 2 ){
        fprintf(stderr,
          "usage: viki-key-wrap keygen\n"
          "       viki-key-wrap wrap -r age1... [-r age1...] [-i in] [-o out]\n"
          "       viki-key-wrap unwrap -k AGE-SECRET-KEY-1... [-i in] [-o out]\n"
          "Output is age v1: `age -d` and `rage -d` read it.\n");
        return 2;
    }
    for( i = 2; i < argc; i++ ){
        if( !strcmp(argv[i], "-r") && i+1 < argc ){ if( nRecip >= MAX_RECIP ) die("too many recipients"); recips[nRecip++] = argv[++i]; }
        else if( !strcmp(argv[i], "-i") && i+1 < argc ) zIn = argv[++i];
        else if( !strcmp(argv[i], "-o") && i+1 < argc ) zOut = argv[++i];
        else if( !strcmp(argv[i], "-k") && i+1 < argc ) zKey = argv[++i];
        else die("unknown option");
    }
    if( !strcmp(argv[1], "keygen") ) return cmd_keygen();
    if( !strcmp(argv[1], "wrap") ){ if( !nRecip ) die("wrap needs at least one -r"); return cmd_wrap(recips, nRecip, zIn, zOut); }
    if( !strcmp(argv[1], "unwrap") ){ if( !zKey ) die("unwrap needs -k"); return cmd_unwrap(zKey, zIn, zOut); }
    die("unknown command");
    return 2;
}
#endif /* VIKI_AGE_NO_MAIN */
