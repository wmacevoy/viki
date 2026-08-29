# viki-core — the rewrite

> *"i meant rewrite = fix it - viki-core is the sqlite contracts as a nice api
> library. no more fossil compatibility more than ancestory. retain/recall,
> oopc patterns. no filesystem - that's someone else's problem."*
> — Warren, 2026-08-29

**Status: DESIGN.** Nothing built. This replaces the patch-fossil track, which
was five days of work to make someone else's process globals behave.

---

## 1. What the constraints delete

Every constraint below removes a whole class of problem rather than solving it.
That is the argument for the rewrite over the patch track.

| constraint | what stops existing |
|---|---|
| **contract is SQLite, not Fossil** | `Global g`, the 66 cached `static Stmt`, `zSavedKey`, the repository-filename cache, `fossil_main()`, argv shims, output capture, the exit trap, `fossil_panic()`'s untrapped `abort()` |
| **no filesystem** | repo paths, checkout walking, `.viki/` placement, VFS negotiation, `apndvfs`, file locking, iOS `NSFileProtection`, App Group corruption, `mtime`-based staleness |
| **no network** | the sync protocol, HTTP with no timeout, `xfer.c`, capability bits, `uv-pull-only` scraping, TLS |
| **no subprocess** | all three fork sites, `VIKI_NO_FORK`, the framing protocol, `#viki-eof`, `split_preserve_empty`, NUL-truncation |
| **ancestry, not compatibility** | Fossil's schema as an API, its manifest card format, its escaping rules, its wire protocol, its 2.28 pin |

**The caller hands us an open `sqlite3*`.** That one decision carries the
filesystem, the encryption key, the VFS and the location out of scope
simultaneously — the host opened it, so the host already chose all four.

## 2. What ancestry means

We keep Fossil's *ideas*, none of its code:

- **Content addressing.** An assertion's identity IS the hash of its body.
- **Grow-only.** Rows are immutable and never updated in place.
- **Union is merge.** Because identity is a content hash, merging two stores is
  `INSERT OR IGNORE` — there is no conflict resolution to get wrong, which is
  the property `SYNC.md` already identified as the safe class.
- **Resolution at read time.** Superseded rows stay. Which one is current is
  computed per query, not stamped into storage.
- **Field-level merge** for the cases that need it, which is what
  `ticketchng` does and why `ARBITRATION.md` §2b wanted tickets.

## 3. What viki-core actually is

Two things Fossil does not have, plus the substrate to hold them:

1. **Hybrid retrieval** — BM25 + literal + vector, RRF-fused.
2. **Time as a queryable dimension** — intervals, recurrence, due dates.

Everything else in the library exists to store and merge assertions.

## 4. Shape

**oopc** for the one polymorphic family that keeps getting written by hand
(`viki_note`, `cal_event`, provenance are the same type three times):

    VikiAssert          id · ts · supersedes        the root
      key()             what it competes on
      rank()            which of two wins
      text()            what gets chunked and embedded
      canon()           the bytes the id is the hash OF

**retain/recall** so the context is not threaded and not global:

    viki_note("the gate latch sticks below freezing");

One argument. Any depth. Compare today's
`viki_cmd_capture(".", zText, zPlace, zType, zWho, zDue, zState, zChannel)` —
eight parameters, the first a directory string, which is why nothing but
`viki.c` calls it.

## 5. Size

Smaller than the patch track, and the effort moves onto code we own.

| | |
|---|---|
| schema + assertion store + `INSERT OR IGNORE` merge | ~1 day |
| `VikiAssert` vtable, one `resolve()` | ~1 day |
| retrieval: the three legs + RRF, ported from `viki_ask.c` | ~1 day, it already works |
| chunking + embedding glue, ported from `embed.c` | ~1 day, it already works |
| time/interval assertions, ported from `viki_cal.c` | ~1 day, the shredder already works |
| the retained context + the verb surface | ~1 day |

**~1 week**, the same order as v0-by-patching — but the risk profile is
different: no unknowns of the "does `_Thread_local` on 66 cached statements
misbehave" kind, and every remaining question is about our own code.

Most of the hard-won logic **ports rather than gets rewritten**: `viki_ask.c`'s
fusion, `embed.c`'s pooling and L2, `tokenizer.c`, `viki_cal.c`'s RFC 5545
shredder, the chunking parameters that were measured in. What is deleted is the
plumbing around them.

## 6. What the host owns

Named explicitly so the boundary does not erode the way `edge/` did:

- opening and keying the `sqlite3*` (SQLCipher, VFS, path, iOS protection class)
- moving bytes between peers (HTTP, iCloud, AirDrop, a USB stick)
- policy, judgment, and asking questions (`assistant/`, SCOPES L3)
- the CLI, the HTTP face, MCP, wasm — all bindings to this ABI, per `oopc`'s
  rule that the C ABI is the interface

## 7. The libfossil track is a DEAD END

Recorded with numbers so it is not re-proposed. Growing `libfossilsee` into
viki's substrate fails on two counts, and both were measured this week rather
than argued.

**Hundreds of patch points.** Making Fossil's C safe to embed means owning:

```
5,006   g.<field> references behind a process-global Global
  219   function-local mutable statics
   78   file-scope mutable statics
   66   cached prepared statements (static Stmt) across 26 files, each
        holding an sqlite3_stmt* bound to ONE connection
   96   raw printf() outside the fossil_print() funnel
    3   caches already known to bite -- zSavedKey (a second open silently
        reuses the first key: measured, a WRONG key succeeded),
        savedKeySize, dbRepositoryFilenameCache (returns the first repo
        ever opened)
    6   patches fossil-see already carries just to build at all
```

Three of those caches were found "one at a time, each by a different symptom"
(`embed/README.md`'s own words) and there was no reason to think the audit was
finished.

**And a bad API.** `libfossilsee` exposes five symbols — a read-only SQLite
handle. It is a *database* handle, not a *program* handle, so a caller can read
what Fossil stores and invoke nothing Fossil does. Using it correctly required
learning eight facts from Fossil's **source** (that `tagxref.rid` is the
artifact while `tagxref.value` is the page size, that `GROUP BY 1 + max(mtime)`
avoids a superseded page, that the body is a counted `W` card, …), which is the
definition of a failed abstraction. It also behaves differently by transport:
`pragma_table_info` succeeds through `fossil sql` and returns *not authorized*
in process, which silently indexed zero tickets on one path and not the other.

**What survives from that track**, because it was worth doing on its own:
the upstream `/json/finfo` deletion fix, the `text/calendar` finding, and the
swappable-context patch. They stay in fossil-see. What does not survive is the
plan to build on top of them.

## 8. Ingress is not viki's problem

Reading Fossil repositories — or Outlook, or a calendar feed — is **ingress**,
which is SCOPES L3 and already settled: *"injest is not viki's problem, it is
the job of the humans/agents using viki"* (Warren). A connector reads whatever
it likes and calls `viki_note()`; it may fork, link libfossil, or shell out,
because it is not the thing running on a phone.

viki-core never learns that Fossil exists. It inherits the patterns and
nothing else.

---

## Built (2026-08-29)

```
core/include/viki_core.h   the whole public surface
core/src/viki_core.c       assertions, merge, projection, retrieval
core/src/viki_cal.c        calendar: NO parser + ONE VikiAssert subtype
core/src/sha256.c          ported unchanged
core/build.sh              no downloads, no submodules, no fossil
core/test/core-probe.sh    7 constraint + 29 behaviour assertions
```

`sh core/test/core-probe.sh` → **84 passed, 0 failed** (11 constraint + 73 behaviour).

Inputs are the SQLite amalgamation this repo already caches and `retain.h`
from the sibling checkout. That short list *is* the design.

**The controls are the point.** E1 asserts that *without* vectors a query
sharing no word with its target MISSES — so E3 finding it at rank 1 is
evidence the vector leg works rather than evidence the keyword leg is good.
M2 asserts a second merge adds nothing. S3 asserts the superseded row is
retained, not deleted. A1/A6 assert `viki_note()` with nothing retained is an
error rather than a silent drop. C6 asserts the constraint greps find
`sqlite3_*`, so C1–C5 are not passing because the pattern is broken.

### What is real

- one argument: `viki_note("the gate latch sticks below freezing")`, working
  four frames down through a callback that was handed nothing
- content addressing: same bytes → same id → one row
- union is merge, and idempotent
- resolution at read time, losers retained, ONE statement for every type
- three-leg RRF retrieval, degraded mode reported rather than hidden
- nested stores, outer restored on scope exit

### Audited 2026-08-29, and it mattered

An 11-agent adversarial audit over five independent lenses (memory, SQL,
retrieval, API contract, gaps-vs-design), each dimension's findings then
refuted by a skeptic, produced **23 surviving findings against code the
original 26 assertions were already green against.** Every one is now fixed
and carries a standing assertion. The two that mattered most:

- **Degraded mode returned ZERO hits over any corpus indexed with an
  embedder.** All three legs filtered on the retained epoch, and degraded
  epoch is `''` — so the keyword and literal legs, which need no model at
  all, matched nothing. The required working path answered "nothing is
  known" about a store full of text. *Four of the five lenses found this
  independently.* The probe missed it because its R section reindexed
  degraded FIRST, so `''` chunks happened to exist before the E section
  added an embed epoch. **G1 indexes only at the embed epoch, and fails
  against the original code — verified by reverting.**
- **Identity covered content but not position.** `id = sha256(canon())` meant
  two assertions with the same text under different keys collided into one
  row, so the second silently vanished and grow-only quietly lost a write.
  Identity is now framed: `sha256(kind␟key␟ts␟supersedes␟canon)`.

Two more worth naming: core ran bare `BEGIN`/`COMMIT` on a connection it does
not own, so `viki_reindex()` **committed the caller's open transaction** — now
`SAVEPOINT`, which nests correctly whether or not the host is mid-transaction;
and `viki_sql()` dropped every statement after the first and never read its
step result, so a failed write on the raw rung returned `VIKI_OK`.

### Calendar: jsCalendar in, and NO parser at all

The first port carried ~180 lines of hand-written iCalendar lexing — unfolding,
the params/value split with quote tracking, TEXT unescaping, deriving the four
time forms from syntax. **All of it is gone.** Input is **jsCalendar (RFC
8984)** and every field is `json_extract()`; SQLite is the parser, which is
the contract.

Both JSON calendar formats parse in pure SQL — measured — but jCal (RFC 7265)
is the mechanical transform of iCalendar into positional arrays
(`["dtstart",{"tzid":…},"date-time","2026-09-01T15:00:00"]`), so every access
is `$[3]` and the shape is iCalendar's problems in JSON clothing. jsCalendar is
object-based, and three of its decisions are exactly viki's already:

| jsCalendar | why it matters here |
|---|---|
| `start` is **local as written**, `timeZone` is an **IANA name** | "never store an offset" stops being a rule this code enforces and becomes the format. K4 is its control. |
| `recurrenceOverrides` is an **object keyed by recurrence-id** | `json_each()` yields exactly the `(UID, RECURRENCE-ID)` identity RFC 5546 resolution needs — an override becomes its own assertion on its own key, with no special case in core |
| `sequence` and `updated` are **named fields** | RFC 5546 precedence is two `json_extract()` calls |

And it is what real sources already emit: JMAP is jsCalendar natively, Google
Calendar and Microsoft Graph return JSON, EventKit hands you objects.
**iCalendar text is the narrowest ingress path** — `.ics` files — and
converting it is a connector's job (SCOPES L3), not core's.

`viki_cal_ingest()` takes a single object, a bare array, or a JMAP-shaped
envelope; `json_each()` flattens all three, so a caller need not know which its
source produced (K9, K10). Invalid JSON is refused rather than reported as an
empty calendar, and an object with no `uid` has no identity and is skipped
(K1, K1b).

**C7 is the constraint this bought**: no hand-written lexer may reappear in
core — no `strtok` over input, no quote-state machine, no fold rule — with C7b
as its control, because a grep for absence passes on an empty file.

### Chunks are RANGES, and two chunkings coexist — V2_DESIGN §3, built

The predecessor keyed chunks on `(content_hash, model_id, chunk_ix)` and stored
the text per chunk. `chunk_ix` is an **ordinal** whose meaning depends on the
parameters that produced it, so two peers with different chunk sizes wrote rows
that **agreed on the key and disagreed on the text**, and `INSERT OR IGNORE`
silently double-indexed a document. The fix there was to fold chunking into the
model id.

Keyed on `(id, lo, hi, model)` **that collision is unrepresentable** — the
extent is *in* the key rather than implied by it. Four things follow, and each
has an assertion:

| | |
|---|---|
| `model` means the **model** again (D-11 as originally written); `chunking` is provenance, not identity | V5 |
| a **second chunking adds** ranges over the same assertion without disturbing the first | V2, V3 |
| **overlap is free** — overlapping ranges are just ranges; the predecessor duplicated the overlapped text into two rows | V6 |
| chunk rows are immutable on a content key, so they are **grow-only and union-mergeable**, the same class as assertions | — |

**The text exists exactly once.** `viki_chunk` has no text column at all (V6);
a chunk's text is `substr(atext, lo+1, hi-lo)` through a view, and FTS5 accepts
that **view** as its external content — verified, including
`INSERT INTO viki_fts(viki_fts) VALUES('rebuild')` regenerating the whole index
from ranges alone (V7).

**And the reason it matters most**: a device can store an assertion with **no
chunking decision at all**, and a device with a model can add ranges over it
later. Compute-once-share-many extends from embedding to chunking.

The search win is measured, not asserted. Two topics six lines apart:

```
V3b CONTROL  only the FINE (2-line) chunking  -> no range holds both  ✓ misses
V4           the COARSE (8-line) added        -> one range holds both ✓ found
```

The vector leg's budget is **per chunking** (`row_number() OVER (PARTITION BY
chunking)`), because without that partition the finest policy takes every slot
simply by having the most rows.

### Observability, and "no poking at the tables"

`viki_watch()` gives semantic events, not row events — *"an assertion
arrived"*, not *"a row was inserted into viki_assert"*. The schema is core's
business and has already changed once (ranges replaced ordinals), so a
listener is told things in the vocabulary of the API.

**Events are write-only by construction.** `PUT`, `SUPERSEDED`, `FORGOTTEN`,
`MERGED`, `PROJECTED`. A read can be an arbitrary join and has no change to
announce; O5 asserts that reads fire nothing.

**They are buffered and flushed on COMMIT, discarded on rollback.** Core runs
inside `SAVEPOINT`s on a connection it does not own, so the host may roll back
after us — and a listener that wrote to a UI or told a peer cannot take that
back. O6 asserts a host `ROLLBACK` is never announced. A listener that tries
to *write* gets `VIKI_EBUSY` rather than reentering the queue it is being
dispatched from (O9).

**And the requirement that all interaction goes through core is enforced, not
stated.** `viki_count()` and `viki_each()` exist so a caller never names a
table; `viki_cal_events()` covers the calendar. **C8 greps the probe** for
statements prepared on the connection itself — the probe is the only host core
has, so it is where the rule is testable. `viki_sql()` is allowed, because it
*is* the API (SCOPES §1b: a curated verb must never be the only door).

That conversion immediately paid for itself twice: it broke A5, which had been
counting the wrong store (`viki_count()` answers about the *retained* one), and
it exposed a real inconsistency — `VIKI_N_CURRENT` counted "nothing explicitly
supersedes it" while `viki_current()` means "wins its key by rank". **Those are
not the same**: an update that wins by a higher RFC 5546 sequence sets no
supersedes link, so the older assertion is unsuperseded and no longer current.
Two notions under one name is exactly the drift a single resolver was meant to
prevent.

### Withdrawal, and a rule that turned out to be narrower than inherited

`viki_forget()` is the deliberate exception to grow-only — "I pasted a
credential into a note" is a real thing, and a memory with no way to unsay
something is one people stop telling things to. It is **local**: a peer that
has the assertion still has it. Anything else would be a tombstone protocol.
`viki_prune_epoch()` drops a dead model's projection and leaves truth alone.

The predecessor's rule is that FTS must be deleted **before** the chunk row.
Measured while porting it, that depends on the idiom:

| idiom | needs the content row? | order matters? |
|---|---|---|
| `DELETE FROM f WHERE rowid=?` | yes — it re-reads it | **yes** |
| `INSERT INTO f(f,rowid,text) VALUES('delete',…)` | no — text is explicit | no |

core uses the second. **The more useful finding is about the assertions**: two
obvious ways to test withdrawal are green straight through the bug. An
`ask`-based check cannot see it, because retrieval JOINs the chunk table and an
orphaned index entry never becomes a hit; and FTS5's own `integrity-check`
*passes* over an index whose content row vanished. W4b asserts the property
directly against `viki_fts`, which catches it and is idiom-independent —
verified by disabling the FTS delete entirely.

### Not yet

A real embedder binding, and the CLI/HTTP/MCP faces, which are bindings to this
ABI rather than new implementations.
