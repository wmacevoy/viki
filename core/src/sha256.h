/*
** sha256.h -- content_hash keying for viki's local cache (VIKI_DESIGN.md).
**
** Standalone vendored SHA-256 (src/sha256.c) -- no crypto library
** dependency, so viki's build doesn't need vendor/fossil-see (or
** anything else) built first. Not a security boundary -- content_hash is
** purely an internal cache key, never compared against or exposed as a
** Fossil artifact hash -- so a small vendored implementation is
** appropriate here even though it isn't independently security-audited
** the way a library like LibreSSL's is. See FINDINGS.md for why this
** replaced an earlier LibreSSL EVP_Digest-based version.
*/
#ifndef VIKI_SHA256_H
#define VIKI_SHA256_H

#include <stddef.h>

/* Hashes data[0..len) with SHA-256, writes a 64-char lowercase hex digest
** + NUL terminator (65 bytes) to hexout. Aborts the process via
** fossil_fatal-style fprintf+exit on the (should-never-happen) internal
** EVP failure case, since a bad content_hash silently corrupting the
** cache is worse than a loud, immediate exit. */
void viki_sha256_hex(const void *data, size_t len, char hexout[65]);

/* Streaming form, so a frame can be hashed without first being built in
** memory (REQ3/ENV-2). */
typedef struct {
    unsigned int state[8];
    unsigned long long bitlen;
    unsigned char buf[64];
    unsigned int buflen;
} sha256_ctx;
void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const unsigned char *data, size_t len);
void sha256_final(sha256_ctx *ctx, unsigned char out[32]);

#endif
