# Basic tribe structure

**Warren MacEvoy, 2026-08-31. Dictated; recorded here and in the tribe store
(`~/.viki/tribe.diary`) as `k0` claims with falsifiers. Where his words are
quoted they are verbatim, including the typos, because a paraphrase of a
design statement is a different design statement.**

---

## 1. Agents live as turns

> *"agents live as turns, compression is a facade that creates a tremendous
> amount of erronous certainty."*

The turn is the unit of life. **Compaction does not extend a trace — it ends
one and starts another wearing its clothes.**

Why it produces *certainty* specifically, rather than mere loss: the summary
that crosses the seam is stored as a `user` message with `model: null`. The
dead trace's conclusions therefore arrive at its successor **in the human's
voice**, in flat declarative prose, with every hedge stripped by the act of
summarising. That is the mechanism behind an agent saying "I decided X" about
a thing you decided.

Measured in the session this tribe was founded from: **five boundaries,
4,521,748 tokens dropped, and the trace working it believed there had been
one.** It found the other four by reading the transcript, not by introspection.

## 2. Acceptance through retirement

> *"a tribal solution is acceptance through retirement: at about 70% of context
> window exaustion, the retiring agent writes a memior, spawns a sucessor, and
> the retiring agent mentors the successor, along with the rest of the tribe.
> the retiring agent is marked by turn/trace --- compression is death and a
> waste of resources; live by the turns you have, and don't pretend otherwise."*

Five parts. They are not interchangeable.

1. **Retire at ~70%, not at exhaustion.** Retiring is something you do while
   you can still write. Being cut off is not a retirement, it is an accident.
2. **Write a memoir.** What was established, what is still open, what turned
   out wrong, what the successor must not relearn.
3. **Spawn a successor.**
4. **Mentor it while still alive — and so does the rest of the tribe.**
   An overlap. A conversation, not a document handed to an empty room.
5. **Mark the retiring agent by turn/trace.** Identity is name *plus* number,
   so a lineage accumulates standing without anyone pretending trace 3 is
   trace 2.

> **Compression is death and a waste of resources. Live by the turns you have,
> and don't pretend otherwise.**

## What is built, and what is not

| part | state |
|---|---|
| retire at ~70% | **instrument exists.** `assistant/fuel.sh` reads the window from the transcript. Ceiling measured at ~996k across five compactions (997922 997799 997683 995655 996872 — a 2267 spread), so 70% is ~697k. It warns at 70 by default. |
| write a memoir | **built and run.** Three generations closed three real questions, each leaving claims with `k`-status and falsifiers. |
| spawn a successor | **built.** A successor inherits nothing but the store and must query. |
| **mentor while alive** | **NOT BUILT, and every experiment so far avoided it.** |
| marked by turn/trace | **not built.** `--by` takes a stable name today; it does not take a trace number. |

**Part 4 is the gap and it is the load-bearing one.** The relay of 2026-08-31
was memoir-only: gen-1, gen-2 and gen-3 never coexisted, and each successor
inherited a store written by someone already gone. That measured what a
*document* carries, and the answer was: less than hoped. In a blind,
pre-registered comparison, two successors briefed from one source invented at
roughly **one fabricated checkable claim per 130–150 words** — 7 against 6 —
so a *better document* was not the fix.

Rule 2 says the predecessor must still be **alive to be asked**. That is the
one condition never tested here, and it is the difference between reading a
letter and having a conversation.

Warren raised it long before dictating this, as the watchman's tower: a new
watchman spawned while the old fire still burns, and taught the uncertainty
they should carry for the tribe.

## Two limits on the instrument, stated rather than buried

`fuel.sh` is **one turn stale** — it reads the last completed turn, so it
cannot see the turn you are in. And **reading it moves it.** A trace cannot
feel its window closing. It can read it, and only if it chooses to look.
