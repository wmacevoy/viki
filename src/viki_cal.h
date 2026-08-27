#ifndef VIKI_CAL_H
#define VIKI_CAL_H

#include <sqlite3.h>

/*
** THE ICS SHREDDER -- iCalendar text in, the ASSERTION TIER out.
**
** CALENDAR_DESIGN_V2.md SS 5 splits calendar state into three tiers. This
** builds the middle one and deliberately stops there:
**
**   artifacts     the bytes             immutable, never invalidated
**   ASSERTIONS    every component ever written, shredded, PRE-RESOLUTION
**   occurrences   windowed expansion in a stated zone   <- NOT THIS
**
** The boundary is VIEWER DEPENDENCE. An occurrence depends on a zone, a
** window and a tzdata version, so by D-11's own argument it is not shareable
** and does not belong beside rows that are. Everything here is a deterministic
** function of the input bytes.
**
** WHY THIS IS IN viki AND NOT A CONNECTOR. QUEUE SS 52: the adapters FETCH and
** the shredder DERIVES, and deriving a cleaned projection is the half
** VIKIVERSE_V1 SS 2.3 explicitly keeps. Parsing "DTSTART;TZID=America/Denver:
** 20260827T140000" into its fields needs no opinion, which is SCOPES SS 3's
** test. Deciding whether that blocks your afternoon DOES, and this file has no
** column for it -- TRANSP, STATUS and PARTSTAT are stored as the facts they
** are, and nothing here scores them.
**
** IT IS ALSO WHY THIS IS ON THE CRITICAL PATH EITHER WAY. Google Calendar and
** O365 both speak iCalendar on the wire, so SS 2.3's ingest needs a shredder
** whichever way CALENDAR_DESIGN.md's OQ-1 resolves. D-2 stands; this does not
** touch it.
*/

/* Shred one iCalendar document into cal_event / cal_attendee.
**
** zSource is recorded per row as provenance -- WHERE these bytes came from,
** for coverage. It is not identity: identity is (uid, recurrence_id, sequence,
** dtstamp), so the same event ingested twice from two places is one row.
**
** Returns 0 on success. *pnEvents / *pnAttendees receive the counts actually
** written (may be NULL). A document with no components is NOT an error -- an
** empty calendar is a legitimate answer -- but it reports 0 and says so. */
int viki_cal_shred(sqlite3 *db, const char *zText, size_t nText,
                   const char *zSource, int *pnEvents, int *pnAttendees);

/* `viki calendar shred <file> [--source NAME]` */
int viki_cmd_cal_shred(sqlite3 *db, int argc, char **argv);

/* `viki calendar events [--from ISO] [--to ISO] [--all] [--json]`
**
** Prints the RESOLVED view: RFC 5546's (UID, RECURRENCE-ID) ->
** max(SEQUENCE, DTSTAMP) applied as a read-time projection over the
** assertions, never as a write-time overwrite. Superseded rows stay in the
** table, which is what makes the table grow-only and therefore mergeable
** (SYNC.md SS 2) -- the same shape as viki_note's supersession. */
int viki_cmd_cal_events(sqlite3 *db, int argc, char **argv);

#endif
