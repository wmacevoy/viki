/*
** sha256.h -- content_hash keying for viki's local cache (VIKI_DESIGN.md).
**
** Thin wrapper around LibreSSL's EVP SHA-256 (already linked in via
** fossil-see's build -- vendor/fossil-see/vendor/libressl-build-out --
** so this adds no new dependency and no hand-rolled crypto code, matching
** how pizza-party-vote-fossil's ppv-crypto module uses LibreSSL EVP rather
** than vendoring its own hash implementation).
**
** Not a security boundary -- purely an internal cache key -- but there is
** no reason to use anything less vetted than what's already on hand.
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

#endif
