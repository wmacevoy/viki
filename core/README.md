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
