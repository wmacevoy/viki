#include "viki_serve.h"
#include "viki_ask.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ---- growable byte buffer, used to build JSON/HTML response bodies ---- */

typedef struct { char *buf; size_t len; size_t cap; } sbuf;

static void sbuf_init(sbuf *s){
    s->cap = 256;
    s->buf = malloc(s->cap);
    s->len = 0;
    s->buf[0] = '\0';
}

static void sbuf_append(sbuf *s, const char *data, size_t n){
    if( s->len + n + 1 > s->cap ){
        while( s->len + n + 1 > s->cap ) s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void sbuf_puts(sbuf *s, const char *str){ sbuf_append(s, str, strlen(str)); }

static void sbuf_int(sbuf *s, int v){
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%d", v);
    sbuf_append(s, tmp, (size_t)n);
}

static void sbuf_double(sbuf *s, double v){
    char tmp[64];
    int n = snprintf(tmp, sizeof tmp, "%.6f", v);
    sbuf_append(s, tmp, (size_t)n);
}

/* JSON string literal, including the surrounding quotes. Escapes the
** minimum JSON requires (quote, backslash, control chars); everything
** else -- including non-ASCII UTF-8 bytes from indexed content -- passes
** through unescaped, which is valid JSON. */
static void sbuf_json_string(sbuf *s, const char *str){
    const unsigned char *p = (const unsigned char*)str;
    sbuf_append(s, "\"", 1);
    if( p ){
        while( *p ){
            switch( *p ){
                case '"':  sbuf_append(s, "\\\"", 2); break;
                case '\\': sbuf_append(s, "\\\\", 2); break;
                case '\n': sbuf_append(s, "\\n", 2); break;
                case '\r': sbuf_append(s, "\\r", 2); break;
                case '\t': sbuf_append(s, "\\t", 2); break;
                default:
                    if( *p < 0x20 ){
                        char esc[8];
                        int n = snprintf(esc, sizeof esc, "\\u%04x", *p);
                        sbuf_append(s, esc, (size_t)n);
                    }else{
                        sbuf_append(s, (const char*)p, 1);
                    }
            }
            p++;
        }
    }
    sbuf_append(s, "\"", 1);
}

/* ---- query-string parsing ---- */

static void url_decode(const char *in, char *out, size_t outCap){
    size_t o = 0;
    while( *in && o + 1 < outCap ){
        if( in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2]) ){
            char hex[3] = { in[1], in[2], '\0' };
            out[o++] = (char)strtol(hex, NULL, 16);
            in += 3;
        }else if( *in == '+' ){
            out[o++] = ' ';
            in++;
        }else{
            out[o++] = *in++;
        }
    }
    out[o] = '\0';
}

/* Finds key=value in a raw (still percent-encoded) query string and
** url-decodes the value into out. Returns 1 if found, 0 otherwise --
** including when query is NULL (no '?' in the request target). */
static int get_query_param(const char *query, const char *key, char *out, size_t outCap){
    size_t keylen = strlen(key);
    const char *p = query;
    if( !p ) return 0;
    while( *p ){
        const char *amp = strchr(p, '&');
        const char *segEnd = amp ? amp : p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(segEnd - p));
        if( eq ){
            size_t klen = (size_t)(eq - p);
            if( klen == keylen && strncmp(p, key, keylen) == 0 ){
                char raw[1024];
                size_t vlen = (size_t)(segEnd - (eq + 1));
                if( vlen >= sizeof raw ) vlen = sizeof(raw) - 1;
                memcpy(raw, eq + 1, vlen);
                raw[vlen] = '\0';
                url_decode(raw, out, outCap);
                return 1;
            }
        }
        p = amp ? amp + 1 : segEnd;
    }
    return 0;
}

/* ---- HTTP wire helpers ---- */

static void send_all(int fd, const char *buf, size_t len){
    size_t sent = 0;
    while( sent < len ){
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if( n < 0 ){
            if( errno == EINTR ) continue;
            return;
        }
        sent += (size_t)n;
    }
}

static void send_response(int fd, int status, const char *statusText,
                           const char *contentType, const char *body, size_t bodyLen){
    char header[256];
    int hlen = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, statusText, contentType, bodyLen);
    send_all(fd, header, (size_t)hlen);
    send_all(fd, body, bodyLen);
}

static void send_json_error(int fd, int status, const char *statusText, const char *msg){
    sbuf body;
    sbuf_init(&body);
    sbuf_puts(&body, "{\"error\":");
    sbuf_json_string(&body, msg);
    sbuf_puts(&body, "}");
    send_response(fd, status, statusText, "application/json", body.buf, body.len);
    free(body.buf);
}

/* ---- route handlers: fill an sbuf, return the HTTP status to send ---- */

static int handle_health(sbuf *body, viki_embedder *emb, const char *version){
    sbuf_puts(body, "{\"status\":\"ok\",\"version\":");
    sbuf_json_string(body, version);
    sbuf_puts(body, ",\"mode\":");
    sbuf_json_string(body, emb ? "hybrid" : "bm25");
    sbuf_puts(body, ",\"model_id\":");
    if( emb ) sbuf_json_string(body, viki_embedder_model_id(emb));
    else sbuf_puts(body, "null");
    sbuf_puts(body, "}");
    return 200;
}

static int handle_ask(sbuf *body, sqlite3 *db, viki_embedder *emb, const char *query){
    char qParam[1024], kParam[32];
    int k = 5, n, i;
    viki_ask_result results[VIKI_CANDIDATE_POOL];

    if( !get_query_param(query, "q", qParam, sizeof qParam) || !qParam[0] ){
        sbuf_puts(body, "{\"error\":\"missing required query parameter 'q'\"}");
        return 400;
    }
    if( get_query_param(query, "k", kParam, sizeof kParam) ) k = atoi(kParam);
    if( k < 1 ) k = 1;
    if( k > VIKI_CANDIDATE_POOL ) k = VIKI_CANDIDATE_POOL;
    viki_ask_info askInfo;
    double askMinCos = 0.0;
    askInfo.bestCos = VIKI_COS_NONE; askInfo.lowConfidence = 0; askInfo.nSuppressed = 0;

    {
        /* Uses the _opts entry point rather than the wrapper so the JSON can
        ** EXPLAIN a floored result. An empty "results" array with no reason
        ** is indistinguishable from an empty corpus, and an agent acting on
        ** this API needs "I found nothing worth acting on" to be a distinct,
        ** readable outcome from "I found nothing". */
        viki_ask_opts aopts;
        viki_ask_defaults(&aopts);
        n = viki_ask_query_opts(db, qParam, k, emb, results, VIKI_CANDIDATE_POOL,
                                &aopts, &askInfo);
        askMinCos = aopts.minCos;
    }

    sbuf_puts(body, "{\"query\":");
    sbuf_json_string(body, qParam);
    sbuf_puts(body, ",\"mode\":");
    sbuf_json_string(body, emb ? "hybrid" : "bm25");
    sbuf_puts(body, ",\"model_id\":");
    if( emb ) sbuf_json_string(body, viki_embedder_model_id(emb));
    else sbuf_puts(body, "null");
    /* Confidence, reported alongside the mode so a client can decide whether
    ** to act. best_cos is null in bm25-only mode: there is no cosine, and
    ** emitting 0 there would read as a real (orthogonal) measurement. */
    sbuf_puts(body, ",\"best_cos\":");
    if( askInfo.bestCos > VIKI_COS_NONE ) sbuf_double(body, askInfo.bestCos);
    else sbuf_puts(body, "null");
    sbuf_puts(body, ",\"min_cos\":"); sbuf_double(body, askMinCos);
    sbuf_puts(body, ",\"low_confidence\":");
    sbuf_puts(body, askInfo.lowConfidence ? "true" : "false");
    sbuf_puts(body, ",\"suppressed\":"); sbuf_int(body, askInfo.nSuppressed);
    sbuf_puts(body, ",\"results\":[");
    for( i = 0; i < n; i++ ){
        if( i ) sbuf_puts(body, ",");
        sbuf_puts(body, "{\"rank\":"); sbuf_int(body, i + 1);
        sbuf_puts(body, ",\"rrf\":"); sbuf_double(body, results[i].rrf);
        sbuf_puts(body, ",\"source\":"); sbuf_json_string(body, results[i].source);
        sbuf_puts(body, ",\"chunk_ix\":"); sbuf_int(body, results[i].chunk_ix);
        sbuf_puts(body, ",\"hash\":"); sbuf_json_string(body, results[i].hash);
        sbuf_puts(body, ",\"snippet\":"); sbuf_json_string(body, results[i].snippet);
        /* FRAGMENT provenance as explicit booleans, and `snippet` left
        ** exactly as retrieved.
        **
        ** The CLI decorates its excerpt with VIKI_MARK_* because its only
        ** output channel is a line of text a human or an agent reads. This
        ** is not that channel. A client already parsing `snippet` -- to
        ** quote it, diff it, or feed it to a model -- would silently start
        ** receiving marker words mixed into indexed content, with no way
        ** to tell viki's decoration from a document that happens to
        ** contain the same characters. Additive boolean fields cannot
        ** break such a client, and they carry the fact in a form it can
        ** branch on rather than one it must strip. A JSON client that
        ** wants the printed form composes it from these flags and the
        ** VIKI_MARK_* strings, which is exactly what the HTML page below
        ** does. chunk_count is 0 when the extent could not be determined
        ** (see fill_fragment_flags), never a guess. */
        sbuf_puts(body, ",\"fragment_head\":");
        sbuf_puts(body, (results[i].frag & VIKI_FRAG_HEAD) ? "true" : "false");
        sbuf_puts(body, ",\"fragment_tail\":");
        sbuf_puts(body, (results[i].frag & VIKI_FRAG_TAIL) ? "true" : "false");
        sbuf_puts(body, ",\"snippet_truncated\":");
        sbuf_puts(body, (results[i].frag & VIKI_FRAG_CUT) ? "true" : "false");
        sbuf_puts(body, ",\"chunk_count\":"); sbuf_int(body, results[i].chunk_count);
        sbuf_puts(body, "}");
    }
    sbuf_puts(body, "]}");
    return 200;
}

/* Full untruncated chunk text, keyed by (content_hash, chunk_ix) --
** viki_ask_result.snippet is deliberately short; agents that want the
** whole chunk (e.g. to quote it, or to fetch more context) come here.
** chunk_text doesn't vary by model_id (only the embedding does, see
** viki_db.c's schema), so this ignores model_id entirely. */
static int handle_chunk(sbuf *body, sqlite3 *db, const char *query){
    char hashParam[128], ixParam[32];
    int ix;
    sqlite3_stmt *st;
    int status;

    if( !get_query_param(query, "hash", hashParam, sizeof hashParam) || !hashParam[0] ){
        sbuf_puts(body, "{\"error\":\"missing required query parameter 'hash'\"}");
        return 400;
    }
    if( !get_query_param(query, "ix", ixParam, sizeof ixParam) ){
        sbuf_puts(body, "{\"error\":\"missing required query parameter 'ix'\"}");
        return 400;
    }
    ix = atoi(ixParam);

    if( sqlite3_prepare_v2(db,
            "SELECT chunk_text FROM viki_chunk WHERE content_hash=?1 AND chunk_ix=?2 LIMIT 1",
            -1, &st, NULL) != SQLITE_OK ){
        sbuf_puts(body, "{\"error\":\"internal error preparing query\"}");
        return 500;
    }
    sqlite3_bind_text(st, 1, hashParam, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, ix);

    if( sqlite3_step(st) == SQLITE_ROW ){
        const char *text = (const char*)sqlite3_column_text(st, 0);
        sbuf_puts(body, "{\"hash\":");
        sbuf_json_string(body, hashParam);
        sbuf_puts(body, ",\"chunk_ix\":"); sbuf_int(body, ix);
        sbuf_puts(body, ",\"text\":");
        sbuf_json_string(body, text ? text : "");
        sbuf_puts(body, "}");
        status = 200;
    }else{
        sbuf_puts(body, "{\"error\":\"no such chunk\"}");
        status = 404;
    }
    sqlite3_finalize(st);
    return status;
}

/* Static search-page HTML. Renders every server-supplied field
** (source path, snippet, chunk text -- none of it trusted markup, since
** it's free text pulled from indexed files/wiki/tickets/forum posts)
** via textContent, never innerHTML, so indexed content containing HTML/
** script-like text is displayed literally rather than executed. No
** external requests (fonts, CDN scripts) -- fully self-contained.
**
** The fragment markers keep that property and sharpen it. Each one is its
** own <span>, built by frag() with its own textContent, so the marker is
** a separate DOM node from the excerpt rather than characters spliced
** into it -- which is both why no innerHTML is needed and why a document
** whose text literally contains "<<document continues below>>" cannot
** pass itself off as viki's marker: that text lands in the excerpt node,
** unstyled. The marker strings come from the VIKI_MARK_* macros by C
** string concatenation rather than being retyped here, so this page and
** `viki ask` cannot drift apart. */
static const char *const VIKI_SERVE_HTML =
    "<!doctype html>\n"
    "<html><head>\n"
    "<meta charset='utf-8'>\n"
    "<title>viki</title>\n"
    "<style>\n"
    "  body { font-family: system-ui, sans-serif; max-width: 780px; margin: 2rem auto; padding: 0 1rem; }\n"
    "  #q { width: 70%; padding: 0.4rem; }\n"
    "  button { padding: 0.4rem 0.8rem; }\n"
    "  .hit { border-top: 1px solid #ccc; padding: 0.6rem 0; }\n"
    "  .meta { color: #666; font-size: 0.85em; }\n"
    "  .snippet { white-space: pre-wrap; }\n"
    "  .frag { color: #a33; font-style: italic; }\n"
    "</style>\n"
    "</head><body>\n"
    "<h1>viki</h1>\n"
    "<form id='f'>\n"
    "  <input id='q' type='text' placeholder='ask viki...' autofocus>\n"
    "  <button type='submit'>Search</button>\n"
    "</form>\n"
    "<div id='status' class='meta'></div>\n"
    "<div id='results'></div>\n"
    "<script>\n"
    "var f = document.getElementById('f');\n"
    "var qEl = document.getElementById('q');\n"
    "var statusEl = document.getElementById('status');\n"
    "var resultsEl = document.getElementById('results');\n"
    "f.addEventListener('submit', function(e){\n"
    "  e.preventDefault();\n"
    "  var q = qEl.value.trim();\n"
    "  if (!q) return;\n"
    "  statusEl.textContent = 'searching...';\n"
    "  resultsEl.textContent = '';\n"
    "  fetch('/api/ask?q=' + encodeURIComponent(q) + '&k=10')\n"
    "    .then(function(r){ return r.json(); })\n"
    "    .then(renderResults)\n"
    "    .catch(function(err){ statusEl.textContent = 'error: ' + err; });\n"
    "});\n"
    "function frag(t){\n"
    "  var s = document.createElement('span');\n"
    "  s.className = 'frag';\n"
    "  s.textContent = t;\n"
    "  return s;\n"
    "}\n"
    "function renderResults(data){\n"
    "  if (data.error) { statusEl.textContent = 'error: ' + data.error; return; }\n"
    "  statusEl.textContent = data.mode + (data.model_id ? (' / ' + data.model_id) : '') +\n"
    "    ' -- ' + data.results.length + ' result(s)';\n"
    "  resultsEl.textContent = '';\n"
    "  data.results.forEach(function(hit){\n"
    "    var div = document.createElement('div');\n"
    "    div.className = 'hit';\n"
    "    var meta = document.createElement('div');\n"
    "    meta.className = 'meta';\n"
    "    meta.textContent = '#' + hit.rank + '  rrf=' + hit.rrf.toFixed(4) +\n"
    "      '  ' + hit.source + ' (chunk ' + hit.chunk_ix +\n"
    "      (hit.chunk_count ? ' of ' + hit.chunk_count : '') + ')';\n"
    "    div.appendChild(meta);\n"
    "    var snippet = document.createElement('div');\n"
    "    snippet.className = 'snippet';\n"
    "    if (hit.fragment_head) snippet.appendChild(frag('" VIKI_MARK_HEAD " '));\n"
    "    snippet.appendChild(document.createTextNode(hit.snippet));\n"
    "    if (hit.snippet_truncated) snippet.appendChild(frag(' " VIKI_MARK_CUT "'));\n"
    "    if (hit.fragment_tail) snippet.appendChild(frag(' " VIKI_MARK_TAIL "'));\n"
    "    div.appendChild(snippet);\n"
    "    resultsEl.appendChild(div);\n"
    "  });\n"
    "}\n"
    "</script>\n"
    "</body></html>\n";

static void handle_client(int fd, sqlite3 *db, viki_embedder *emb, const char *version){
    char req[16384];
    ssize_t total = 0;
    char method[16], target[4096], httpver[16];
    char *lineEnd;
    char *qmark;
    const char *query;

    while( total < (ssize_t)sizeof(req) - 1 ){
        ssize_t r = recv(fd, req + total, sizeof(req) - 1 - (size_t)total, 0);
        if( r <= 0 ) break;
        total += r;
        req[total] = '\0';
        if( strstr(req, "\r\n\r\n") ) break;
    }
    if( total <= 0 ) return;
    req[total] = '\0';

    lineEnd = strstr(req, "\r\n");
    if( lineEnd ) *lineEnd = '\0';
    method[0] = target[0] = httpver[0] = '\0';
    if( sscanf(req, "%15s %4095s %15s", method, target, httpver) != 3 ){
        send_json_error(fd, 400, "Bad Request", "malformed request line");
        return;
    }

    if( strcmp(method, "GET") != 0 ){
        send_json_error(fd, 405, "Method Not Allowed", "only GET is supported");
        return;
    }

    query = NULL;
    qmark = strchr(target, '?');
    if( qmark ){ *qmark = '\0'; query = qmark + 1; }

    if( strcmp(target, "/") == 0 ){
        send_response(fd, 200, "OK", "text/html; charset=utf-8", VIKI_SERVE_HTML, strlen(VIKI_SERVE_HTML));
        return;
    }

    {
        sbuf body;
        int status = -1;
        sbuf_init(&body);

        if( strcmp(target, "/api/health") == 0 ){
            status = handle_health(&body, emb, version);
        }else if( strcmp(target, "/api/ask") == 0 ){
            status = handle_ask(&body, db, emb, query);
        }else if( strcmp(target, "/api/chunk") == 0 ){
            status = handle_chunk(&body, db, query);
        }

        if( status > 0 ){
            const char *statusText = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
            send_response(fd, status, statusText, "application/json", body.buf, body.len);
            free(body.buf);
            return;
        }
        free(body.buf);
    }

    send_json_error(fd, 404, "Not Found", "no such route");
}

int viki_cmd_serve(sqlite3 *db, viki_embedder *emb, const char *zHost, int port, const char *zVersion){
    int listenFd;
    struct sockaddr_in addr;
    int one = 1;

    signal(SIGPIPE, SIG_IGN); /* a client closing mid-response must not kill the server */

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if( listenFd < 0 ){ perror("viki serve: socket"); return 1; }
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if( inet_pton(AF_INET, zHost, &addr.sin_addr) != 1 ){
        fprintf(stderr, "viki serve: invalid --host '%s' (expected an IPv4 address, e.g. 127.0.0.1)\n", zHost);
        close(listenFd);
        return 1;
    }

    if( bind(listenFd, (struct sockaddr*)&addr, sizeof addr) != 0 ){
        perror("viki serve: bind");
        close(listenFd);
        return 1;
    }
    if( listen(listenFd, 16) != 0 ){
        perror("viki serve: listen");
        close(listenFd);
        return 1;
    }

    fprintf(stderr, "viki serve: listening on http://%s:%d (mode=%s%s%s)\n",
            zHost, port, emb ? "hybrid" : "bm25",
            emb ? ", model_id=" : "", emb ? viki_embedder_model_id(emb) : "");
    fprintf(stderr, "viki serve: GET /  GET /api/ask?q=&k=  GET /api/chunk?hash=&ix=  GET /api/health\n");
    fprintf(stderr, "viki serve: loopback-only, no auth -- do not expose this port to a network. Ctrl-C to stop.\n");

    for( ;; ){
        int clientFd = accept(listenFd, NULL, NULL);
        if( clientFd < 0 ){
            if( errno == EINTR ) continue;
            perror("viki serve: accept");
            continue;
        }
        handle_client(clientFd, db, emb, zVersion);
        close(clientFd);
    }
}
