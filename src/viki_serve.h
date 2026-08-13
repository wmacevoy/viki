#ifndef VIKI_SERVE_H
#define VIKI_SERVE_H

#include <sqlite3.h>
#include "embed.h"

/* `viki serve`: a minimal, single-threaded, loopback-only HTTP server
** exposing the exact same retrieval viki_ask_query() gives the CLI --
** both as an HTML search page for humans (GET /) and as a small JSON
** API for scripts/agents (GET /api/ask, /api/chunk, /api/health). No
** authentication, no static-file serving (every response is generated
** from the constant HTML page or from DB queries, so there's no path-
** traversal surface), no HTTPS -- deliberately: this process is never
** meant to face the internet directly. To actually serve it over the
** internet, put it behind a TLS-terminating, auth-checking reverse
** proxy (server/setup-viki-serve.sh wires this up with Caddy + Basic
** Auth, reusing the same hub Caddy instance server/SERVER_SETUP.md
** already runs in front of `fossil server` -- see FINDINGS.md for why
** this repo didn't hand-roll TLS/auth in C instead). zHost defaults to
** 127.0.0.1 in viki.c; don't change that in a deployed systemd unit --
** let the proxy be the only internet-facing listener. Binds
** zHost:port, then blocks running a blocking accept() loop until a
** fatal socket error or the process is killed (Ctrl-C / SIGINT).
** Returns 0 on a clean shutdown (unreachable in practice today --
** there's no in-process stop signal yet), nonzero on a startup failure
** (socket/bind/listen). zVersion is only used for the /api/health
** payload. */
int viki_cmd_serve(sqlite3 *db, viki_embedder *emb, const char *zHost, int port, const char *zVersion);

#endif
