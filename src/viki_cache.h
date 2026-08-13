#ifndef VIKI_CACHE_H
#define VIKI_CACHE_H

/* `viki cache push` / `viki cache pull`: distribute zCacheDbPath as a
** Fossil unversioned file (D-12), via subprocess calls to `fossil uv`.
** Must be run from within an open Fossil checkout (cwd). Returns 0 on
** success, nonzero (and prints diagnostics to stderr) otherwise.
**
** Which fossil binary: $VIKI_FOSSIL_BIN if set, else "fossil-see" if
** found on PATH, else "fossil". See FINDINGS.md for why this isn't
** hardcoded to a single name. */
int viki_cmd_cache_push(const char *zCacheDbPath);
int viki_cmd_cache_pull(const char *zCacheDbPath);

/* Shared with viki_index.c's wiki/ticket extraction, which also shells
** out to `fossil`. */
const char *viki_fossil_binary(void);

/* $VIKI_FOSSIL_USER if set, else $USER, else "viki" -- ticket commands
** (unlike wiki commands, empirically) refuse to run at all without a
** resolvable user, even for read-only queries. See FINDINGS.md. */
const char *viki_fossil_user(void);

#endif
