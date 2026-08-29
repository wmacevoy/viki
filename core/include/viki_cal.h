/*
** viki_cal.h -- calendar assertions. A VikiAssert subtype, nothing more.
**
** Everything structural is core's: grow-only storage, content addressing,
** union-is-merge, and resolution at read time. This header adds the RFC 5545
** shape on top and no new machinery.
*/
#ifndef VIKI_CAL_H
#define VIKI_CAL_H

#include "viki_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIKI_CAL_KEY  512
#define VIKI_CAL_RANK 64

/* Times are held AS WRITTEN with their IANA TZID and NEVER as an offset: an
** offset is a fact about a moment, a TZID is a fact about a rule, and only
** the second survives the rule changing. `zXForm` is one of
** utc | zoned | floating | date -- and a floating time is not a UTC time. */
typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;
    /* identity */
    const char *zUid, *zRecurrenceId;
    int         iSequence;
    const char *zDtstamp;
    /* what it is */
    const char *zComponent;              /* VEVENT | VTODO                  */
    const char *zSummary, *zText;
    const char *zStatus, *zTransp, *zOrganizer;
    /* when -- as written */
    const char *zDtstart, *zDtstartTzid, *zDtstartForm;
    const char *zDtend,   *zDtendTzid,   *zDtendForm;
    const char *zDue,     *zDueTzid,     *zDueForm;
    const char *zDuration, *zRrule;
    /* provenance and the bytes identity is computed over */
    const char *zSource, *zRaw;
    char        zKeyBuf[VIKI_CAL_KEY];
    char        zRankBuf[VIKI_CAL_RANK];
} VikiEvent;

extern const struct VikiAssertVftbl vikiEventVftbl;

/* Creates the viki_event PROJECTION. Truth is the assertion; this table is
** derived, deletable and rebuildable -- the same standing as viki_chunk. */
VikiStatus viki_cal_attach(sqlite3 *db);

/* Shreds iCalendar text into assertions. REFUSES input with no
** BEGIN:VCALENDAR rather than reporting an empty calendar: an adapter that
** fetched an HTML error page must not read as a quiet day. */
VikiStatus viki_cal_shred(const char *zIcs, size_t nIcs,
                          const char *zSource, int *pnAdded);

/* Rebuilds viki_event from the assertion store. Safe to call at any time;
** this is what makes the projection disposable rather than precious. */
VikiStatus viki_cal_reproject(int *pnRows);

/* The RESOLVED events whose start falls in [zFrom, zTo] -- either bound may
** be NULL. Bounds are accepted in ISO ("2026-09-01T00:00") or RFC 5545 basic
** ("20260901T000000") form and normalised, because '-' sorts below every
** digit and comparing the two forms directly silently returns nothing.
** RESOLVED means one row per (UID, RECURRENCE-ID): superseded assertions stay
** in the store and do not appear here. */
typedef int (*viki_cal_row)(void *pApp, const VikiEvent *pEv);
VikiStatus viki_cal_events(const char *zFrom, const char *zTo,
                           viki_cal_row x, void *pApp);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_CAL_H */
