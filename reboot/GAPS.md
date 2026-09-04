# GAPS — this design vs. `../core/`

> **STALE AGAINST v2.** This document was written against `REQUIREMENTS.md` v1 and has not been
> revised. G-14 in particular cites `P-1..P-9`, a requirement series v2 deleted entirely. The
> comparison of mechanisms is still accurate; the requirement IDs are not. See `FINDINGS.md` T-11.

Where today's implementation stands against `REQUIREMENTS.md`. Every row names the requirement it
misses and the test that will go green when it is met.

**All findings are by inspection.** Nothing here was executed. Several are cheap to reproduce and
worth reproducing before acting on — a finding believed and not run is exactly the K3 this repository
is organized to avoid. The `repro` column says what a reproduction would look like.

---

## What `core/` already gets right

Listing this first because the gaps below are corrections to a design that is mostly sound, and
because several of these are decisions people routinely get wrong.

| Property | Where |
| --- | --- |
| Identity is content **in its position**, not content alone | `viki_core.c:706` — and the comment records the bug it fixed |
| Chunks keyed on `(id, lo, hi, model)` — the extent is *in* the key | `viki_core.c:213` |
| Signatures are rows on `(id, signer)`, not a column | `viki_core.c:230` — so a signed copy cannot be shadowed by an unsigned one under `INSERT OR IGNORE` |
| Local sequence kept out of the id, in a never-merged side table | `viki_core.c:184` |
| A partial merge is reported incomplete, never as a whole one | `viki_core.c:868` — satisfies **M-3** |
| A tombstone names its target without carrying it | `viki_trace.c:232` |
| FTS delete before row delete (external content re-reads the base row) | `drop_chunks`, `viki_core.c` |
| `SAVEPOINT`, never `BEGIN`/`COMMIT`, on a borrowed connection | `viki_core.c:671` |
| Partial projection is countable, not inferable from a total | `viki_unprojected` — satisfies **M-5** |
| The host owns the clock; core calls `time()` zero times | `viki_core.h` — satisfies **C-1** |
| Erasure is documented as best-effort, without overclaiming | `viki_core.c:1160` — satisfies **W-11** |

---

## Gaps

| # | Requirement | What `core/` does | Where | Repro |
| --- | --- | --- | --- | --- |
| **G-1** | **W-2** | A tombstone is an ordinary row and can itself be redacted | `viki_core.c:1179`, `viki_trace.c:254` | 4 commands; see below |
| **G-2** | **W-3** | `viki_blob` is never swept by any withdrawal path | `apply_redactions`, `viki_forget` | store a blob, redact it, read `viki_blob` |
| **G-3** | **W-5** | Any tombstone from any source is applied; no signature is checked | `viki_core.c:1179` | merge a store containing a tombstone nobody signed |
| **G-4** | **R-2** | `ORDER BY arank DESC LIMIT 1` — no tiebreak | `viki_core.c:800`, `viki_task.c:155`, `viki_cal.c:316` | two assertions, one key, equal `ts` |
| **G-5** | **R-4** | `NOT EXISTS(s.supersedes=a.id)` hides a row regardless of who wrote `s` | `viki_core.c:803` | supersede someone else's note from a second store |
| **G-6** | **I-1, A-2** | No author anywhere in the frame or the schema | `viki_core.c:706`, `:152` | — structural |
| **G-7** | **A-4** | No Unicode normalization before hashing | `hex_sha256`, `viki_core.c:655` | put "café" as NFC and as NFD; compare ids |
| **G-8** | **A-3** | Separator framing (US, 0x1f) rather than length-prefixed | `viki_core.c:719` | — argued safe; see below |
| **G-9** | **W-10** | `stamp_arrivals` is not reached from any withdrawal path | called only at `viki_core.c:769`, `:900` | forget an assertion; compare `viki_arrival` to `viki_assert` |
| **G-10** | **R-6** | Three different resolution statements, resolving differently | `viki_core.c:800`, `viki_task.c:155`, `viki_cal.c:316` | — admitted in `viki_core.h:155` |
| **G-11** | **R-5** | A fork is silently resolved by `max(rank)`; nothing reports it | `viki_core.c:800` | two unsuperseded assertions on one key |
| **G-12** | **W-1, W-7, W-8** | One tier only. `redact` destroys immediately; there is no seal and no retention | `viki_trace.c:254` | — structural |
| **G-13** | **W-9** | `why` is required free text, grow-only, and propagates forever | `viki_trace.c:263` | — structural |
| **G-14** | **P-1..P-9** | No permissions of any kind. All-or-nothing on the SQLCipher key | — | — structural |
| **G-15** | **A-7** | An arriving row's id is not checked against its content | `merge_into`, `viki_core.c:857` | insert a row whose id does not match, merge it |
| **G-16** | **R-3** | Nothing asserts fold-order purity | — | shuffle-insert the same set twice |
| **G-17** | **W-3** | No `secure_delete`; the only `VACUUM` is `clone` | grep finds none in `core/`, `cli/` | inspect the db file after an erase |
| **G-18** | **W-2** | `viki_put` does not consult the tombstone set, so re-authoring identical content re-adds it until the next merge | `viki_core.c:688` | redact, then put the same bytes |

---

## The three that matter

### G-1 — the remove-set is not grow-only, so the 2P-Set claim is false

`viki_trace.h:79` states that "a redacted id can never be re-added". Tombstones are ordinary rows in
`viki_assert`; `apply_redactions` deletes by target with no exclusion of `kind='redact'`, and
`viki_redact` validates only that target, why and by are non-empty. So:

> redact T → T destroyed, tombstone R remains → redact R → R destroyed → merge with any peer that
> still holds T → **T is back.**

A 2P-Set requires the tombstone set to be immune to removal. Here the tombstones live in the set that
redaction removes from. This is the structural finding; everything else on the list is smaller.

It is also what makes G-3 worse than it looks: an unauthenticated tombstone can destroy anything, and
a second one can then destroy the record that it happened.

**Fix:** refuse a tombstone whose target is a tombstone, and exclude tombstones from the sweep. Both
are one predicate. **Tests:** `test_withdrawal.py::TombstoneImmunity`.

### G-2 — erasure destroys the description and keeps the payload

Neither `apply_redactions` nor `viki_forget` touches `viki_blob`. `PRAGMA foreign_keys` appears
nowhere in `core/` or `cli/`, and the `REFERENCES viki_assert(id)` declaration carries no
`ON DELETE CASCADE` in any case.

So erasing a blob assertion removes the *description* — "scan of a lease agreement" — and leaves the
PDF. For the ethical and legal purpose the feature exists to serve, that is exactly inverted: the
description is the harmless half.

**Fix:** sweep the payload table on both withdrawal paths. **Tests:** `test_withdrawal.py::ErasureReach`.

### G-3 — redaction is an unauthenticated censorship primitive

Any tombstone is applied by any peer that receives it. `VikiIdentity` and `viki_signed()` exist, merge
correctly, and are consulted by no read path and no sweep.

So erasure is not only a privacy mechanism, it is a weapon: anyone who can write to any store that
ever merges into yours can permanently destroy any assertion whose id they know, leaving only a `why`
they wrote themselves.

This is the third distinct problem whose fix is *signatures consulted at fold time* — the others
being G-5 (anyone may suppress anything by superseding it) and the absence of authorship (G-6). One
mechanism, three problems, which is usually the sign it is the right mechanism and the reason
`REQUIREMENTS.md` makes I-1 a prerequisite rather than a feature.

**Fix:** a tombstone bites only on a verified signature from a principal holding `x` on the target.
**Tests:** `test_withdrawal.py::TombstoneAuthority`, `test_resolution.py::SupersessionAuthority`.

---

## Two that are probably fine, recorded so they are not rediscovered

**G-8 (framing).** The US separator genuinely cannot appear in kind, ts or supersedes, and JSON
escapes it inside a body. The argument is sound. Length-prefixing is specified anyway because it
removes the word "probably" at no cost, and because the argument has to be re-verified every time a
new field joins the frame — which A-2 does immediately, by adding author and mode.

**G-17 (secure delete).** SQLite leaves freed page content in place until the page is reused, and a
write that lands in the write-ahead log is readable there until checkpoint. So "destroyed" currently
means "unlinked from the b-tree". Whether that matters is a threat-model question nobody has
answered: against a regulator it probably does not, against someone holding the device and the key it
does. Listed because it is a one-pragma fix that is very hard to retrofit onto an existing store.

---

## What this repository does *not* claim

It does not claim the current `core/` is wrong to have shipped. Most of the gaps are the difference
between a design that is sound in the absence of an adversary and one that is sound in the presence
of one, and the second is a strictly later question. G-1 is the exception: it makes a stated
guarantee false today.
