/* viki-http -- the smallest HTTP/HTTPS GET that can pull a tribe's cache.
**
** TLS is libtls, from the LibreSSL this project already vendors. That is the
** whole reason it is only a couple of hundred lines: libtls is OpenBSD's
** deliberately-small client API, so there is no context/BIO/verify-callback
** ceremony to get subtly wrong. NO NEW DEPENDENCY, and no libcurl.
**
** CERTIFICATE VERIFICATION IS ON AND THERE IS NO FLAG TO TURN IT OFF. libtls
** verifies by default and this file never calls tls_config_insecure_*. A
** puller that fetches a corpus over a connection it did not authenticate is
** worse than one that refuses, and an --insecure flag is the kind of thing
** that gets added "just for testing" and then ships.
**
** Redirects are NOT followed. Fossil serves /uv/ directly; a redirect here
** means something is in the way (a proxy, a login wall) and silently chasing
** it can land the corpus request somewhere unintended. The status is returned
** so the caller can say so.
*/
#include "viki-http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <tls.h>

static const char B64A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64(const char *in, char *out){
    size_t n = strlen(in), i; int o = 0;
    for( i = 0; i < n; i += 3 ){
        unsigned v = (unsigned)(unsigned char)in[i] << 16;
        size_t rem = n - i;
        if( rem > 1 ) v |= (unsigned)(unsigned char)in[i+1] << 8;
        if( rem > 2 ) v |= (unsigned char)in[i+2];
        out[o++] = B64A[(v>>18)&63]; out[o++] = B64A[(v>>12)&63];
        out[o++] = rem > 1 ? B64A[(v>>6)&63] : '=';
        out[o++] = rem > 2 ? B64A[v&63] : '=';
    }
    out[o] = 0;
}

/* A tiny reader that is either a socket or a TLS context, so the request and
** response code below is written once instead of twice. */
typedef struct { int fd; struct tls *tls; } Conn;
static ssize_t conn_write(Conn *c, const void *p, size_t n){
    return c->tls ? tls_write(c->tls, p, n) : write(c->fd, p, n);
}
static ssize_t conn_read(Conn *c, void *p, size_t n){
    return c->tls ? tls_read(c->tls, p, n) : read(c->fd, p, n);
}

int viki_http_get(const char *zUrl, unsigned char **ppBody, int *pnBody){
    return viki_http_get_cond(zUrl, NULL, ppBody, pnBody, NULL, 0);
}

int viki_http_get_cond(const char *zUrl, const char *zEtag,
                       unsigned char **ppBody, int *pnBody,
                       char *outEtag, size_t etagCap){
    char scheme[8], userinfo[256] = "", host[256], port[16], path[2048];
    const char *p = zUrl, *at, *slash, *colon;
    Conn c = { -1, NULL };
    struct tls_config *cfg = NULL;
    struct addrinfo hints, *res = NULL, *ai;
    unsigned char *buf = NULL;
    int cap = 0, len = 0, status = -1, hdrEnd = -1, i;
    char req[4096]; int rl = 0;

    *ppBody = NULL; *pnBody = 0;

    if( !strncmp(p, "https://", 8) ){ strcpy(scheme, "https"); strcpy(port, "443"); p += 8; }
    else if( !strncmp(p, "http://", 7) ){ strcpy(scheme, "http"); strcpy(port, "80"); p += 7; }
    else { fprintf(stderr, "viki-http: only http:// and https:// (got %s)\n", zUrl); return -1; }

    slash = strchr(p, '/');
    at = memchr(p, '@', slash ? (size_t)(slash - p) : strlen(p));
    if( at ){
        size_t n = (size_t)(at - p);
        if( n >= sizeof(userinfo) ) return -1;
        memcpy(userinfo, p, n); userinfo[n] = 0;
        p = at + 1;
        slash = strchr(p, '/');
    }
    {
        size_t hn = slash ? (size_t)(slash - p) : strlen(p);
        if( hn >= sizeof(host) ) return -1;
        memcpy(host, p, hn); host[hn] = 0;
    }
    colon = strrchr(host, ':');
    if( colon && !strchr(colon, ']') ){
        snprintf(port, sizeof(port), "%s", colon + 1);
        *(char*)colon = 0;
    }
    snprintf(path, sizeof(path), "%s", slash ? slash : "/");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if( getaddrinfo(host, port, &hints, &res) != 0 ){
        fprintf(stderr, "viki-http: cannot resolve %s\n", host); return -1;
    }
    for( ai = res; ai; ai = ai->ai_next ){
        c.fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if( c.fd < 0 ) continue;
        if( connect(c.fd, ai->ai_addr, ai->ai_addrlen) == 0 ) break;
        close(c.fd); c.fd = -1;
    }
    freeaddrinfo(res);
    if( c.fd < 0 ){ fprintf(stderr, "viki-http: cannot connect to %s:%s\n", host, port); return -1; }

    if( !strcmp(scheme, "https") ){
        if( tls_init() != 0 ){ fprintf(stderr, "viki-http: tls_init failed\n"); goto done; }
        cfg = tls_config_new();
        c.tls = tls_client();
        /* Defaults verify the chain, the name and the validity window. This
        ** file deliberately offers no way to relax any of that. */
        if( !cfg || !c.tls || tls_configure(c.tls, cfg) != 0 ){
            fprintf(stderr, "viki-http: tls setup failed\n"); goto done;
        }
        if( tls_connect_socket(c.tls, c.fd, host) != 0 ){
            fprintf(stderr, "viki-http: TLS to %s failed: %s\n", host, tls_error(c.tls));
            goto done;
        }
    }

    rl += snprintf(req + rl, sizeof(req) - rl,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: viki-pull/1\r\n"
        "Accept: */*\r\nConnection: close\r\n", path, host);
    if( userinfo[0] ){
        char enc[512];
        b64(userinfo, enc);
        rl += snprintf(req + rl, sizeof(req) - rl, "Authorization: Basic %s\r\n", enc);
    }
    if( zEtag && zEtag[0] )
        rl += snprintf(req + rl, sizeof(req) - rl, "If-None-Match: %s\r\n", zEtag);
    rl += snprintf(req + rl, sizeof(req) - rl, "\r\n");
    if( conn_write(&c, req, (size_t)rl) < 0 ){ fprintf(stderr, "viki-http: write failed\n"); goto done; }

    for(;;){
        ssize_t got;
        if( len + 65536 > cap ){ cap = cap ? cap * 2 : 262144; buf = realloc(buf, (size_t)cap); if( !buf ) goto done; }
        got = conn_read(&c, buf + len, 65536);
        if( got == TLS_WANT_POLLIN || got == TLS_WANT_POLLOUT ) continue;
        if( got <= 0 ) break;
        len += (int)got;
    }
    if( len < 12 ){ fprintf(stderr, "viki-http: short response\n"); goto done; }
    status = atoi((const char*)buf + 9);
    for( i = 0; i + 3 < len; i++ ){
        if( buf[i]=='\r' && buf[i+1]=='\n' && buf[i+2]=='\r' && buf[i+3]=='\n' ){ hdrEnd = i + 4; break; }
    }
    if( hdrEnd < 0 ){ fprintf(stderr, "viki-http: no header terminator\n"); status = -1; goto done; }

    /* Chunked transfer would need de-chunking; Fossil serves uv blobs with a
    ** Content-Length, so this refuses rather than silently returning framing
    ** bytes as if they were the corpus. */
    {
        char *hdr = malloc((size_t)hdrEnd + 1);
        if( hdr ){
            memcpy(hdr, buf, (size_t)hdrEnd); hdr[hdrEnd] = 0;
            if( strcasestr(hdr, "\r\nTransfer-Encoding: chunked") ){
                fprintf(stderr, "viki-http: chunked responses are not supported\n");
                free(hdr); status = -1; goto done;
            }
            free(hdr);
        }
    }
    /* Lift the ETag before the body, so a later run can ask "still this?" */
    if( outEtag && etagCap ){
        char *hdr = malloc((size_t)hdrEnd + 1);
        outEtag[0] = 0;
        if( hdr ){
            char *e;
            memcpy(hdr, buf, (size_t)hdrEnd); hdr[hdrEnd] = 0;
            e = strcasestr(hdr, "\r\nETag:");
            if( e ){
                char *v = e + 8, *end;
                while( *v == ' ' ) v++;
                end = strstr(v, "\r\n");
                if( end && (size_t)(end - v) < etagCap ){
                    memcpy(outEtag, v, (size_t)(end - v));
                    outEtag[end - v] = 0;
                }
            }
            free(hdr);
        }
    }
    if( status == 304 ){ *pnBody = 0; *ppBody = NULL; goto done; }
    *pnBody = len - hdrEnd;
    *ppBody = malloc((size_t)(*pnBody ? *pnBody : 1));
    if( !*ppBody ){ status = -1; goto done; }
    memcpy(*ppBody, buf + hdrEnd, (size_t)*pnBody);

done:
    if( c.tls ){ tls_close(c.tls); tls_free(c.tls); }
    if( cfg ) tls_config_free(cfg);
    if( c.fd >= 0 ) close(c.fd);
    free(buf);
    return status;
}
