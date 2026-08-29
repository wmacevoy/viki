/* viki_task.h -- a note that someone owes, to someone, by a date.
**
** WHY THIS IS A SEPARATE ASSERTION KIND AND NOT COLUMNS ON A NOTE:
**
** viki_assert has eight columns and none of them is `due`, deliberately --
** the whole point of one assertion type is that notes, calendar events and
** provenance share it. Adding note-specific columns is how that stops being
** true. So a task is a SUBTYPE: kind='task', with its fields in `body` as
** JSON, reached with json_extract() exactly as jsCalendar is. SQLite is the
** parser here too (core-probe C7).
**
** WHY STRUCTURING IS A NEW ASSERTION RATHER THAN AN EDIT:
**
** The predecessor rewrote the capture markdown files in place when you ran
** `viki structure`, which is how it once DELETED user text (FINDINGS.md).
** Here the raw capture stays exactly as it was written and the task is a
** SECOND assertion whose `supersedes` names it. Grow-only, so union is still
** merge, and the act of structuring is itself in the record. viki_core.h
** already states the resolution rule this depends on: "a superseded note
** leaves the ledger".
**
** WHICH MAKES ARRIVAL FREE. The predecessor spelled it `--closes`; here a
** thing that arrives is just another assertion superseding the task, and the
** task leaves the ledger because nothing has to be deleted for it to.
*/
#ifndef VIKI_TASK_H
#define VIKI_TASK_H
#include "viki_core.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const struct VikiAssertVftbl *vftbl;
    char        zId[VIKI_ID_HEX+1];
    const char *zTs;
    const char *zSupersedes;      /* the capture this structures, or the task
                                  ** this one retires. Either way it leaves. */
    /* ---- the fields a ledger needs. Only zText is required. ------------ */
    const char *zText;
    const char *zWho;             /* NULL/"" == the reader's own: "mine"    */
    const char *zDue;             /* YYYY-MM-DD or YYYY-MM-DDThh:mm:ssZ     */
    const char *zPlace;
    const char *zState;
    const char *zChannel;
    const char *zKey;
    /* ---- filled by viki_task(); never caller-set ----------------------- */
    char       *zJson;            /* canon(): the body, built by json_object */
    char       *zCompose;         /* text(): what gets chunked and embedded  */
} VikiTask;
extern const struct VikiAssertVftbl vikiTaskVftbl;

/* Stores the task and frees the two internal buffers before returning.
**
** A MALFORMED @due IS REFUSED, NOT STORED. The predecessor accepted
** "@due next Tuesday", sorted it lexically, and reported a phantom OVERDUE
** in the morning brief every day with no way to see why (FINDINGS.md). A
** ledger that is wrong in the direction of anxiety is worse than one that
** rejects a line. */
VikiStatus viki_task(VikiTask *p, char *zIdOut);

/* ---- reading the ledger --------------------------------------------- */
typedef struct {
    const char *zId, *zTs, *zWho, *zDue, *zPlace, *zState, *zText;
    int bMine;      /* zWho empty, or equal to the zMe passed to viki_ledger */
} VikiTaskRow;
typedef int (*viki_task_row)(void *pApp, const VikiTaskRow *pRow);

/* LIVE tasks only -- anything another assertion supersedes is excluded, which
** is the one property a ledger cannot do without. Ordered by due date with
** undated last, NOT by write time. Returns rows through xRow; a non-zero
** return from xRow stops the walk. */
VikiStatus viki_ledger(const char *zMe, viki_task_row xRow, void *pApp);

#ifdef __cplusplus
}
#endif
#endif /* VIKI_TASK_H */
