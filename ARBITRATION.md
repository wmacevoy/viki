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

`fossil json` is built in and returns structured output.

### MEASURED 2026-08-29, and it does NOT do what I hoped

I wrote, an hour before testing it, that evaluating `fossil json` was "the
highest-leverage thing available, because if it works the entire hand-rolled
framing goes away". **That was wrong, and the measurement is the useful part.**

What works, from the CLI, with `-R` and no checkout:

| call | result |
|---|---|
| `json finfo --name P` | full history for one path: checkin, user, comment, state |
| `json wiki list` | **bulk** — every page name in one call |
| `json wiki get NAME` | one page **with content** |
| `json artifact UUID` | one artifact |
| `json timeline checkin` | paginated, `limit` defaults to 20 |
| `json ticket` | **FOSSIL-1102, not implemented in Fossil** |

**Three reasons it does not replace the framing protocol:**

1. **Content is per-artifact.** `wiki list` is bulk but carries only names;
   content comes from `wiki get`, one call per page. Wiki extraction would be
   `1 + N` subprocesses where the framed SQL is **1**. That is precisely the
   O(artifacts) cost the framing protocol exists to avoid, and on a repo with
   500 wiki pages it is 501 process spawns against one.
2. **Tickets are absent.** `json ticket` maps to `json_page_nyi`. So a
   ticket-backed ledger cannot read through JSON at all.
3. **`finfo` does not report deletions.** Measured: a file added, modified and
   then `fossil rm`'d reports `added` and `modified` and **nothing for the
   delete**. That is exactly the blind spot `viki when` was built to fix, and
   the reason its probe mutates `AND ml.fid <> 0` — the mutation that makes
   `when` behave like `finfo` and drops every deletion.

   > **2026-08-29, and this one changed:** it is a *bug*, not a limit. The
   > query inner-joins `blob b` on `b.rid=mlink.fid` while a deletion has
   > `fid==0`, so the join removes exactly the rows the `isDel` column it also
   > computes exists to flag. Fixed for the JSON route in fossil-see
   > (`fossil-json-finfo-deletions.patch`, vendored); the CLI has the same
   > defect and the **web** `/finfo` page does not. So the argument in §4 that
   > deletions are "genuinely viki's and NOT available from Fossil's own
   > surfaces" is now **wrong as stated** — Fossil can report them, and one of
   > its faces always could. See FINDINGS.md.

**So the framing protocol stays.** It is solving bulk content read, which JSON
does not solve. What was actually wrong with it was never SQL — it was having
**two transports** and letting the weaker one define the wire format. The
in-process path already carries lengths correctly; the subprocess path is where
the NUL desync lived.

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

**Partly answered by the JSON measurement above, and the answer is better for
provenance than I expected.** Two things in it are genuinely viki's and are NOT
available from Fossil's own surfaces:

- **Deletions.** ~~`fossil finfo` and `json finfo` both omit them.~~
  **Withdrawn 2026-08-29.** They omit them because of a two-line bug, now fixed
  in the vendored build for the JSON route; the web `/finfo` page never had it.
  So this is not a gap viki has to fill — the deletion row is available from
  Fossil, and re-deriving it in `viki_prov.c` would be duplicating a surface
  that works rather than adding one that does not. The honest remaining claim
  is the next bullet only.
- **The index join.** "This artifact changed, and here is whether it is in your
  cache" is viki's question by construction.

So provenance is not merely re-derived history — but the case for it is now
**one bullet, not two**, and that is a materially weaker case than when this
section was written. What it should NOT do is re-derive the parts Fossil does
give, and with deletions moving into that category the thinning target is
anything in those 1,066 lines that is not the index join.

**Sequence:** this thinning is a rewrite, and the branch has already failed
three review rounds. Land nothing from it until 2b is settled, because
notes-as-tickets changes what `viki why` and `viki when` even need to do.

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

1. ~~Does `fossil json` work from the CLI?~~ **ANSWERED 2026-08-29 — yes, and
   it does not help enough.** Per-artifact content, no tickets. See §2c. The
   framing protocol stays; the two-transport problem is the real defect and
   `libfossilsee` is already its fix. (The third reason given here, "no
   deletions in `finfo`", was a bug and has been fixed — it is no longer an
   argument for anything. The first two still hold and are sufficient.)
2. Does a ticket-backed note survive `fossil` being absent at *read* time? The
   projection must still answer from the cache when the repo cannot be opened.
3. Does supersession-as-ticket-change preserve `viki why`'s both-directions
   chain, or does that become a Fossil query too?
