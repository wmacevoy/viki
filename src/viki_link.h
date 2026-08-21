/*
** viki_link.h -- map an indexed source back to its page in Fossil's web UI.
**
** viki indexes nine artifact classes and answers across all of them, but a
** result was a dead string: `forum:633a8f62...` told you the answer exists
** and gave you no way to reach it. AGENTS.md has listed this under "Not yet
** built" since `viki serve` was written. A unified index deserves a unified
** responder -- one that hands back a place to go, not a hex identifier.
**
** THE URL SHAPES ARE NOT GUESSED. They were extracted from the vendored
** fossil-see binary's own href templates (`strings | grep '%R/'`), which is
** the only source that cannot drift from what the server actually routes:
**
**   %R/info/<hash>          check-ins, and the universal artifact page
**   %R/wiki?name=<Name>     wiki pages, keyed by NAME rather than hash
**   %R/tktview/<uuid>       tickets
**   %R/forumpost/<hash>     forum posts
**   %R/artifact/<hash>      raw artifact bytes
**   %R/file?name=<p>&ci=<c> a file at a check-in
**
** DEGRADES HONESTLY. With no base URL configured there is no link, and
** callers get NULL rather than a plausible-looking guess -- the same
** discipline as a missing embedding model. A wrong link is worse than none:
** it invites an agent to cite a page that does not exist.
*/
#ifndef VIKI_LINK_H
#define VIKI_LINK_H

#include <stddef.h>   /* size_t, in a PUBLIC header: do not rely on the includer */

/* Resolves the Fossil web-UI base URL: $VIKI_FOSSIL_URL, else NULL.
** Trailing slashes are stripped so callers can always append "/...". */
const char *viki_link_base(void);

/* Writes the URL for an indexed source path (a file path, or a virtual path
** such as `wiki:Name` / `forum:UUID`) into out. Returns 1 if a URL was
** written, 0 if none applies -- no base configured, or a class with no
** meaningful page. Never writes a partial URL on failure. */
int viki_link_for(const char *zBase, const char *zPath, char *out, size_t nOut);

/* A short human label for a source, for UI display: the wiki name, the
** ticket/forum/check-in id shortened to 12 hex like Fossil's own UI does, or
** the file path unchanged. Never fails; falls back to the path itself. */
void viki_link_label(const char *zPath, char *out, size_t nOut);

#endif
