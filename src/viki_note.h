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

/* One projected note, as borrowed pointers valid only for the callback call.
** A callback rather than an allocated array: the CLI prints as it goes and
** the HTTP layer appends JSON as it goes, so neither needs the rows to
** outlive the scan, and neither has to free anything. */
typedef struct {
    const char *id, *ts, *type, *place, *who, *due, *state, *text, *closes;
    const char *claimed, *lease, *challenge, *stolen;
} viki_note_row;

typedef void (*viki_note_cb)(void *pCtx, const viki_note_row *row);

/* Filters. Empty/NULL means "no filter on that field".
**
** `closes` is the SUPERSESSION filter and it reads backwards from the others:
** every other field asks "which notes look like this", `closes` asks "which
** note RETIRED that one". It matches the raw note id -- unlike place/type/
** who/state it is never normalize_key'd, because an id is an id. */
typedef struct {
    const char *place, *type, *state, *who, *since, *grep, *closes;
    int bLast;      /* only the most recent match */
    int nMax;
    int bPending;   /* untyped captures only -- the agent's work queue */
    int bUnclaimed; /* only notes with NO holder. THE gap the roleplay found:
                    ** three agents independently discovered that
                    ** `--state open` lists CLAIMED work, because claiming
                    ** sets who and never touches state -- and that `--who ""`
                    ** cannot express it, since an empty filter means "no
                    ** filter on that field". The one question a work queue
                    ** must answer was the one it could not. */
    int bStale;     /* only claims whose declared lease has lapsed */
} viki_note_filter;

/* PURE query: no output of its own. `viki notes`, `viki structure --pending`
** and the HTTP API all run through this, so there is one filter
** implementation rather than three that drift. Returns rows visited, or -1. */
int viki_note_query(sqlite3 *db, const viki_note_filter *f, viki_note_cb cb, void *pCtx);

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
/* CLAIMS, LEASES AND STEALING.
**
** A work queue shared by unreliable peers needs an answer to "is this claim
** dead or is its holder just offline?" -- and viki exists precisely for peers
** that go offline for long stretches, so a fixed global timeout cannot tell
** those apart. The claimer therefore declares its OWN responsiveness:
**
**   viki structure <id> --who alice --lease 1m   "I'm online, challenge me fast"
**   viki structure <id> --who alice --lease 2d   "I'm going to the north pasture"
**
** A lease is a promise about availability, written by the only party who
** knows. Everything else follows from it:
**
**   --heartbeat        the holder renews, saying "still here"
**   --challenge bob    refused while the lease is live; otherwise recorded
**                      with a timestamp, which is the "are you still on
**                      this?" a peer is owed before being displaced
**   --steal bob --grace 1m --lease 5m
**                      succeeds only if the lease lapsed AND a challenge has
**                      aged past --grace (default 60s). Records @stolen-from, because a
**                      steal is a SUPERSESSION, not an overwrite -- the same
**                      spine as --closes and the fprev fix. Silent
**                      reassignment would make the record lie about who held
**                      the work.
**
** --who now REFUSES to overwrite an existing holder (compare-and-set). That
** refusal is what makes the steal path the only sanctioned way to displace
** someone, rather than a courtesy the tool merely hopes for.
**
** Clock skew is real and unarbitrable here: leases are compared across
** machines with no authority to appeal to. A negative interval means "cannot
** adjudicate" and REFUSES the steal, failing toward leaving work alone.
** And a lapsed lease is evidence of a broken promise, not of death -- which
** is the weaker claim, and the one recorded. */
typedef struct {
    const char *zType, *zPlace, *zWho, *zDue, *zState, *zCloses;
    const char *zLease;      /* duration: 30s / 5m / 2h / 3d */
    const char *zChallenge;  /* challenger's name */
    const char *zSteal;      /* stealer's name */
    const char *zGrace;      /* how long an unanswered challenge must age before a
                             ** steal is permitted. SEPARATE from zLease: the grace
                             ** is how long you wait for the OLD holder, the lease is
                             ** how long the NEW one promises to be reachable. I first
                             ** overloaded --lease for both and the steal silently
                             ** refused, because "--steal bob --lease 1m" was read as
                             ** "wait a minute" rather than "hold it for a minute". */
    int bHeartbeat;
    int bForce;              /* bypass compare-and-set; for a holder correcting itself */
} viki_structure_opts;

void viki_structure_defaults(viki_structure_opts *o);

int viki_cmd_structure_apply(sqlite3 *db, const char *zId, const char *zType,
                             const char *zPlace, const char *zWho, const char *zDue,
                             const char *zState, const char *zCloses);

int viki_cmd_structure_opts(sqlite3 *db, const char *zId, const viki_structure_opts *opts);

/* `viki notes [filters]` -- the aggregation and recency surface.
** NULL/0 means "no filter on that field". bLast returns only the most recent
** match, which is the "who baited rimi site last" shape.
**
** zCloses is the other half of `structure --closes`: writing the link made
** supersession explicit, but without a filter the link was write-only -- a
** closed note said only THAT it was retired, and finding what retired it
** meant grepping the capture files by hand for `@closes <id>`, which is
** exactly the "read the files, not the projection" habit this table exists
** to end. `viki notes --closes <old-id>` answers it as a query. */
/* Filter-struct form, so a new predicate never changes a signature. */
int viki_cmd_notes_filter(sqlite3 *db, const viki_note_filter *f);

int viki_cmd_notes(sqlite3 *db, const char *zPlace, const char *zType,
                   const char *zState, const char *zWho, const char *zSince,
                   const char *zGrep, const char *zCloses, int bLast, int nMax);

#endif
