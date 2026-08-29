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

`sh core/test/core-probe.sh` → **120 passed, 0 failed** (11 constraint + 109 behaviour).

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

---

## The CLI, and `viki run`

`cli/viki_cli.c` — a **binding**, not an implementation. Every verb is a thin
call into core, and **the CLI is the host**: it resolves the path and opens the
connection, which core never learns about. That is the boundary demonstrated
rather than asserted.

    viki note TEXT · ask QUERY · forget ID · merge PATH · reindex
    viki cal ingest FILE · cal events [FROM TO] · count WHAT · list · sql
    viki run CMD [ARGS...]

`sh cli/cli-probe.sh <dir>` → **44 passed, 0 failed**, on an encrypted diary.

### Opening a keyed diary

The key is entirely the host's business — core never sees one. Sources, worst
to best:

| | |
|---|---|
| `--key VALUE` | **refused.** argv is world-readable in `ps` on every system this runs on, and shell history keeps it. A flag that exists *will* end up in a cron line, so it does not exist (K1). |
| `$VIKI_KEY` | accepted **with a warning** — readable from `/proc` on Linux and inherited by every child. But it is what CI needs, and refusing it only pushes people to a key file with worse permissions. |
| `--keyfile PATH` | the default answer. Permissions are checked the way `ssh` checks a private key; 0644 is refused (K2). |
| a prompt | when stdin is a terminal and nothing else was given. Echo off. |
| the platform | Keychain, Secure Enclave, TPM. Not wired — this is the seam, and it needs no core change. |

**Raw key vs passphrase is detected, not configured.** 64 hex characters is
used as a raw `x'…'` key; anything else is a passphrase. Measured on this
project: 5.99 ms versus 345.93 ms, because SQLCipher runs PBKDF2 for one and
not the other. Someone who generated a raw key should not have to say so, and
should not silently pay 58× for it.

Two details that are easy to get wrong and are asserted: the key is applied
**before anything else touches the connection** (SQLCipher decrypts on the
first read, so a late `PRAGMA key` is too late), and a **wrong key is detected
by the first real read** rather than by `PRAGMA key`, which succeeds
regardless because it only sets the cipher context. Reporting that plainly
beats letting schema creation fail with something that reads like corruption.

### What `viki run` is actually for — third answer, and this one is not speed

I have now argued the speed case twice and been wrong twice, in opposite
directions. The measurements, all real:

```
plaintext store          0.16 s -> 0.13 s over 60 calls   (~0.5 ms/call)
raw x'...' key           5.99 ms per open
passphrase               345.93 ms per open   (12 calls: 1.35 s -> 0.16 s)
```

**A raw key is the normal case** (Warren, 2026-08-29): *"the kdf cost is not
there usually — raw key — but the secret retention and api guarantee is."* So
the 8.4× is real and atypical, and citing it as the justification is the same
mistake as citing "fast fork" was. The two reasons that hold in **all three**
rows:

**Secret retention.** With per-invocation keying, the key file is read N times,
the key material exists in N processes, and each of those is a place it can be
read out of. With `viki run` it is read **once**, lives in **one** process, and
a child has no path to it at all — K7 asserts the child sees no key and still
writes. That reduction does not depend on the key being expensive.

**API guarantee.** One context means one connection, one transaction domain,
and **one set of retained things**. Without it every invocation starts with
nothing retained: no embedder, no identity, no listeners — so a script cannot
have hybrid retrieval, or signed writes, or observability, at all. With it the
parent retains them once and every child inherits them through the socket.
That is not an optimisation; it is the difference between a script being able
to use those features and not.

**Wired.** The `viki run` parent now retains a `VikiIdentity` and a
`VikiEmbed` around its serve loop, so a child's writes are **signed by the
parent's identity while the child holds no key**, and retrieval is hybrid
while the child holds no model:

```
S5   a child's write is SIGNED by the parent's identity
S5b  ...and keeps working regardless of what the child does with the file
S6   a peer that merged the diary verifies WHO, holding no key
```

That is the API guarantee in its concrete form: without a held context every
invocation starts with nothing retained, so a script cannot have signed writes
**at all** — not slower, not at all.

### `viki run` — and the obvious justification is wrong

The proposal was "fast fork": build a context, fork, parent serves, child gets
`$VIKI_CONTEXT`. The shape is right. **The speed argument is not**, and it is
worth writing down because it changes what the feature is for.

Measured on this machine:

```
sqlite3_open + WAL     0.267 ms      bare fork+exec   1.282 ms
viki_attach (schema)   0.023 ms
retain + one ask       0.638 ms
  total cold setup     0.928 ms
```

**Cold viki setup is cheaper than the process spawn.** End to end, 60 CLI
invocations cost 0.16 s directly and 0.13 s through a warm context — about
0.5 ms saved per call. For a plaintext store with no model, `viki run` is not
worth having.

What it actually holds is the two costs that are *not* in that table:

| | per invocation |
|---|---|
| SQLCipher with a **passphrase** | **345.93 ms** (PBKDF2; FINDINGS.md) |
| SQLCipher with a raw `x'…'` key | 5.99 ms |
| an ONNX model load | 100–500 ms typical |

So `viki run` is **"pay for the key and the model once, and never hand either
to a child"** — not fast fork. The fork is incidental; the custody is the
point.

### Which is exactly why the transport is a unix socket

After `viki run`, the parent holds a **decrypted** store and a loaded model.
Anything that can reach the socket can read the whole memory *without the
passphrase* — so the transport is the boundary, not a convenience.

A loopback port (v6 or v4) is reachable by **every process of every user** on
the machine, so it would need a token in the URL — and a token in an
environment variable is readable from `/proc` on Linux and leaks into any
child's `env` output. A unix socket in a **0700 directory** is guarded by the
filesystem: no token, no port, absent from `netstat`, and it never touches the
network stack. Stricter *and* faster. S1–S3 assert it, including a control that
the context is a path and never a `host:port`.

### Three things the implementation had to get right

- **The child's exit status is the wrapper's** (R1). A wrapper that swallowed
  it would be useless in a script, which is the only place this is worth having.
- **The store is opened before the fork**, so the child never opens it and
  never needs the key. SQLite forbids using one connection across a fork, and
  nothing does: the child speaks the socket and touches no `sqlite3*`.
- **The parent drains after the child exits** (R3). A script whose last line is
  `viki note …` has that write in flight at exit; a server that stopped
  accepting on `SIGCHLD` would lose it.

And one bug worth keeping: `signal()` installs a BSD handler with `SA_RESTART`,
so `SIGCHLD` does **not** interrupt `accept()` — it resumes, and the loop
condition is never re-tested. The parent served every request correctly and
then hung forever. `select()` with a short timeout polls the flag instead of
fighting the signal semantics, and gives the drain for free.

### Degradation

A stale `$VIKI_CONTEXT` — a killed parent, a copied environment — **degrades to
opening the store directly** (F1), the same way a missing embedder degrades
retrieval instead of refusing it. Failing there would make an exported variable
a permanent trap.

---

## The model belongs in its own database, not in each store

> *"put onnx in a viki-core viki database. can this be used directly as an
> uncompressed blob? this saves duplication between viki databases, since now
> opening more than one is not a problem."* — Warren, 2026-08-29

**Yes to all three, and the third is the strongest.** Measured on a 90 MB blob:

```
db holding a 90 MB blob        90.2 MB on disk   (uncompressed, stored as-is)
sqlite3_column_blob (no mmap)     20.0 ms
sqlite3_column_blob (mmap on)     10.3 ms
mmap of a plain file               1.2 ms
```

SQLite stores blobs **verbatim** — 0.2% overhead — and `sqlite3_column_blob()`
hands back a contiguous pointer, which is exactly what
`OrtCreateSessionFromArray(env, bytes, len, …)` wants. So "used directly" is
true in the sense that matters: no decompression, no temp file, no copy of your
own.

It is **not** zero-copy. A blob larger than a page lives on overflow pages, so
SQLite assembles it — one full-size copy, 10–20 ms, versus ~1 ms of lazy paging
for a mmap'd file. Against ORT's own 100–500 ms parse that is 2–10%, and the
buffer can usually be released once the session exists.

### The duplication argument is the real win, and it is bigger than disk

D-11 pins **one model, universal across peers** — so a copy inside every
tribe's store is N copies of a byte-identical artifact. Keeping it in its own
database and opening that alongside is the shape that follows, and **it needs
no change to core**: `VikiEmbed` and `VikiStore` are separate retain stacks with
independent lifetimes, so the model is simply retained *outside* the stores.

```c
RETAIN_BEGIN(VikiEmbed, &emb, ge);          /* ONE model, one ORT session */
    RETAIN_BEGIN(VikiStore, &tribeA, g1);  …  RETAIN_END(g1);
    RETAIN_BEGIN(VikiStore, &tribeB, g2);  …  RETAIN_END(g2);
RETAIN_END(ge);
```

That saves the copy **on disk, in RAM, and in ORT session count** — one session
serves every tribe the process touches. N1 asserts it; N1b is the control that
the same stores degrade once the embedder leaves scope, so N1 is about the
retained embedder and not something ambient.

And "opening more than one is not a problem" is exactly right, and worth naming
as a contrast: that was the *entire* difficulty in the fossil-embedding track —
`Global g`, 66 cached prepared statements, one repository per process. Here a
store is a `sqlite3*` the host opened, so there is nothing to be a problem.

### What this does not solve

The tribe's store stops being self-contained: a store copied to another machine
needs the model database too. That is already the shape D-12 chose — the model
distributes separately, checksummed against a pinned manifest — so this changes
the container, not the contract.


### Two different lifetimes, and one correction

There are **two** things here and they were briefly conflated, so it is worth
separating them permanently.

| | durable | session |
|---|---|---|
| what | a signed **assertion** in the diary | a **token** issued by a running core |
| lifetime | forever; read by peers years later | at most as long as the core that issued it |
| "now" | there isn't one — verification happens whenever | the API's own clock |
| trust from | the signer's recorded public key | the deployment: a service enforcing it, or a trusted executable on a phone |

An earlier draft argued that expiry does little work because `ts` is
self-reported and verification happens later. **That is true of durable
assertions and wrong about tokens** (Warren, 2026-08-29): *"the now is the
api."* A token bounded by the issuing process's lifetime has a trustworthy
clock and a verifier that is the issuer — expiry there means exactly what JWT
means by it, and a REST face needs the machinery regardless. The key map is a
local table under a random key, so it dies with the process by construction.

So: **expiry is meaningless for stored assertions and correct for session
tokens.** Do not import either conclusion into the other half.

**Not built, deliberately** (Warren: *"don't do this yet"*): token issuance,
the key map, and cross-instance token migration.

---

## Identity: what signing adds, and what it must not decide

> *"what gives signed changes, a tribe is identity. maybe this is pluggable —
> what lets this be a thumbprint on a laptop open an identity. what lets an
> agent trace have an identity (something in your own diary) and a human use a
> fingerprint?"* — Warren, 2026-08-29

**Content addressing already gives integrity.** An assertion's id is the hash
of what it says, so tampering produces a *different* assertion rather than a
corrupted one. What it does not give is **authority**: anyone holding the diary
can write anything into it, and the store cannot say who did. Signatures supply
exactly that missing half — the predecessor reached the same conclusion
(`keywrap-probe`'s S-series) and its lessons are the assertions here.

### Pluggable falls out of the existing shape

Core links no crypto, so signing and verifying are **host callbacks**, retained
like the embedder. That is what lets the mechanism differ per platform without
core knowing:

| | how it signs |
|---|---|
| **a human on a laptop** | the private key lives in the Secure Enclave / TPM and a **thumbprint authorises one signature**. `xSign` calls the platform; the key never enters this process, let alone this library. |
| **an agent** | its identity is an assertion **in your own diary** — a name and a public key you wrote down — and it signs with the key you issued it. Its authority traces to an entry you can read. |
| **a headless peer** | a file, an env var, an HSM. Core cannot tell. |

`RETAIN_DECLARE(VikiIdentity)` — so an identity is retained for a scope exactly
as a store or an embedder is, and **unsigned is a working path** (I2), not a
failure. A *declined* signature — a cancelled thumbprint, a locked keychain —
stores the assertion unsigned rather than losing it (I5, I5b). Losing a write
because a prompt was dismissed would be the worst possible trade.

### Signatures are rows, not a column

Several identities may sign one assertion, so countersigning is union-merge
like everything else (I6) — and a signed copy can never be shadowed by an
unsigned one, which it would be if the signature rode on the assertion and
`INSERT OR IGNORE` kept whichever arrived first.

`viki_merge` carries signatures too. Without that a peer would receive a
statement while losing the evidence of who stood behind it, which is the half
signing exists to supply.

### Four facts, and no judgment

`viki_signed()` returns `NONE`, `OK`, `BAD` or `UNKNOWN`. All four are facts.
**Whether a verified signer is a *trusted* one is a judgment, and judgments
live in the caller** — the same line `coverage` draws by reporting last-seen
times and no thresholds.

`UNKNOWN` is the one worth having: a signature from an identity this diary does
not hold is not a failure, it is a fact about *coverage*, and it is precisely
what a peer sees before the signer's identity assertion reaches it (I9).

### The two assertions that carry the weight

- **I4** — a signature made with another key must **not** verify. Without it a
  signature proves nothing about *who*, which is the only thing it was added
  for. This is the predecessor's S5, and it is the one that matters.
- **I8** — a fresh peer merges the diary and establishes who said what while
  **holding no private material at all**: it retains a verifier with no
  `xSign`, no signer id, and no key. That is the predecessor's S3, and it is
  what makes signatures useful to someone who was not there.

## Blobs: the vector is of a DESCRIPTION, not of the payload

> *"blobs can have custom chunking — this one is a vector of a description
> that the blob of onnx coefficients."* — Warren, 2026-08-29

`VikiAssert` has always had two slots that a note makes look redundant:

    canon()   the bytes identity is computed over
    text()    what gets chunked and embedded

A blob is what makes the difference load-bearing. Chunking 23 MB of int8 ONNX
coefficients would produce ranges of noise and a vector that means nothing —
the bytes have no semantic content. What is worth embedding is a
**description**, so the blob's single range covers the description and its
vector is the description's.

That is the general shape, not a special case for models: a PDF's text is its
extracted text, an image's is its caption or OCR, a recording's is its
transcript. **The payload is addressed; the description is searched.**

Measured on the real pinned model:

```
model on disk    23,026,053 bytes
stored                    52 ms
ranges over it             1        <- over the DESCRIPTION
ask "which embedding model do I have"
                 -> all-MiniLM-L6-v2 sentence embedding model, ONNX, int8...
read back        23,026,053 bytes in 14.8 ms, byte-identical
diary on disk    23,097,344 bytes   (100.3% of the model)
```

Three decisions inside it, each with an assertion:

- **Identity is the caller's content hash**, not a rehash. The host already has
  it — D-12 pins the model's checksum — and rehashing 23 MB inside a put pays
  twice for a number that must match the pin anyway. Identity is
  `(content hash, description)`, so the same bytes described differently are
  **two claims about one payload**, which is right: the description is the
  claim (B6).
- **The payload lives outside `viki_assert`** (B5), so a 23 MB model does not
  sit in the row that every resolve, count and merge scans.
- **`viki_blob_get()` returns a borrowed pointer** valid until the next core
  call, because `sqlite3_column_blob()` owns that memory until its statement is
  stepped — so exactly one statement is held alive. That is stated in the
  header rather than left for a caller to discover.

B4b is the control worth naming: the indexed range must hold the description
and **not** the coefficients. Without it, B4 would pass just as happily over a
range full of binary that happened to contain the query's letters.

### And this is how images arrive

An image is the same thing: the payload is the PNG, the text is a caption or
OCR. Nothing about it is a special case.

**The consequence worth being explicit about** is why the description goes
through the *ordinary* chunk path rather than a side table: it lands in
`viki_chunk_text`, so `viki_fts` indexes it, so **the primitive legs find it**.
A photograph is findable by a keyword query **with no embedder loaded at all**
— which is the state a phone, a fresh clone, or any degraded run is actually
in.

```
B7   an image blob is found by the KEYWORD leg, no embedder
B7b  CONTROL: ...and that search really was degraded
B7c  CONTROL: the PAYLOAD is not searchable, only the description
```

B7b matters because without it B7 would pass just as well with an embedder
quietly retained, and would then be a claim about vectors rather than about
keywords. B7c is the other edge: binary must never reach the index.

---

## Three answers: the word, the size, and scratch

### The word: **diary**, and it earns it

`SOFTWARE-ENGINEERING-2` §4 says conceptual integrity shows up first in
vocabulary, so this is worth settling rather than drifting. **A viki database is
a diary**, and the argument is that every property of the noun is already a
property of the thing:

| a diary | a viki store |
|---|---|
| append-only — you do not un-write yesterday | grow-only; superseded assertions stay |
| dated | every assertion carries `ts`, and `rank` is usually lexical time |
| one writer | one peer writes it; **merging diaries** is what a tribe is |
| kept, not filed | queried, not read start to finish |

It also fixes `viki_merge` in the mind: you are merging diaries, and union is
merge because two people writing the same sentence wrote one fact.

**Do not use "journal".** In a library whose contract is SQLite, "journal"
already means the rollback journal, and `PRAGMA journal_mode=WAL` is in the
CLI's own open path. A word that means two things in one file is the drift
§4 warns about.

`SCOPES` §4 already defines **a tribe** as one L0 instance; that stays. A tribe
is the set of diaries that merge. A diary is one peer's file.

### The size: **store it uncompressed** — DECIDED

The 90 MB figure came from a blob of *random bytes* I generated to test
throughput. The real pinned model is **23,026,053 bytes** —
`all-MiniLM-L6-v2`, `model_qint8_arm64.onnx` — which is 23% of a <100 MB
payload budget, not 90%.

```
raw                   23,026,053 bytes
gzip -6               17,464,371          75.8%
zstd -19              16,919,669          73.5%
gunzip to memory           79.3 ms
```

**Decision (Warren, 2026-08-29): store it uncompressed.** ~25% is not worth
the trouble for a core feature at this size, and the numbers say why rather
than the other way round: 6 MB saved, 79 ms paid on every load, plus a
decompressor, plus a materialised-uncompressed cache to manage, plus the
question of where that cache lives.

The ranking is the useful part to keep: **quantisation already did the heavy
lifting.** A float32 MiniLM-L6 is ~90 MB; int8 took it to 23 — a 4× cut,
against gzip's 1.3×. If the payload budget ever binds, the next lever is the
model, not the container. Do not revisit compression before that.

### Scratch: deferred, not designed

A scratch diary would need no new machinery — a store you either `viki merge`
or delete — and that was verified before deciding not to build it. **Deferred
(Warren, 2026-08-29): "let's worry about scratch if it comes up for a reason."**

The merge properties it exercised are kept in `cli/cli-probe.sh` under their own
name, because they are true of `merge` whether or not anyone ever calls a store
"scratch": promotion copies rather than links, so deleting the source afterwards
does not un-promote, and a store that was never merged leaves no trace.
, not of the payload

> *"blobs can have custom chunking — this one is a vector of a description
> that the blob of onnx coefficients."* — Warren, 2026-08-29

`VikiAssert` has always had two slots that a note makes look redundant:

    canon()   the bytes identity is computed over
    text()    what gets chunked and embedded

A blob is what makes the difference load-bearing. Chunking 23 MB of int8 ONNX
coefficients would produce ranges of noise and a vector that means nothing —
the bytes have no semantic content. What is worth embedding is a
**description**, so the blob's single range covers the description and its
vector is the description's.

That is the general shape, not a special case for models: a PDF's text is its
extracted text, an image's is its caption or OCR, a recording's is its
transcript. **The payload is addressed; the description is searched.**

Measured on the real pinned model:

```
model on disk    23,026,053 bytes
stored                    52 ms
ranges over it             1        <- over the DESCRIPTION
ask "which embedding model do I have"
                 -> all-MiniLM-L6-v2 sentence embedding model, ONNX, int8...
read back        23,026,053 bytes in 14.8 ms, byte-identical
diary on disk    23,097,344 bytes   (100.3% of the model)
```

Three decisions inside it, each with an assertion:

- **Identity is the caller's content hash**, not a rehash. The host already has
  it — D-12 pins the model's checksum — and rehashing 23 MB inside a put pays
  twice for a number that must match the pin anyway. Identity is
  `(content hash, description)`, so the same bytes described differently are
  **two claims about one payload**, which is right: the description is the
  claim (B6).
- **The payload lives outside `viki_assert`** (B5), so a 23 MB model does not
  sit in the row that every resolve, count and merge scans.
- **`viki_blob_get()` returns a borrowed pointer** valid until the next core
  call, because `sqlite3_column_blob()` owns that memory until its statement is
  stepped — so exactly one statement is held alive. That is stated in the
  header rather than left for a caller to discover.

B4b is the control worth naming: the indexed range must hold the description
and **not** the coefficients. Without it, B4 would pass just as happily over a
range full of binary that happened to contain the query's letters.

### And this is how images arrive

An image is the same thing: the payload is the PNG, the text is a caption or
OCR. Nothing about it is a special case.

**The consequence worth being explicit about** is why the description goes
through the *ordinary* chunk path rather than a side table: it lands in
`viki_chunk_text`, so `viki_fts` indexes it, so **the primitive legs find it**.
A photograph is findable by a keyword query **with no embedder loaded at all**
— which is the state a phone, a fresh clone, or any degraded run is actually
in.

```
B7   an image blob is found by the KEYWORD leg, no embedder
B7b  CONTROL: ...and that search really was degraded
B7c  CONTROL: the PAYLOAD is not searchable, only the description
```

B7b matters because without it B7 would pass just as well with an embedder
quietly retained, and would then be a claim about vectors rather than about
keywords. B7c is the other edge: binary must never reach the index.

---

## Three answers: the word, the size, and scratch

### The word: **diary**, and it earns it

`SOFTWARE-ENGINEERING-2` §4 says conceptual integrity shows up first in
vocabulary, so this is worth settling rather than drifting. **A viki database is
a diary**, and the argument is that every property of the noun is already a
property of the thing:

| a diary | a viki store |
|---|---|
| append-only — you do not un-write yesterday | grow-only; superseded assertions stay |
| dated | every assertion carries `ts`, and `rank` is usually lexical time |
| one writer | one peer writes it; **merging diaries** is what a tribe is |
| kept, not filed | queried, not read start to finish |

It also fixes `viki_merge` in the mind: you are merging diaries, and union is
merge because two people writing the same sentence wrote one fact.

**Do not use "journal".** In a library whose contract is SQLite, "journal"
already means the rollback journal, and `PRAGMA journal_mode=WAL` is in the
CLI's own open path. A word that means two things in one file is the drift
§4 warns about.

`SCOPES` §4 already defines **a tribe** as one L0 instance; that stays. A tribe
is the set of diaries that merge. A diary is one peer's file.

### The size: **store it uncompressed** — DECIDED

The 90 MB figure came from a blob of *random bytes* I generated to test
throughput. The real pinned model is **23,026,053 bytes** —
`all-MiniLM-L6-v2`, `model_qint8_arm64.onnx` — which is 23% of a <100 MB
payload budget, not 90%.

```
raw                   23,026,053 bytes
gzip -6               17,464,371          75.8%
zstd -19              16,919,669          73.5%
gunzip to memory           79.3 ms
```

**Decision (Warren, 2026-08-29): store it uncompressed.** ~25% is not worth
the trouble for a core feature at this size, and the numbers say why rather
than the other way round: 6 MB saved, 79 ms paid on every load, plus a
decompressor, plus a materialised-uncompressed cache to manage, plus the
question of where that cache lives.

The ranking is the useful part to keep: **quantisation already did the heavy
lifting.** A float32 MiniLM-L6 is ~90 MB; int8 took it to 23 — a 4× cut,
against gzip's 1.3×. If the payload budget ever binds, the next lever is the
model, not the container. Do not revisit compression before that.

### Scratch: deferred, not designed

A scratch diary would need no new machinery — a store you either `viki merge`
or delete — and that was verified before deciding not to build it. **Deferred
(Warren, 2026-08-29): "let's worry about scratch if it comes up for a reason."**

The merge properties it exercised are kept in `cli/cli-probe.sh` under their own
name, because they are true of `merge` whether or not anyone ever calls a store
"scratch": promotion copies rather than links, so deleting the source afterwards
does not un-promote, and a store that was never merged leaves no trace.

## The ledger (2026-08-29)

`viki_task.c` adds the one thing a personal memory cannot do without: **a note
that someone owes, to someone, by a date.**

It is a SUBTYPE, not new columns. `viki_assert` has eight columns and none of
them is `due` -- that is what lets notes, calendar events and provenance share
one type, and adding task columns is how that stops being true. So a task is
`kind='task'` with its fields in `body` as JSON, reached with `json_extract()`
exactly as jsCalendar is. SQLite is the parser here too (core-probe C7), and
the body is built by `json_object()` rather than by any escaper of ours.

**Structuring is a new assertion, not an edit.** The predecessor rewrote
`captures/*.md` in place and once deleted user text doing it. Here the raw
capture stays and the task is a second assertion whose `supersedes` names it.
The act of structuring is itself in the record, and union is still merge.

**Which makes arrival free.** `viki note "..." --supersedes <task-id>` retires
a task. That is what the predecessor spelled `--closes`, except nothing is
deleted for it to happen -- the ledger reads only what nothing supersedes
(T5, T6). An arrival is filed as a NOTE rather than a task on purpose: as a
task it would retire one row and add another, so the list would never shrink
(T6b).

**A malformed due date is refused at write time** (T3). The predecessor
accepted `@due next Tuesday`, sorted it lexically below every digit, and
printed a phantom OVERDUE in the morning brief every day with no way to see
why. A ledger wrong in the direction of anxiety is worse than one that
rejects a line.

**Ordered by due, not by write time** (T4), and **unowned counts as mine**
(T4b) -- a commitment nobody claimed is one the reader is still carrying.

**The host owns the clock.** core calls `time()` zero times; it has no
filesystem, no network and no clock, so everything it stores is something it
was told. `isoNow()` lives in the CLI. That is what makes a store reproducible
from its inputs.

### Direction, settled 2026-08-29

> "core is the derivable set --- fossil-see is a dead end." -- Warren

`core/` is the successor to `src/`, not a second thing to maintain beside it.
The Fossil-backed implementation is no longer the target; what `src/` still
has and `core/` does not is now a MIGRATION LIST, not a parallel product. The
ledger was the first item on it and is done.
