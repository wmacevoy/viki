# Calendar design — synced artifacts + local SQLite projection

**Problem.** Fossil syncs *artifacts* (immutable blobs), not database rows. A
calendar must be queryable ("what's on Thursday?") and offline-mergeable (two
devices edit events on a plane; both survive). So the design splits into:

1. **Source of truth (synced):** each event change is an immutable artifact.
2. **Projection (local, per device):** a SQLite table rebuilt from artifacts —
   fast to query, safe to throw away, never synced.

This is exactly Fossil's own ticket architecture: ticket-change artifacts sync;
every clone re-materializes a local `ticket` table; web "reports" are SQL over
that table. We reuse the pattern (and possibly the machinery — see §5).

## 1. Field model (steal from RFC 5545 / iCalendar VEVENT)

| Field | Type | Notes |
|-------|------|-------|
| `uid` | text | stable event id (uuid) — the "ticket number" |
| `summary` | text | title |
| `dtstart` | text (ISO 8601, with TZ) | start |
| `dtend` / `duration` | text | end or duration |
| `allday` | bool | |
| `rrule` | text | RFC 5545 recurrence rule, e.g. `FREQ=WEEKLY;BYDAY=TU` |
| `exdate` | text list | cancelled occurrences |
| `recurrence_id` | text | when this artifact *overrides one occurrence* of a recurring event |
| `location` | text | |
| `description` | text | markdown |
| `status` | enum | confirmed / tentative / cancelled |
| `attendees` | text list | fossil usernames (humans + agents) |
| `categories` | text list | project tags (e.g. `monument-rock`, `icpc`) |
| `mtime`, `author` | | from the artifact envelope |

Keep TZ rules simple: store IANA zone name + local time; expand in app.

## 2. Local projection schema (steal from Android CalendarProvider)

```sql
-- Rebuilt from artifacts; DROP + rebuild is always safe.
CREATE TABLE cal_event (
  uid TEXT PRIMARY KEY,
  summary TEXT, dtstart TEXT, dtend TEXT, allday INT,
  rrule TEXT, exdate TEXT, recurrence_id TEXT,
  location TEXT, description TEXT, status TEXT,
  attendees TEXT, categories TEXT,
  mtime REAL, author TEXT
);

-- Pure cache: recurring events expanded to concrete rows over a window
-- (e.g. -1yr .. +2yr), re-expanded lazily as the window moves.
CREATE TABLE cal_instance (
  uid TEXT REFERENCES cal_event(uid),
  begin_utc INT,   -- epoch seconds, indexed
  end_utc INT
);
CREATE INDEX cal_instance_begin ON cal_instance(begin_utc);
```

"What's on Thursday" = `SELECT ... FROM cal_instance JOIN cal_event USING(uid)
WHERE begin_utc < :thu_end AND end_utc > :thu_start;`

## 3. Merge semantics (why this beats .ics files)

Each artifact records *field deltas* ("uid X: dtstart → 2pm"), timestamped and
attributed. On sync, all artifacts from all parties arrive; the projection
replays them in timestamp order. Consequences:

- Two devices editing **different fields** of one event: both changes apply. No conflict.
- Two devices editing the **same field**: last-write-wins by artifact time —
  deterministic, same answer on every device, and full history is preserved in
  the timeline (nothing is silently destroyed; the "losing" value is one click away).
- Deletion = `status: cancelled` (tombstone), never physical removal.

A one-file-per-event `.ics` design would instead produce *file-level* merge
conflicts needing UI. That's the main reason to prefer the artifact/DB design.

## 4. Recurrence

Store `rrule` verbatim; never materialize occurrences into the synced layer.
Expansion happens only in the local `cal_instance` cache (there are solid RRULE
libraries in Dart, e.g. the `rrule` package). Single-occurrence edits ("move
just next Tuesday's meeting") use `recurrence_id` overrides, same as iCalendar.

## 5. Implementation choice (open — OQ-1)

**Option A — literal Fossil tickets.** Define the fields above as a custom
ticket schema. Free: sync, field-merge, `fossil ticket` CLI (agents can create
events from a shell), server-side SQL reports ("agenda" report in the web UI),
attribution, history. Cost: ticket semantics are a little clunky (everything
looks like a "ticket"), and the app reads Fossil's ticket tables rather than
its own.

**Option B — own artifact type.** Events as small structured files in a hidden
`.cal/` directory of the checkout (one JSON per event, field-delta entries), app
maintains its own projection. Free: full control of semantics. Cost: we write
and maintain the merge/projection code ourselves, and the server web UI shows
nothing calendar-ish without custom work.

**Recommendation: start with A.** It is dramatically less code, it's
battle-tested merge logic, agents get a CLI for free, and browser-only
collaborators get queryable reports on day one. If ticket semantics chafe,
the field model above ports unchanged to Option B later — the projection
schema and app UI don't care where the deltas come from.

**A third option is specified in `CALENDAR_DESIGN_V2.md`** (on `main` since
2026-08-27; it lived only on branch `calendar/interchange-v2` before that, so
this pointer named a file no one could open from a main checkout). Not adopted; D-2 stands. It matters here for one
reason: SS 3 above rules `.ics` out because a *one-file-per-event* design
produces file-level merge conflicts. That objection is correct about the design
it describes and does not reach Option C, which merges at component granularity
by `(UID, RECURRENCE-ID) -> max(SEQUENCE, DTSTAMP)` per RFC 5546. The real
remaining trade is that `ticketchng` gives field-level merge and Option C gives
component-level. Read `CALENDAR_DESIGN_V2.md` SS 4 and SS 7 before re-deciding
OQ-1, and QUEUE SS 52 for why part of it is needed whichever way it goes.

## 6. What the app does on each sync

1. `fossil sync` (via FFI) — artifacts flow both ways.
2. Replay new event artifacts → upsert `cal_event`.
3. Re-expand `cal_instance` for touched uids.
4. Refresh views; badge anything changed by others since last look.
