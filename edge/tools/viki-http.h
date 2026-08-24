#ifndef VIKI_HTTP_H
#define VIKI_HTTP_H
#include <stddef.h>
/* Fetches zUrl into a malloc'd buffer. Returns the HTTP status, or -1 on a
** transport failure (with a message on stderr). Caller frees *ppBody.
** http:// and https:// only; userinfo in the URL becomes Basic auth. */
int viki_http_get(const char *zUrl, unsigned char **ppBody, int *pnBody);
/* Conditional form. zEtag may be NULL/empty. Returns 304 and no body when the
** resource is unchanged, which is what makes "sync whenever you can" cheap
** rather than a repeated full download. outEtag (cap bytes) receives the
** response ETag when one is offered. */
int viki_http_get_cond(const char *zUrl, const char *zEtag,
                       unsigned char **ppBody, int *pnBody,
                       char *outEtag, size_t cap);
#endif
