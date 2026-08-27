# CALENDAR_DESIGN_V2 -- the interchange layer: iCalendar as Fossil artifacts

**Status: SPEC, on a branch, NOT ADOPTED.** D-2 stands. This exists so the v2
push can pick it up or reject it on the merits, without re-deriving it.

`CALENDAR_DESIGN.md` remains correct about the field model, the projection
(`cal_event` / `cal_instance`), and the merge goals. This document changes one
thing: it supplies a **third option for SS 5's open OQ-1**, and it answers the
specific objection in SS 3 that ruled `.ics` out.

Every source claim below is cited to `vendor/fossil-see/vendor/fossil/src` at
`FOSSIL_REF` 42e3bc1, so it can be re-verified in minutes rather than trusted.

---

## 0. What this is and is not

**It is an interchange convention** -- how a calendar is represented *as Fossil
artifacts* so that two independent implementations converge on the same
calendar. It is the layer with the longest half-life: it constrains stored data
and other implementations, so it should change approximately never.

**It is not** the projection schema, the verb surface, or the UI. Those sit
above it, change faster, and are versioned separately (SS 8).

**It does not decide OQ-1.** See SS 6.

---

## 1. The split that makes the rest work

Two independent time axes, and Fossil only has an opinion about one:

- **Transaction time** -- when the repo agreed a fact was true. The D card and
  the artifact DAG. Immutable, reproducible, already perfect.
- **Valid time** -- when the thing happens. `DTSTART`. Payload.

Fossil's native tense is transaction time, which is the correct and only tense
an artifact store should have. **The mistake to avoid is putting occurrence time
into artifact metadata**, which is exactly what the technote `E` card does
(`manifest.c:652` -- "the timestamp of the event ... distinct from the D
timestamp"). One timestamp, no duration, no recurrence, no way to express "on
Tuesday we agreed that Thursday moved". It dead-ends because it conflates the
axes.

Put every valid-time semantic in the payload, where a standard already defines
it, and the problem dissolves without touching the artifact model.

---

## 2. Normative rules

### 2.1 Storage

- A calendar is a **Fossil wiki page** whose mimetype is `text/calendar`.
- Page names are prefixed `calendar/` (e.g. `calendar/family`). Fossil reserves
  only `checkin/*` and `branch/*`; `calendar/*` is unclaimed.
- The page content is one complete `VCALENDAR` object.

**One wiki page = one VCALENDAR object = one calendar collection.** This 1:1:1
is the whole storage model.

Requires a one-line Fossil patch: `wiki_filter_mimetypes()` (`wiki.c:176`)
allowlists only `azStyles` plus `text/x-markdown` and `text/plain`; anything
else silently becomes `text/x-fossil-wiki`. Add `text/calendar` to that second
branch. Nothing else is needed -- see SS 4.

### 2.2 Calendar identity

The `VCALENDAR` carries RFC 7986 `UID` and `NAME`. The `UID` is the calendar's
identity and is what membership refers to; it survives a page rename, which a
page name cannot.

### 2.3 Membership

**Membership is containment.** A component belongs to the calendar whose
`VCALENDAR` contains it. There is no per-component back-pointer in iCalendar and
none should be invented -- RFC 7986's applicability table puts `UID`, `NAME`,
`DESCRIPTION`, `CATEGORIES`, `SOURCE`, `URL`, `REFRESH-INTERVAL` and
`LAST-MODIFIED` at **VCALENDAR level only**; only `COLOR`, `IMAGE` and
`CONFERENCE` reach into components. CalDAV (RFC 4791) answers the same way: a
calendar is a collection, membership is containment.

Cross-cutting tags that are *not* calendar membership use component-level
`CATEGORIES` (RFC 5545 SS 3.8.1.2). Note that RFC 7986 defines a
VCALENDAR-level `CATEGORIES` as well: same spelling, different applicability,
two properties.

### 2.4 Write rules

These are obligations on every writer. They are load-bearing, not style -- SS 3
shows what breaks without each.

- **W1. Always update `DTSTAMP`** on any write.
- **W2. Bump `SEQUENCE`** on significant change: `DTSTART`, `DTEND`/`DUE`,
  `RRULE`, `STATUS`, `LOCATION` (RFC 5545 SS 3.8.7.4).
- **W3. Never delete by omission.** Removal is `STATUS:CANCELLED`.
- **W4. Preserve unrecognized property lines verbatim.** A write is
  fetch-page -> splice one component -> put-page, so anything the writer did not
  understand must survive the round trip byte-for-byte.
- **W5. Omit `METHOD`.** Absent means "stored calendar"; present means an iTIP
  scheduling message in transit. These pages are stores.

### 2.5 Read rules

- **R1. Union over ALL versions** of all `calendar/*` pages -- not over leaves,
  not over tips.
- **R2. Group by `(UID, RECURRENCE-ID)`; the survivor is `max(SEQUENCE,
  DTSTAMP)`.** This is RFC 5546 precedence, cited not invented.
- **R3. Membership is the set union of containing `VCALENDAR` UIDs across all
  surviving copies** -- not the container of the winning copy.
- **R4. As-of is a filter**, not a code path: `artifact_mtime <= T` inside R1.

### 2.6 Time

- **T1. No `VTIMEZONE` is stored.** Zoned values carry the IANA `TZID` only.
  This is RFC 7809's posture ("time zones by reference").
- **T2. Never store an offset.** `America/Denver` is a fact about intent;
  `-0600` is a computed value with a shelf life.
- **T3. Four forms, chosen deliberately:**

  | Form | Use for |
  |---|---|
  | `DTSTART:...Z` | fixed instants -- a freeze, a cutoff, a deadline |
  | `DTSTART;TZID=...` | wall-clock commitments -- "2pm Denver" |
  | `DTSTART:...` (floating) | "09:00 wherever I am" |
  | `DTSTART;VALUE=DATE` | dates are dates |

- **T4. Recurrence steps the wall clock**, and each occurrence resolves its own
  offset. "Every Friday at 09:00" stays 09:00 across a DST transition while the
  underlying instant shifts. This only works because the offset is computed per
  occurrence rather than stored once on the master.
- **T5. Materialize `VTIMEZONE` at export.** Strict RFC 5545 requires a `TZID`
  to reference a `VTIMEZONE` in the same object, so a bare `TZID` is
  non-conformant in a standalone `.ics`. Generate it from *current* tzdata at the
  moment a file is handed to Google or Apple, never at write time.

---

## 3. Why the read rule is what it is

The single most surprising finding, and the one that forces W3 and R1.

**Fossil's wiki has no merge, no conflict detection, and no stale-edit check.**
`grep -ni "fork\|merge\|conflict\|stale"` across all of `wiki.c` returns zero
hits. `wiki_cmd_commit()` (`wiki.c:2101`) emits exactly **one** P card:

```c
if( rid ){
  zUuid = db_text(0, "SELECT uuid FROM blob WHERE rid=%d", rid);
  blob_appendf(&wiki, "P %s\n", zUuid);
}
```

And every caller resolves `rid` as the **current tip** server-side
(`json_wiki.c:378`, `wiki.c:1029`, `wiki.c:2474`). A client cannot supply the
version it edited against -- there is no `If-Match`, no expected-version
parameter, no conflict rejection. Fossil itself comments on the adjacent race
two lines below the save.

So two loss paths exist today, for every wiki page in every Fossil repo:

1. **Same server, concurrent writers** -- writes chain linearly and the later
   document wholly replaces the earlier one.
2. **Two clones, edit offline, sync** -- both artifacts claim the same parent,
   so the page genuinely **forks into two leaves**; display picks
   `ORDER BY mtime DESC` (`wiki.c:1668`) and the other leaf is silently
   shadowed.

A leaf-only read therefore **drops events that were written and never
retracted**. R1 exists because of this, W3 exists because R1 makes omission
unreadable as an assertion, and W1/W2 exist because R2 needs a total order.

**The payoff is the argument for the whole design.** Same storage, same write
path, opposite outcome:

| | prose wiki page | `text/calendar` page |
|---|---|---|
| Item identity | none | `UID` |
| Item versioning | none | `SEQUENCE` + `DTSTAMP` |
| Deletion | omission, indistinguishable from loss | `STATUS:CANCELLED` |
| Concurrent edit | clobbered | union reader reconstructs both |

A clobbered paragraph is gone. A clobbered `VEVENT` is still asserted in an
ancestor artifact, still carries its merge keys, and returns on the next read.
The calendar is **strictly better-behaved than the wiki it is stored in**.

**Corollary worth keeping: ingest order does not matter.** Because resolution is
`(SEQUENCE, DTSTAMP)` and not arrival order, a shredder may insert an assertion
whenever it arrives -- including old artifacts landing out of order during a
sync -- and the resolved view is unchanged. Anything mtime-ordered would not
have this property.

---

## 4. Decision register

**Storage**

- **iCalendar payload inside wiki artifacts** (SS 2.1). *Because* wiki storage
  already syncs, versions, and retains a DAG, and the payload carries its own
  merge keys, so it converges where the wiki itself cannot. **Refused: the
  technote `E` card as the time carrier** -- one timestamp in artifact metadata,
  no duration, no recurrence, and it conflates the two time axes (SS 1).
- **One page per calendar, not per event** (SS 2.1). *Because* delta compression
  makes a large document cheap, and the union resolves at component granularity
  regardless, so page size is a human-organisation question rather than a
  correctness one. **Refused: one page per event** -- N artifacts and N wiki
  names buys nothing the union does not already give.
- **A one-line mimetype patch** (SS 2.1). *Because* `wiki_filter_mimetypes()` is
  the single chokepoint for all 13 call sites -- CLI, `/wikiedit`, JSON API,
  technotes, and the search indexer. **Refused: adding `text/calendar` to
  `azStyles`** -- that puts it in the wiki editor's mimetype dropdown and invites
  hand-editing ICS in a textarea; `wiki_render_by_mimetype()`'s existing `else`
  branch already emits escaped `<pre>`, so no render case is needed.

**Merge**

- **Union over all versions** (R1). *Because* Fossil linearizes concurrent saves
  onto the tip, so a leaf-only read silently drops a clobbered write's events
  (SS 3). **Refused: union over leaves** -- demonstrably lossy, and the loss is
  invisible.
- **`(UID, RECURRENCE-ID)` -> `max(SEQUENCE, DTSTAMP)`** (R2). *Because* RFC
  5546 already specifies exactly this precedence and every CalDAV client
  implements it. **Refused: inventing merge semantics** -- there is nothing to
  gain and an interop cost.
- **Component-level merge.** *Because* it needs no per-property provenance, which
  iCalendar does not carry. **Refused: file-level merge** -- that is the design
  `CALENDAR_DESIGN.md` SS 3 correctly rejects, and this is not it.
  **Acknowledged cost:** this is coarser than `ticketchng`'s field-level merge.
  Two devices editing *different fields* of one event both apply under tickets;
  here the higher `SEQUENCE` takes the whole component. Tickets genuinely win
  that axis. See SS 7.
- **No change to Fossil's write path.** *Because* the read rule makes optimistic
  concurrency unnecessary. **Refused: patching `json_wiki_save` to accept a
  parent version** -- a fork of upstream behaviour to buy something already
  bought.

**Membership and identity**

- **Containment, with RFC 7986 `UID` for rename-safety** (SS 2.3). *Because*
  that is the standard's own model and a wiki page is a container. **Refused: a
  per-component calendar-reference property** -- verified against RFC 7986's
  applicability table; no such property exists, and CalDAV answers by containment
  too.
- **Two reductions over the same data** (R2 vs R3). *Because* a genuinely shared
  event has one component `UID` in two calendars: it must collapse to one event
  while keeping both memberships. **Refused: reading membership off the winning
  copy** -- sharing an event into a second calendar would then silently remove it
  from the first.

**Time**

- **`TZID` by reference, no stored `VTIMEZONE`** (T1). *Because* `VTIMEZONE` is a
  materialized snapshot of rules that change several times a year, so a stored
  copy is a cached answer to a question whose answer expires. **Refused:
  embedding `VTIMEZONE`** -- also refused by RFC 7809 for bandwidth, but the
  reason that matters here is currency.
- **Render with current tzdata; accept that instants are not reproducible**
  (T1, SS 8). *Because* RFC 7808 provides **no mechanism to request a historical
  tzdata version** -- the spec returns "the current version of time zone data it
  has". **Refused: pinning a tzdata version for bitemporal reproducibility** --
  unavailable, and wrong anyway: a calendar wants current rules.
  **Honest consequence:** the *assertions* are reproducible as-of any T; the
  *instants* are not, because they depend on state the repo does not contain.

**Boundary**

- **Named time bands stay out of the synced repo.** *Because* VIKIVERSE_V1 SS 2.4
  settled this class: `viki coverage` ships per-source times with **no
  thresholds**, and the brief in `assistant/` adds "STALE", since "a threshold is
  a judgment about Warren's day rather than a fact about the corpus". "Afternoon"
  and "open enough" are the same judgment. **Refused: a `cal_band` table in the
  repo** -- it would be the third boundary crossing.
  The line: **viki returns busy intervals and the gaps between them, in a stated
  zone over a stated range, with coverage attached. `assistant/` decides that a
  two-hour Tuesday gap counts as an open afternoon.** This also makes SS 2.5 work
  -- "you're free Thursday" is precisely the completeness claim that must carry
  its sources.

---

## 5. Query representation (informative)

Not normative, but it is what the interchange is *for*. Three tiers, and the
boundary between them is exactly the viewer-dependence boundary:

| Tier | Holds | Lifetime | Invalidated by |
|---|---|---|---|
| Artifacts | ICS inside wiki artifacts | immutable | never |
| Assertions / `cal_event` | every component ever written, shredded, pre-resolution | permanent, rebuildable | schema bump |
| Occurrences / `cal_instance` | windowed expansion in a stated zone | ephemeral | window, zone, as-of, **tzdata version** |

Two consequences:

- **The occurrence tier must denormalize local wall-clock fields**
  (`local_date`, `local_dow`, `local_minute_of_day`). "Tuesday" and "afternoon"
  are wall-clock predicates; storage is instants; SQLite has no IANA zone support
  (`datetime()` knows UTC and `'localtime'`, which defers to the process `TZ`).
  So **zone is a parameter of the materializing function, not a display
  concern**.
- **"Open" is the complement of a merged interval union.** Indexing events does
  not make gap-finding cheap; materializing *merged* busy intervals does -- and
  merging removes the interval-overlap indexing problem rather than working
  around it. Merge per person, not per calendar; availability is a property of a
  human.

Availability gating deserves promoted columns, rolled into one `busy_weight`
so the oracle is one indexed predicate: `TRANSP` (the stock RFC 5545 property
that means "does this block time", routinely ignored), `STATUS` (`CANCELLED`
tombstone, `TENTATIVE` soft block), the viewer's own `PARTSTAT` (a declined
invitation does not consume the afternoon), and the rule that a `VTODO` with
`DUE` but no `DTSTART` is a **point**, not an interval.

`VEVENT` and `VTODO` want different projections. `VEVENT` is an interval and its
queries are overlaps. `VTODO` is a deadline with state, and
`RELATED-TO;RELTYPE=PARENT|CHILD|SIBLING` shredded into an edge table gives a
dependency DAG -- a recursive CTE then answers "what is blocking this" and "what
does slipping this delay". That is the argument for VTODO-as-table.

The tier boundary is also the **shareability** boundary, by D-11's own reasoning:
an embedding cache is shareable because it is a deterministic function of
`(content_hash, model_id, chunk_params)` with no viewer-dependent input. The
occurrence tier has viewer-dependent inputs (zone, window) and is therefore not
shareable by the same argument. The assertion tier is.

---

## 6. What this does NOT decide

**OQ-1 stays open, and this does not force it.** `CALENDAR_DESIGN.md` SS 5
already notes the field model "ports unchanged ... the projection schema and app
UI don't care where the deltas come from". That is stronger than it looks:
**the projection has two producers.**

VIKIVERSE_V1 SS 2.3's actual v1 job is *ingest* -- "Google Calendar + O365
ingest, derived not mirrored". Both speak iCalendar on the wire. **So an
ICS -> projection shredder is required regardless of how D-2 resolves**, because
it is the ingest path. Build that first: it serves the presenting complaint
immediately and is the same component that later serves authored events, whether
those arrive as tickets or as `calendar/*` pages.

D-2 -- "calendar as queryable ticket-style artifacts plus a local projection" --
therefore stands, and nothing here needs it revisited on a schedule. The one
piece of genuinely new information for whenever it *is* revisited is in SS 3:
`CALENDAR_DESIGN.md` SS 3 rejected `.ics` because "a one-file-per-event `.ics`
design would produce *file-level* merge conflicts needing UI". That objection is
correct about the design it describes and does not reach this one.

---

## 7. Open questions

1. **Is component-level merge acceptable?** Tickets give field-level. The loss
   is concurrent edits to *different fields of the same event within one sync
   window*, which is rare in a family-scale calendar and where the losing value
   stays one click away in history -- the same argument `CALENDAR_DESIGN.md` SS 3
   makes for tickets. Rare is not never. Unmeasured.
2. **CRLF round-trip through the web path.** The `W` card is byte-length-prefixed
   (`manifest.c:1026`), so RFC 5545's 75-octet folding survives at the artifact
   layer, and `blob_to_lf_only()` is not called from `wiki.c`. The CGI/textarea
   path is **unverified** and is where CRLF discipline usually dies.
3. **R-Tree availability** for interval indexing, if merged busy intervals are
   ever not enough. FTS5 is demonstrably compiled in (`chat.c:314`,
   `search.c:1759`); `rtree` is used nowhere in Fossil's source. Verify before
   depending on it.
4. **Is the assertion tier worth sharing across peers** as uv blobs, the way the
   embedding cache is? It is viewer-independent, so it qualifies. Not costed.

---

## 8. Versioning

Three contracts that move at different rates and must not share a number:

| Contract | Analogue in Fossil | Breaks | Rate |
|---|---|---|---|
| `calendar_format` -- this document | `CONTENT_SCHEMA` (`schema.c:47`) | stored data, other implementations | ~never |
| `calendar_schema` -- projection + verbs | `AUX_SCHEMA_MIN/MAX` (`schema.c:48-49`) | queries | occasionally |
| fossil-see version + `FOSSIL_REF` | -- | builds | independent |

Fossil's own comment is the precedent: content schema changes need special
procedures, whereas for the aux schema "all we need to do is rebuild the
database" (`schema.c:44-47`). Note it declares a MIN/MAX **compatibility window**
rather than a point version -- the mechanism a consumer needs in order to signal
"I need attention" before it breaks.

**`viki-manifest` is the vehicle.** It already pins `model_id`, model path and
checksum, chunking params and an ndvss/schema version; it is both a uv blob and
a committed artifact for tamper-evidence (`SYNC.md:107,122`); it is signed. Add:

- `calendar_format`, `calendar_schema` -- semver.
- `tzdata_version` -- **same class as `model_id`**: an external dependency that
  invalidates a derived cache with no content change. Without it, a peer caching
  occurrence results has no invalidation signal.
- fossil-see version / `FOSSIL_REF` as **provenance only**, never asserted
  against. Coupling the calendar version to Fossil's means a Fossil bump implies
  a calendar change that did not happen, and consumers learn to ignore the check.

The epoch pattern already does the hard part: `model_id` "lets two epochs coexist
during migration. Never a flag-day for content -- only for the disposable layer."
A `calendar_schema` bump is an overnight re-derive, not a migration.

**Two consumer classes, two contracts.** V2_DESIGN SS 6b keeps `viki sql` off the
MCP face on aggregation grounds. So:

- **Clone-holding agents** (ARCHITECTURE.md's agent path: a full clone in the
  agent's workspace) have the SQLite file and will write SQL. For these the
  schema *is* the API and needs the semver discipline above -- expose **versioned
  views**, give base tables no compatibility promise, and say so. Agents must
  name columns; `SELECT *` makes every additive change breaking.
- **stdio/MCP consumers** get the verb surface only. Their contract is
  VIKIVERSE_V1 SS 2.11's, which that document already names as the v1 work.

The dangerous failure mode for both is not a rename -- that errors loudly. It is
**semantic drift under stable names**: deciding `TENTATIVE` counts as busy
changes every scheduling answer with no error anywhere. Any change to what
`busy` means is MAJOR.

---

## 9. Prototype

A working reference implementation of the union reducer, the four time forms,
recurrence with `EXDATE` and `RECURRENCE-ID` overrides, and the leaf-only-vs-all-
versions comparison:

**In this repo:** `experiments/calendar-interchange-prototype.html` -- one
self-contained file, no dependencies, no network. Open it in a browser.

Also published, same content:
https://claude.ai/code/artifact/a3bf03ae-82b8-424c-8152-db8ad1a6cee3

Mock data stands in for `/json/wiki/get?uuid=...`. **The "Resolve over: All
versions / Leaf only" toggle demonstrates the SS 3 loss directly** -- switch it
and the LibreSSL review snaps from the 15th back to the 8th, because `9d1b5ec2`
clobbered `2f8a6d94`. That is the claim SS 3 rests on, reproducible in one click
rather than taken on faith. The "Display in" selector does the same for T1-T4:
of the four time forms, three move and the floating one does not.

Excluded from the viki index (`.vikiignore`) -- it is a demonstration, and its
CSS would be corpus noise. Not viki code, and not a dependency.
