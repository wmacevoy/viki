# API.md — the correction, and the shape that follows from it

**Status: DESIGN, for Warren's decision.** No code yet. Written in answer to:

> *"make an oopc/retain style api for viki, libfossilsee is not a boundary
> because we are glueing in vector search and time (calendar, etc) into fossil.
> but there are repeated abstractions. weeks of building what was already there
> or tiny patches is something to correct for. what is the correction?"*
> — Warren, 2026-08-29

---

## 1. The correction, in one sentence

**viki treats Fossil as a fixed external, and Fossil is ours.**

Every expensive mistake this project has made reduces to that. Not carelessness
— each decision was locally reasonable, and the code that came out of it was
good code. The error is upstream of the code: a limitation gets observed once,
written into a comment as a fact, and then *never re-tested*, while the thing it
describes is a repository we own and can change.

Six instances, all verifiable in this tree:

| What was built | What it was working around | What the fix actually was |
|---|---|---|
| `index_unversioned()` forked `unversioned cat` **per blob** (~1 year) | "the only extraction path that does not require linking zlib" | `decompress()` was already registered by the same call that registers `content()` |
| `viki_prov.c`, 1,066 lines | "`finfo` does not report deletions" | a `LEFT JOIN` — two lines |
| the counted framing protocol | `fossil sql` exits 0 on a failed prepare | in-process access, which already existed |
| `viki_note.c`'s supersession, 1,318 lines | — | Fossil tickets merge field-by-field; `ARBITRATION.md` §2b already says so |
| `cal_event`'s assertion tier | — | CLAUDE.md's own words: *"the same shape as viki_note"* |
| the `text/calendar` mimetype patch | — | derived twice, independently, in two repos, neither knowing about the other |

Two of those cost roughly **500:1** against the patch that would have replaced
them. That ratio is the whole argument.

### 1a. The mechanism: a workaround is a claim, and claims get controls

This repository already knows that a positive claim needs a control that comes
out the other way — `test/m1.sh` labels eighteen of them. **It has never pointed
that discipline at its own workarounds.**

A workaround is a claim about the substrate: *"we do W because the substrate
cannot do X."* Written in a comment, that claim never expires. Written as a
test, it expires the day the substrate gains X.

> **Proposed: `build/substrate-probe.sh`.** Every assertion inverted — each one
> asserts the substrate **cannot** do something, and names the workaround that
> exists because of it. It goes RED when the substrate gains the capability,
> and the failure message says *"`index_unversioned()`'s per-blob fork is now
> dead code."*

The uv case is the proof this would have worked: the limitation was never real,
so that assertion would have been red the first day it ran.

### 1b. The mechanism: cost the patch before writing the projection

Not a preference — a required line in the commit message whenever code exists to
compensate for the substrate:

    substrate patch considered: ~N lines in fossil-see vs ~M lines here; chose X because Y.

`viki_prov.c` could not have survived writing that line honestly.

### 1c. And the deep one: stop reimplementing the substrate's data model

See §2. This is the repeated abstraction.

---

## 2. The repeated abstraction, named

`viki_note` and `cal_event` are the same structure with different field names:

| | `viki_note` | `cal_event` |
|---|---|---|
| identity | `note_id` | `(uid, recurrence_id)` |
| ordering / tie-break | `ts` | `max(sequence, dtstamp)` — RFC 5546 |
| supersession | `closes` → another note_id | a higher-ranked row on the same key |
| resolution | at READ time; superseded notes leave the ledger | at READ time; superseded rows **stay** |
| merge | union on a content key | union on a content key |

Both are: **a grow-only table of immutable assertions on an identity key, where
"which one is current" is computed at read time and losers are retained.**

`viki_prov.c` is a third instance of the same idea, and any future projection —
mail, contacts, tasks from another source — will be a fourth.

**And that structure is what a Fossil artifact IS.** Immutable, content-addressed,
grow-only, union-merged, with resolution computed at read time. viki has now
implemented Fossil's core data model three times, in SQLite, inside a system
built on Fossil. That is the "weeks of building what was already there," and it
is not a subprocess problem or an API problem — it is a *modelling* problem.

### 2a. So what is actually viki's?

Warren's correction — *"libfossilsee is not a boundary because we are glueing in
vector search and time into fossil"* — is what makes this answerable. viki is
not a consumer across a library boundary. It is Fossil **plus two things Fossil
does not have**:

1. **Hybrid retrieval over content** — BM25 + literal + vector, fused by RRF.
   Fossil has FTS; it has no vectors and no fusion.
2. **Time as a queryable dimension** — intervals, recurrence, due dates,
   coverage windows. Fossil has timestamps. It has no interval semantics.

**Those two are viki's, and nothing else is.** That is a sharper line than
"libfossilsee is the boundary," which was the wrong answer: it drew the line at
a *linkage* rather than at an *idea*, and a line drawn at a linkage is why the
data model got rebuilt on the far side of it.

The test to apply to any new viki code, replacing the placement test in SCOPES
§3 for this specific question:

> **Is this retrieval, or is this time? If neither, Fossil already does it —
> and if Fossil does it badly, patch Fossil.**

---

## 3. Polymorphism (what `oopc` is)

> *"polymorph is the well known pattern (oopc)"* — Warren

Naming it plainly matters, and not only for tidiness. Calling it "the
oopc-shaped API" made it sound like adopting a library; it is not. It is the
ordinary pattern, and `oopc` is one disciplined way to spell it in C: prefix
layout, `static const` vtables, **every slot takes the root type**,
`static_assert` pinning each layer. The question is not whether to use `oopc`.
It is **where viki has a polymorphic family it has written out by hand.**

There are two, and the second is more obvious than the first.

### 3a. The extractors — nine bodies, one algorithm

Every one of the nine `index_*` functions is the same procedure:

    build SQL -> fossil_sql_framed -> framed_init -> loop framed_next
              -> compose a virtual path -> index_text_blob -> return seenEof

Eight of the nine contain *exactly* four framing calls; only `index_tickets`
differs, and only because it introspects first. What varies is three things:

| slot | wiki | uv | checkins |
|---|---|---|---|
| `sql()` | tag/tagxref/blob | unversioned + `decompress()` | event + `content()` |
| `path()` | `wiki:<name>` | `uv:<name>` | `ckin:<uuid>` |
| `text()` | `find_w_card()` | split at first `\n` | the record as-is |

That is a template method with three overridden slots, written nine times.

**Every defect in this family is a defect OF the family**, and the tree shows
it. The NUL exclusion — *the subprocess transport cannot carry an embedded NUL,
so exclude such rows in SQL* — is now written **four** times, at
`viki_index.c:1077`, `:1472`, `:1888` and `:1985`. It reached `attach:` first
and had to be copied to `wiki:`, `ticket:` and `uv:` one at a time as each was
converted. The `return it.seenEof` authority idiom is written seven times.
Nothing is wrong with any single copy; the wrongness is that there are four,
and the fifth extractor will need someone to remember.

### 3b. The assertions

The root type is the assertion:

```c
typedef struct VikiAssertStruct VikiAssert;

struct VikiAssertStruct {
    const VikiAssertVftbl *vftbl;
    const char *zId;          /* content hash -- identity IS the hash */
    const char *zTs;          /* ISO-8601 UTC; lexical order is time order */
    const char *zSupersedes;  /* another assertion's id, or NULL */
};

struct VikiAssertVftbl {
    const Type *type;
    /* every slot takes VikiAssert*, at every level -- oopc rule 1, so no
    ** function-pointer cast ever arises and a mistyped slot is a compile error */
    const char *(*key)  (const VikiAssert*);  /* what it competes on */
    int         (*rank) (const VikiAssert*, const VikiAssert*); /* tie-break */
    const char *(*text) (const VikiAssert*);  /* what gets chunked and embedded */
    int         (*emit) (const VikiAssert*, Blob *pArtifact);   /* -> Fossil */
};
```

Derived by prefix layout, so upcasting is a pointer cast and costs nothing:

```c
struct VikiNoteStruct  { const VikiNoteVftbl  *vftbl; const char *zId, *zTs, *zSupersedes;
                         const char *zWho, *zDue, *zPlace, *zState; };
struct VikiEventStruct { const VikiEventVftbl *vftbl; const char *zId, *zTs, *zSupersedes;
                         const char *zUid, *zRecurrenceId; int seq; /* ... RFC 5545 */ };
```

**What this buys, concretely:** `resolve()` — *given the assertions on one key,
which is current?* — is written **once**, over `VikiAssert*`. Today it is written
twice, in different SQL, in `viki_note.c` and `viki_cal.c`, and a third copy is
pending in provenance. They also resolve *differently* — a superseded note
LEAVES the ledger, a superseded calendar assertion STAYS — and both choices are
deliberate and defensible. That is exactly the problem: two deliberate policies
with no shared type to state them against, so no reader can see that they are
answers to the same question.

**And `emit()` is the load-bearing slot**, because it is where D-10 stops being
a slogan. Truth is a Fossil artifact; the SQLite table is a projection.

Today **neither `viki_note` nor `cal_event` writes a Fossil artifact at all** —
verified, there is no such call in either file. Notes are persisted to
`captures/*.md`, which `viki_note.c:1055` calls *"TRUTH (D-10)"* in its own
words, and calendar assertions exist only in the derived cache.

So viki currently has **three truth stores**: Fossil artifacts, `captures/*.md`,
and — for the calendar — a table D-10 says is disposable. Only the first
survives a peer syncing, merges without a policy, or is tamper-evident. This is
the sharpest single consequence of the modelling problem in §2, and it is not a
style question: a calendar assertion that lives only in the cache is destroyed
by the re-index that D-10 promises is always safe.

Retrieval touches exactly one slot: `text()`. That is the whole coupling between
viki's new idea and its stored types, and keeping it to one slot is what stops
the next projection from growing its own retrieval path.

---

## 4. Pushing state opaquely to nested scope (what `retain` is)

> *"retain is pushing state opaquely to nested scope"* — Warren

**This correction kills the argument I made for it first.** I proposed `retain`
to collapse the five-parameter tail threaded through the nine extractors. That
is not what it is for: parameters threaded through *one* frame want a **struct**,
and reaching for dynamic scope to avoid typing a parameter is how dynamic scope
gets a bad name. The counter drift (`nFiles` vs `nItems`) is real, and a struct
fixes it.

The actual case is the one already in the tree, done badly.

**viki already pushes state opaquely to nested scope — with `getenv`.** Ten
values, read in four files, by whoever needs them at whatever depth:

    VIKI_FOSSIL_REPO   VIKI_FOSSIL_BIN    VIKI_FOSSIL_URL   VIKI_FOSSIL_USER
    VIKI_FOSSILSEE_LIB VIKI_MODEL_DIR     VIKI_IDENTITY_BIN VIKI_NO_FORK
    VIKI_VERSE         VIKI_ME

That is exactly the pattern — a caller provides state, a callee several frames
down consumes it, and neither names the other — implemented with the worst
available mechanism: untyped, process-global, no nesting, no lifetime, and not
safely writable at runtime.

**And there is a forcing function, not just a tidiness argument.** SCOPES §4
defines *the verse* as the set of tribes reachable from one device, and on iOS
that is **one process**. `VIKI_FOSSIL_REPO` as a process global cannot express
"this call is about tribe A, that one about tribe B" — so the current mechanism
does not merely offend taste, it caps viki at one tribe per process on the
platform where multiple tribes are the point.

`retain` gives that state a type, a scope, and a stack: a command retains the
tribe it is operating on, everything beneath recalls it, and a nested operation
on a second tribe pushes and pops without disturbing the first.

```c
typedef struct {                 /* the TRIBE being operated on -- retained */
    const char *zRepo, *zUrl, *zUser, *zKey;
    const char *zModelDir, *zEpoch;   /* viki_cache_epoch_id(), derived ONCE */
    unsigned mFlags;                  /* VIKI_NO_FORK, ... */
} VikiTribe;

static int index_wiki(const VikiExtractCtx *ctx){    /* a STRUCT: same frame */
    const VikiTribe *t = viki_recall(VikiTribe);     /* RETAIN: many frames down */
    ...
}
```

The distinction is the whole point: `VikiExtractCtx` is passed, because
`viki_cmd_index` calls these directly and a parameter is honest there.
`VikiTribe` is retained, because `viki_fossil_binary()` and
`viki_fork_forbidden()` are four frames down and must not grow a parameter
apiece — which is precisely why they read `getenv` today.

`zEpoch` living in the retained tribe also gives it one home, which matters:
composing the epoch in two places is the bug that silently turned hybrid
retrieval into BM25-only and took four m1 assertions to catch.

`ports/c/retain.h` already exists and is CI-covered across three thread-local
backends on four platforms. Nothing needs inventing.

### 4a. The win, and it is a call site

> *"`viki_note("message")` is the win — wherever you find it useful after
> retaining a viki context."* — Warren

One argument: the message. Compare today's entry point —

    viki_cmd_capture(".", zText, zPlace, zType, zWho, zDue, zState, zChannel)

— eight parameters, the first a *directory string*, every one of them plumbing
the caller had to know about. That is why nothing but `viki.c` calls it.

**`include/viki.h` is the proposed header** and it compiles clean against the
real `../retain-recall/ports/c/retain.h`. The shape was then run rather than
asserted — a stub `viki_note()` behind the real macros, exercising the four
properties that matter:

```
1. no tribe retained -> status=1 errmsg="no tribe retained"   (loud, not dropped)
2. viki_note() four frames down, through a callback, no parameter    [home]
3. nested RETAIN_BEGIN on a SECOND tribe                             [work]
4. outer tribe restored on scope exit                                [home]
5. after scope -> status=1 again
   home=3 notes, work=1 notes
```

Line 1 is the contract that matters: a memory system that quietly drops what it
was told is worse than one that is absent, because the caller believes it was
remembered. Line 3 is the one `getenv` cannot do **at all** — two tribes live in
one process, which SCOPES §4 requires and iOS makes non-negotiable.

This proves the *pattern*, not viki: the stub is ten lines and the real
implementation is the work. What it settles is that the ergonomics claim is
real and the macros behave under nesting and early scope exit.

---

## 5. What the correction deletes

Not "what it adds" — that is the point of doing it.

- **one** `resolve()` instead of two, with a third never written
- the nine hand-written extractor bodies, and with them the class of defect
  where a fix (the NUL exclusion) has to be applied three separate times
- the five-parameter tail, nine times over
- `getenv` as viki's dynamic-scope mechanism, and the one-tribe-per-process
  ceiling it imposes on the platform where the verse is supposed to live
- `viki_prov.c`'s re-derivation of `finfo` — `ARBITRATION.md` §4 already
  withdrew half its justification once the deletion bug was fixed
- the counted framing protocol, once in-process access is the only transport
- `viki_note`'s hand-rolled supersession, if notes become ticket changes
  (`ARBITRATION.md` §2b)

## 6. Sequencing

1. **`build/substrate-probe.sh`** first, and deliberately so. It is the cheapest
   item, it is the one that stops the bleeding, and it will immediately tell us
   which *other* comments in this tree are describing a limitation that no
   longer exists. Everything below is a guess until it runs.
2. `VikiCtx` + retain. Mechanical, no behaviour change, deletes the drift.
3. `VikiAssert` with `viki_note` and `cal_event` as the first two derived types;
   one `resolve()`.
4. `emit()` — notes and events become Fossil artifacts, tables become
   projections. This is the one that closes D-10 for real, and the one with a
   migration.

Steps 1 and 2 are safe and small. Step 4 is the one-way door; it should not be
started until 1 has reported.
