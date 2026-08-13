#include "sha256.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>

void viki_sha256_hex(const void *data, size_t len, char hexout[65]){
    static const char digits[] = "0123456789abcdef";
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashlen = 0;
    int i;

    if( EVP_Digest(data, len, hash, &hashlen, EVP_sha256(), NULL) != 1 || hashlen != 32 ){
        fprintf(stderr, "viki: EVP_Digest(SHA-256) failed unexpectedly\n");
        exit(1);
    }

    for( i = 0; i < 32; i++ ){
        hexout[i*2]   = digits[(hash[i] >> 4) & 0xf];
        hexout[i*2+1] = digits[hash[i] & 0xf];
    }
    hexout[64] = '\0';
}
