/*
** viki_note.h -- the capture loop: offline capture, online structure, and a
** query surface that answers questions retrieval cannot.
**
** WHY THIS IS NOT `viki ask`. Two real questions exposed the gap:
**
**   "what needs to be done in monument rocks?"  -- wants a LIST of open items
**       filtered by place. `ask` returned five chunks that matched the WORDS
**       "needs / remember to / warn to", including an observation ("new foal
**       in rimi's band") and a schedule. Similarity ranking cannot filter by
**       state, and cannot aggregate.
**
**   "who baited rimi site last?"                -- wants a temporal superlative.
**       `ask` appeared to answer it and did not: adding an OLDER note with
**       richer bait vocabulary took rank 1 away from the actually-most-recent
**       one. Nothing in similarity search knows what "last" means.
**
** So this is a structured projection alongside retrieval, not a replacement
** for it. Ask `viki ask` what you half-remember; ask `viki notes` what is
** open, where, whose, and when.
**
** THE THREE-STAGE LOOP, and which stage needs what:
**
**   CAPTURE (offline, no model, no network, no fossil):  `viki capture "..."`
**       appends a block to captures/YYYY-MM.md in the checkout. A plain file
**       on purpose -- it is durable, it commits to fossil like any other
**       content, and `viki index` already indexes it, so a capture is
**       retrievable by `ask` even if nobody ever structures it.
**
**   STRUCTURE (online, an agent):  the agent appends `@key value` lines to a
**       block -- @type/@place/@who/@due/@state. viki has no LLM and never
**       will; every caller is one. Same division as the HyDE convention.
**
**   PROJECT (local, derived):  `viki index` rebuilds viki_note from the files.
**       D-10 clean -- the files are the truth, the table is disposable.
**
** WHY A FULL REBUILD rather than incremental invalidation: the note table is
** small (one row per capture, not per chunk), and a rebuild cannot leave a
** stale row behind. The incremental path in viki_index.c earned its
** complexity by guarding against wrong deletion; there is no reason to pay
** that here, and every reason not to add a second thing that can go stale.
*/
#ifndef VIKI_NOTE_H
#define VIKI_NOTE_H

#include <sqlite3.h>

/* Default capture file directory, relative to the checkout root. */
#define VIKI_CAPTURE_DIR "captures"

/* `viki capture "<text>" [--place P] [--type T] [--who W] [--due D] [--state S]`.
** Everything but the text is optional -- capture must never block on structure
** the capturer does not have. Appends and returns 0, or nonzero on I/O error. */
int viki_cmd_capture(const char *zDir, const char *zText,
                     const char *zPlace, const char *zType,
                     const char *zWho, const char *zDue, const char *zState);

/* Rebuilds viki_note from every capture block under zDir. Called by
** `viki index` after the chunk work. Returns rows projected, or -1. */
int viki_note_reindex(sqlite3 *db, const char *zDir);

/* `viki structure --pending [--k N]`: lists captures that carry no @type yet,
** i.e. the work queue FOR THE AGENT. Output is one note per line, id first,
** so a caller can iterate it without parsing prose. */
int viki_cmd_structure_pending(sqlite3 *db, int nMax);

/* `viki structure <note-id> [--type T] [--place P] [--who W] [--due D]
**                           [--state S] [--closes ID]`
**
** Rewrites the block in its capture file, adding or replacing @key lines.
** viki owns this write rather than telling an agent to hand-edit, because a
** malformed block silently becomes a different note (or merges with its
** neighbour), and capture files are the TRUTH here -- the projection is
** disposable but they are not. Writes via temp file + rename so an
** interrupted structure pass cannot leave a half-written capture file.
**
** --closes is supersession, made explicit: it marks the TARGET note closed
** and records the link on this one. "tire for utv fixed" closing "utv tire
** is flat" is the same relation as a forum edit superseding its original and
** a corrected measurement superseding a stale one -- the spine this project
** has now met at four different layers. Recording WHICH note closed a thing
** is what makes the history answerable later, rather than just the state.
*/
int viki_cmd_structure_apply(sqlite3 *db, const char *zId, const char *zType,
                             const char *zPlace, const char *zWho, const char *zDue,
                             const char *zState, const char *zCloses);

/* `viki notes [filters]` -- the aggregation and recency surface.
** NULL/0 means "no filter on that field". bLast returns only the most recent
** match, which is the "who baited rimi site last" shape. */
int viki_cmd_notes(sqlite3 *db, const char *zPlace, const char *zType,
                   const char *zState, const char *zWho, const char *zSince,
                   const char *zGrep, int bLast, int nMax);

#endif
