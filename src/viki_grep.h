/*
** viki_grep.h -- POSIX regular-expression search over the indexed corpus.
**
** Why this exists when `rg` already does regex beautifully: ripgrep searches
** FILES ON DISK. viki's index covers nine artifact classes, and five of them
** -- check-in comments, tech notes, ticket changes, attachments and
** unversioned files -- plus wiki pages, tickets and forum posts, are not
** files and cannot be grepped at all. `viki grep` is regex over the whole
** repository's history and discussion, not just its checkout, which is a
** search no file-oriented tool can perform.
**
** It complements `viki ask` rather than competing with it. `ask` is fuzzy
** and RANKED (BM25 + vector, rank-fused); `grep` is exact and UNRANKED --
** every chunk whose text matches, in index order. Reach for `grep` when you
** know the literal string (a symbol, a UUID, an error message) and for
** `ask` when you know only the idea.
**
** Engine is POSIX regcomp/regexec from libc -- deliberately NOT a vendored
** regex library. Every platform viki builds for (macOS, Linux, MSYS2's MSYS
** environment) ships POSIX regex, so this costs no download, no submodule
** and no build step. The tradeoff is POSIX ERE syntax, not PCRE: character
** classes are `[[:digit:]]` rather than `\d`, and there is no lookaround.
** That is a real limitation and is documented in usage().
*/
#ifndef VIKI_GREP_H
#define VIKI_GREP_H

#include "sqlite3.h"

/* Registers a two-argument regexp(pattern, text) SQL function, which also
** makes SQLite's infix `text REGEXP pattern` operator work (SQLite resolves
** that operator to exactly this function name). Registering it on the cache
** db means agents driving .viki/cache.db directly get regex too, not only
** callers of `viki grep`. Compiled patterns are cached per-argument via
** sqlite3_set_auxdata, so a scan compiles the pattern once, not once a row. */
int viki_grep_register(sqlite3 *db);

/* `viki grep "<pattern>"`: prints every matching chunk in `ask`'s hit-line
** shape so the two commands' output can be consumed by the same parser.
** nMax <= 0 means unlimited. Returns 0 on success, nonzero on a bad pattern. */
int viki_cmd_grep(sqlite3 *db, const char *zPattern, int nMax, int bIgnoreCase,
                  const char *zSourceLike, int nChars,
                  const char *zSince, const char *zUntil, int bNewest, int bShowTime);

#endif
