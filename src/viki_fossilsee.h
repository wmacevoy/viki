#ifndef VIKI_FOSSILSEE_H
#define VIKI_FOSSILSEE_H

#include <stddef.h>   /* size_t -- a public header must not rely on the
                      ** includer having pulled this in already. */

/*
** viki_fossilsee -- OPTIONAL in-process Fossil SQL, loaded at runtime.
**
** WHY dlopen AND NOT A LINK-TIME DEPENDENCY. `viki` builds standalone on
** four platforms with no fossil-see prerequisite, and FINDINGS.md records
** that decoupling as deliberate. Linking libfossilsee would undo it for
** every platform at once, to speed up a path that is not viki's
** bottleneck. Loading it at runtime keeps the standalone build linking
** nothing (verified: only libSystem/libc), while a machine that HAS the
** library gets the in-process path for free. If the library is absent,
** damaged, or built from an incompatible ABI, every entry point here
** reports "unavailable" and the caller uses the subprocess exactly as
** before -- degraded mode is a required path here as it is for the model.
**
** WHAT THIS IS ACTUALLY FOR, and it is NOT speed. Measured on an
** encrypted repo with a raw key, one `fossil sql` subprocess costs
** ~6.5ms and the in-process call ~0.01ms -- but `viki index` issues only
** about seven of them per run, so the saving is ~45ms against a run that
** spends seconds embedding. The reason to prefer this path is
** CORRECTNESS: `fossil sql --readonly` exits 0 whether a query returned
** no rows or failed to prepare, and viki cannot tell those apart. That
** ambiguity is what made sweep_sources() delete every forum: row in the
** cache (FINDINGS.md), and it is why every extractor query has to append
** a `#viki-eof` sentinel. In-process, a failed prepare is a real error.
**
** The sentinel is deliberately still emitted and still checked, because
** the SAME SQL runs down both paths and the subprocess path still needs
** it. On the in-process path it is redundant, not wrong.
*/

/* Non-zero if the library loaded and its ABI matches what we compiled
** against. Safe to call repeatedly; the load is attempted at most once.
** Never fatal -- absence is a normal outcome. */
int viki_fossilsee_available(void);

/* One-line description of the loaded library, or why it is not loaded.
** For `viki index`'s stderr notice, so a user can tell WHICH path ran
** without reading the source. */
const char *viki_fossilsee_status(void);

/*
** Runs zSql in-process and returns the SAME BYTE STREAM `fossil sql
** --readonly` would have written to stdout: for each result row, column
** 0's bytes followed by one newline. That equivalence is the whole point
** -- framed_next() and all seven extractors parse the result without
** knowing which path produced it, so the two cannot drift in shape.
**
** Values are copied by LENGTH, not strlen: attachment and unversioned
** payloads routinely contain embedded NUL, and truncating there would
** silently corrupt an artifact mid-stream.
**
** Returns malloc'd bytes (caller frees) with *pnOut set, or NULL if the
** query failed OR the library is unavailable. Those two are distinguished
** by *pbUsed: non-zero means the in-process path RAN and the NULL is a
** real "not authoritative"; zero means nothing ran and the caller must
** fall back to the subprocess.
*/
char *viki_fossilsee_sql_framed(const char *zSql, size_t *pnOut, int *pbUsed);

/* Closes the repository if one is open. Idempotent. Call before exit so
** the encryption key is zeroed rather than left in the process image --
** libfossilsee's close path clears Fossil's process-global saved key. */
void viki_fossilsee_shutdown(void);

#endif
