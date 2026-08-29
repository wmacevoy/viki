# ARBITRATION — Fossil is the change manager, and viki keeps rebuilding one

**Status: DECIDED in conversation 2026-08-29, not yet implemented.** Written
because three separate design questions turned out to be one, and because the
sentence that named it is worth keeping verbatim:

> *"drop sql back-door in api - it is too easy to break - match data to fossil
> already. it seems you are killing yourself doing change management over a
> change manager."* — Warren, 2026-08-29

Read `SCOPES.md` for the L0–L3 split and `SYNC.md` for what a tribe may carry.
This file is about a boundary neither of them names.

---

## 0. The principle

**Fossil is a repository with arbitration.** Artifacts are content-addressed and
Merkle-linked; the sync protocol exchanges them by hash; tickets carry
field-level change records that merge; history is not a feature bolted on, it is
the storage model.

**viki is a projection with a query surface.** That is the whole job.

> **Anything in viki that resembles change management is a bug, not a feature.**

It is a bug because it is a *second* answer to a question Fossil already
answers, and a second answer that does not merge, does not sync, has no
conflict detection, and has to be maintained by hand.

---

## 1. What viki currently reimplements

Measured 2026-08-29.

| viki has | Fossil already has | viki's cost |
|---|---|---|
| `viki_note.closes` — supersession | ticket changes; artifact P-card chains | part of 1,318 lines |
| `claimed` / `lease` / `challenge` / `stolen_from` | ticket assignment with history | same file |
| `viki when` / `viki since` — file history | `mlink`/`event`; `fossil finfo` | **1,066 lines, 3 failed reviews** |
| the counted framing protocol | `fossil json` (structured, already built in) | part of 2,360 lines |
| latest-wins on a hand-edited file | artifact merge | the defect cluster below |

**Three of this session's worst defects were in that column and nowhere else:**
`structure_write()` silently deleting the user's own words from `captures/*.md`;
`emit_field()` letting captured text forge and retire a promise; the fgets-chunk
boundary that defeated the first fix. All three are change-management bugs in a
hand-rolled change manager.

---

## 2. Three decisions, which are one decision

### 2a. No SQL back-door in any API face

`viki sql` stays a **CLI** command, where the caller is a person or an agent
that already holds the tribe key. It is exposed on **no API face** — not HTTP,
not MCP. V2_DESIGN §6b reached the same place from the aggregation argument
(`ask --k 5` returns five chunks; `sql` returns the corpus), and this is the
second, independent reason: **an SQL surface is a write-shaped hole in a
read-only story, and it is too easy to break.**

The corollary that matters more, and that has been a habit rather than a rule:

> **viki NEVER writes through SQL to Fossil's tables. Every write goes through a
> `fossil` command or a file Fossil versions.**

That discipline is currently correct by accident. It needs to be a rule *before*
the ledger moves to tickets, because that is exactly where the temptation
arrives: it is easy to `INSERT INTO ticketchng` and correct to call
`fossil ticket`. The first gets a database row. The second gets an artifact with
merge, history and sync. **That difference is the arbitration.**

### 2b. Match the data to Fossil rather than inventing a parallel model

The ledger's split is by **availability**, not by taste:

- **Capture stays a file.** `viki capture` must work with no model, no network,
  no repo and no cache — a phone in a field. It is append-only and it has never
  been the source of a defect.
- **Structure and supersession become ticket changes.** `ticketchng` is
  field-level change records with history, merged field by field.
  `CALENDAR_DESIGN_V2` §4 already established tickets beat wiki on that axis:
  wiki has no merge, no conflict detection and no stale-edit check.

This deletes `structure_write()` — the data loss *and* the injection — and gets
multi-peer merge and history for free, which is what §2.2b continuity wants
anyway. The cost is honest: structuring needs a repo. But structuring is the
judgment step, which is where you are online anyway, and the
one-subprocess-per-class rule is an *indexing* constraint that does not bind on
an interactive, low-volume operation.

### 2c. Read Fossil's change model; do not mirror it

The framing protocol (`framed_next`, `split_preserve_empty`, "check the
sentinel, never the exit status") exists to get SQL out of a **subprocess**.
It is a transport workaround, and it is where the NUL desync lived — a defect
that existed *only* on the subprocess path, because in-process `libfossilsee`
passes an explicit length. **Two transports for the same reads, and the weaker
one defines the wire format for both.**

`fossil json` is built in and returns structured output. It answered
`FOSSIL-1103` from the CLI in a first probe — an auth/user-context question, not
absence — and **evaluating it is the highest-leverage thing available**, because
if it works the entire hand-rolled framing goes away, and with it the file where
seven of this session's defects lived.

---

## 3. What this says about `wip/provenance`, said plainly

`viki when` / `viki since` is **1,066 lines re-deriving what `mlink`, `event`
and `fossil finfo` already hold**, and it has now failed three rounds of
adversarial review — each round fixing the named defects and introducing a new
one.

That is not bad luck. It is the shape of the thing: a hand-built history layer
over a system whose entire storage model is history.

**This does not automatically mean discard it.** Its genuine contribution is the
*join* — "this artifact changed, and here is whether it is in your index" — and
that join is viki's to make. The question to answer before landing is narrower
than "is provenance good":

> How much of those 1,066 lines is the join, and how much is re-derived history?

If it is mostly join, land it and thin it. If it is mostly history, the honest
move is a much smaller command that shells to Fossil for the history and adds
only the index column.

---

## 4. What does NOT change

- **Reading in bulk stays SQL.** The cache is derived (D-10), so a projection is
  outside the merge model by construction, and Fossil has no bulk read API to
  use instead. Nine classes in ~7 queries versus O(artifacts) subprocesses is
  not a style preference.
- **`viki sql` stays on the CLI.** It is the only place
  `ndvss_cosine_similarity_f` exists, so without it an agent cannot run a vector
  query at all and `ask` becomes the only door (SCOPES §1b).
- **D-10 stands.** The cache is derived, disposable and rebuildable, and none of
  the above makes it truth.

---

## 5. Open

1. Does `fossil json` work from the CLI against a local repo, and does it cover
   the nine classes? **Answer this first** — it sizes everything else.
2. Does a ticket-backed note survive `fossil` being absent at *read* time? The
   projection must still answer from the cache when the repo cannot be opened.
3. Does supersession-as-ticket-change preserve `viki why`'s both-directions
   chain, or does that become a Fossil query too?
