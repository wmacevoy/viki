#include "viki_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *viki_link_base(void){
    static char base[512];
    const char *z = getenv("VIKI_FOSSIL_URL");
    size_t n;
    if( !z || !z[0] ) return NULL;
    snprintf(base, sizeof(base), "%s", z);
    n = strlen(base);
    while( n > 0 && base[n-1] == '/' ) base[--n] = '\0';
    return base[0] ? base : NULL;
}

/* Percent-encodes everything outside the unreserved set. Wiki names carry
** spaces and punctuation, and a raw one would break the query string. */
static void urlenc(const char *z, char *out, size_t nOut){
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    while( *z && o + 4 < nOut ){
        unsigned char c = (unsigned char)*z++;
        if( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
         || (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~' || c=='/' ){
            out[o++] = (char)c;
        }else{
            out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

/* True if z starts with prefix; sets *pRest to what follows. */
static int has(const char *z, const char *prefix, const char **pRest){
    size_t n = strlen(prefix);
    if( strncmp(z, prefix, n) != 0 ) return 0;
    *pRest = z + n;
    return 1;
}

int viki_link_for(const char *zBase, const char *zPath, char *out, size_t nOut){
    const char *rest;
    char enc[1024];

    if( !zBase || !zBase[0] || !zPath || !zPath[0] ) return 0;

    if( has(zPath, "wiki:", &rest) ){
        urlenc(rest, enc, sizeof(enc));
        snprintf(out, nOut, "%s/wiki?name=%s", zBase, enc);
        return 1;
    }
    if( has(zPath, "ticket:", &rest) ){
        snprintf(out, nOut, "%s/tktview/%s", zBase, rest);
        return 1;
    }
    if( has(zPath, "forum:", &rest) ){
        snprintf(out, nOut, "%s/forumpost/%s", zBase, rest);
        return 1;
    }
    /* /info is Fossil's universal artifact page and routes correctly for
    ** check-ins, tech notes and ticket-change artifacts alike, so it is the
    ** right target for the classes with no dedicated viewer -- and a safer
    ** choice than inventing a per-class page that may not exist. */
    if( has(zPath, "ckin:", &rest) || has(zPath, "note:", &rest) || has(zPath, "tchg:", &rest) ){
        snprintf(out, nOut, "%s/info/%s", zBase, rest);
        return 1;
    }
    if( has(zPath, "attach:", &rest) ){
        snprintf(out, nOut, "%s/artifact/%s", zBase, rest);
        return 1;
    }
    if( has(zPath, "uv:", &rest) ){
        urlenc(rest, enc, sizeof(enc));
        snprintf(out, nOut, "%s/uv/%s", zBase, enc);
        return 1;
    }
    /* A checkout file. `ci=tip` because viki indexes the CURRENT checkout;
    ** pinning a check-in would claim a precision the index does not have --
    ** viki_source records the path, not the version it was read at. */
    if( zPath[0] == '.' && zPath[1] == '/' ) zPath += 2;
    if( strchr(zPath, ':') ) return 0;    /* unknown namespace: no guess */
    urlenc(zPath, enc, sizeof(enc));
    snprintf(out, nOut, "%s/file?name=%s&ci=tip", zBase, enc);
    return 1;
}

void viki_link_label(const char *zPath, char *out, size_t nOut){
    const char *rest;
    if( !zPath || !zPath[0] ){ snprintf(out, nOut, "%s", "(unknown)"); return; }
    if( has(zPath, "wiki:", &rest) ){ snprintf(out, nOut, "%s", rest); return; }
    /* Fossil's own UI abbreviates hashes; a full 64-hex id is unreadable and
    ** tells a person nothing a prefix does not. */
    if( has(zPath, "ticket:", &rest) || has(zPath, "forum:", &rest)
     || has(zPath, "ckin:", &rest)   || has(zPath, "note:", &rest)
     || has(zPath, "tchg:", &rest)   || has(zPath, "attach:", &rest) ){
        char kind[16];
        size_t k = (size_t)(strchr(zPath, ':') - zPath);
        if( k >= sizeof(kind) ) k = sizeof(kind) - 1;
        memcpy(kind, zPath, k); kind[k] = '\0';
        snprintf(out, nOut, "%s %.12s", kind, rest);
        return;
    }
    if( has(zPath, "uv:", &rest) ){ snprintf(out, nOut, "uv %s", rest); return; }
    snprintf(out, nOut, "%s", zPath);
}
