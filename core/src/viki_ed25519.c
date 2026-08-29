/*
** viki_ed25519.c -- the BUILT-IN identity, on LibreSSL.
**
** SQLCIPHER AND LIBRESSL ARE BEDROCK for viki (Warren, 2026-08-29), so core
** may use them and this is the default signer rather than an example the host
** has to write. Batteries included.
**
** THE CALLBACK STAYS, and that is the point of having both: VikiIdentity's
** xSign/xVerify are still what core calls, so a platform keystore -- a
** thumbprint against the Secure Enclave, a TPM, an HSM -- overrides this
** without core changing. Included is not mandatory.
**
** Ed25519 costs nothing extra because SQLCipher already links LibreSSL:
** EVP_PKEY_ED25519 with the raw-key constructors and one-shot EVP_DigestSign,
** which is the API Ed25519 is meant to be used through (no separate hash --
** Ed25519 hashes internally, and pre-hashing it is a different algorithm).
**
** THE KEY FILE holds a name and a 32-byte seed as hex, one per line:
**
**     warren (laptop)
**     3b5d...64 hex chars...
**
** The name is in the file because it is part of the identity -- viki's
** identity assertion is (public key, name), so a rename is a different claim
** and must be visible rather than silent.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "viki_core.h"

#define SEED_LEN 32
#define PUB_LEN  32

static int hex2bin(const char *z, unsigned char *a, int n){
    int i;
    for(i=0;i<n;i++){
        int hi, lo;
        char c = z[i*2], d = z[i*2+1];
        hi = (c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1;
        lo = (d>='0'&&d<='9')?d-'0':(d>='a'&&d<='f')?d-'a'+10:(d>='A'&&d<='F')?d-'A'+10:-1;
        if( hi<0 || lo<0 ) return -1;
        a[i] = (unsigned char)((hi<<4)|lo);
    }
    return 0;
}
static void bin2hex(const unsigned char *a, int n, char *z){
    int i;
    for(i=0;i<n;i++) sprintf(z+i*2, "%02x", a[i]);
    z[n*2] = 0;
}

struct VikiIdKey {
    unsigned char aSeed[SEED_LEN];
    char zPubHex[PUB_LEN*2+1];
    char zName[256];
};

VikiIdKey *viki_ed25519_generate(const char *zName, char *zSeedHexOut){
    VikiIdKey *p = (VikiIdKey*)calloc(1, sizeof(VikiIdKey));
    EVP_PKEY *k = 0;
    unsigned char aPub[PUB_LEN];
    size_t nPub = sizeof aPub;
    if( !p ) return 0;
    if( RAND_bytes(p->aSeed, SEED_LEN)!=1 ){ free(p); return 0; }
    k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, 0, p->aSeed, SEED_LEN);
    if( !k || EVP_PKEY_get_raw_public_key(k, aPub, &nPub)!=1 ){
        EVP_PKEY_free(k); free(p); return 0;
    }
    EVP_PKEY_free(k);
    bin2hex(aPub, PUB_LEN, p->zPubHex);
    snprintf(p->zName, sizeof p->zName, "%s", zName ? zName : "unnamed");
    if( zSeedHexOut ) bin2hex(p->aSeed, SEED_LEN, zSeedHexOut);
    return p;
}

VikiIdKey *viki_ed25519_load(const char *zPath, char *zErr, size_t nErr){
    struct stat st;
    FILE *f;
    VikiIdKey *p;
    char zLine[1024], zSeed[128];
    EVP_PKEY *k;
    unsigned char aPub[PUB_LEN];
    size_t nPub = sizeof aPub, n;
    if( stat(zPath,&st)!=0 ){ snprintf(zErr,nErr,"cannot read %s",zPath); return 0; }
    /* THE SAME RULE AS THE DIARY KEY, for the same reason: a signing key in a
    ** 0644 file is an identity anyone on the box can wear. */
    if( st.st_mode & (S_IRWXG|S_IRWXO) ){
        snprintf(zErr,nErr,"%s is group/world accessible (mode %04o) -- chmod 600 it",
                 zPath, (unsigned)(st.st_mode & 07777));
        return 0;
    }
    f = fopen(zPath,"rb");
    if( !f ){ snprintf(zErr,nErr,"cannot open %s",zPath); return 0; }
    p = (VikiIdKey*)calloc(1, sizeof(VikiIdKey));
    if( !p ){ fclose(f); snprintf(zErr,nErr,"out of memory"); return 0; }
    if( !fgets(zLine,sizeof zLine,f) || !fgets(zSeed,sizeof zSeed,f) ){
        fclose(f); free(p); snprintf(zErr,nErr,"%s: expected a name line then a seed line",zPath);
        return 0;
    }
    fclose(f);
    n = strlen(zLine); while( n && (zLine[n-1]=='\n'||zLine[n-1]=='\r') ) zLine[--n]=0;
    n = strlen(zSeed); while( n && (zSeed[n-1]=='\n'||zSeed[n-1]=='\r') ) zSeed[--n]=0;
    if( strlen(zSeed)!=SEED_LEN*2 || hex2bin(zSeed, p->aSeed, SEED_LEN)!=0 ){
        free(p); snprintf(zErr,nErr,"%s: seed must be %d hex characters",zPath,SEED_LEN*2);
        return 0;
    }
    snprintf(p->zName,sizeof p->zName,"%s",zLine);
    k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, 0, p->aSeed, SEED_LEN);
    if( !k || EVP_PKEY_get_raw_public_key(k, aPub, &nPub)!=1 ){
        EVP_PKEY_free(k); free(p); snprintf(zErr,nErr,"bad Ed25519 seed"); return 0;
    }
    EVP_PKEY_free(k);
    bin2hex(aPub, PUB_LEN, p->zPubHex);
    return p;
}

void viki_ed25519_free(VikiIdKey *p){
    if( !p ) return;
    /* wipe: under `viki run` this process may live for the whole command */
    memset(p->aSeed, 0, SEED_LEN);
    free(p);
}
const char *viki_ed25519_pub (const VikiIdKey *p){ return p->zPubHex; }
const char *viki_ed25519_name(const VikiIdKey *p){ return p->zName; }

/* The two callbacks core actually calls. Signing the assertion id is enough:
** the id is the hash of everything the assertion says, so a signature over it
** covers the whole statement. */
int viki_ed25519_sign(void *pApp, const char *zId, unsigned char *aSig, int *pnSig){
    VikiIdKey *p = (VikiIdKey*)pApp;
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, 0, p->aSeed, SEED_LEN);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t n = (size_t)*pnSig;
    int rc = 1;
    if( k && ctx
     && EVP_DigestSignInit(ctx, 0, 0, 0, k)==1          /* NO digest: Ed25519 */
     && EVP_DigestSign(ctx, aSig, &n, (const unsigned char*)zId, strlen(zId))==1 ){
        *pnSig = (int)n;
        rc = 0;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(k);
    return rc;
}

/* NO SECRET IS CONSULTED. pApp is ignored: verification needs only the public
** key recorded in the diary, which is what lets a peer holding no private
** material establish who said what. */
int viki_ed25519_verify(void *pApp, const char *zPubHex, const char *zId,
                   const unsigned char *aSig, int nSig){
    unsigned char aPub[PUB_LEN];
    EVP_PKEY *k;
    EVP_MD_CTX *ctx;
    int rc = 1;
    (void)pApp;
    if( !zPubHex || strlen(zPubHex)!=PUB_LEN*2 ) return 1;
    if( hex2bin(zPubHex, aPub, PUB_LEN)!=0 ) return 1;
    k = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, 0, aPub, PUB_LEN);
    ctx = EVP_MD_CTX_new();
    if( k && ctx
     && EVP_DigestVerifyInit(ctx, 0, 0, 0, k)==1
     && EVP_DigestVerify(ctx, aSig, (size_t)nSig,
                         (const unsigned char*)zId, strlen(zId))==1 ) rc = 0;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(k);
    return rc;
}

/* THE BATTERIES-INCLUDED PATH: records the identity and fills in the
** callbacks, so a caller that just wants signing writes three lines instead
** of an EVP program. A caller that needs a keystore fills VikiIdentity in
** itself and never calls this. */
VikiStatus viki_identity_ed25519(VikiIdKey *pKey, VikiIdentity *pOut, char *zIdOut){
    char zAssert[VIKI_ID_HEX+1];
    VikiStatus rc;
    if( !pKey || !pOut ) return VIKI_EINVAL;
    rc = viki_identity_put(viki_ed25519_name(pKey), viki_ed25519_pub(pKey), zAssert);
    if( rc!=VIKI_OK ) return rc;
    pOut->pApp    = pKey;
    pOut->xSign   = viki_ed25519_sign;
    pOut->xVerify = viki_ed25519_verify;
    pOut->zSigner = 0;      /* filled by the caller from zIdOut, which it owns */
    if( zIdOut ) memcpy(zIdOut, zAssert, VIKI_ID_HEX+1);
    return VIKI_OK;
}
