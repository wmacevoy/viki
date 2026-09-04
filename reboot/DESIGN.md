# DESIGN — chosen mechanism

How `REQUIREMENTS.md` is intended to be met, and what was rejected. Requirement IDs are load-bearing:
if a mechanism here does not trace to an ID, it is speculative structure and should be deleted.

---

## 1. The shape in one paragraph

Truth is a **grow-only set of immutable, content-addressed assertions**. Everything a caller reads is
a **fold** over that set. Merge is set union, so delivery may be out of order, duplicated, partial or
repeated forever and every peer holding the same facts computes the same answer. Two fold rules run
together: **causal** (`supersedes`, exact, needs no clock) and **last-writer-wins** (`rank`, only as
good as the clocks). Withdrawal is the one exception to grow-only, and it is bought at a stated price.

## 2. Why this is a semilattice, and where that stops being true

Union is associative, commutative and idempotent, so the assertion set is a join-semilattice — the
simplest CvRDT there is. The assertions are *operations* ("this supersedes that", "destroy this"),
which normally forces an op-based CRDT and its exactly-once causal delivery requirement. Content
addressing removes that: redelivering an operation is a no-op at the set level. **That substitution
is the load-bearing idea in the whole design.** Op-based expressiveness, state-based delivery.

Withdrawal breaks the G-Set and makes it a **2P-Set**: an add-set and a remove-set, both grow-only,
merged independently. Convergence survives; re-addition does not. This only holds if **the remove-set
is itself immune to removal** — which is W-2, and which is the single property today's `core/` does
not have (`GAPS.md` G-1).

## 3. The vocabulary of the frame

`id = sha256(frame(author, kind, key, ts, mode, supersedes, body))` — A-1, A-2.

Two decisions inside that:

**Length-prefixed, not delimited** (A-3). Today's `core/` joins fields with `\x1f` and argues the
separator cannot occur in any field. That argument is probably right and it is doing work a length
prefix does for free. Each field is emitted as its byte length, a colon, then its bytes; no field
value can produce a framing a different tuple also produces.

**Normalized before hashing** (A-4). `café` in NFC and in NFD are different bytes for the same
visible string, and macOS filesystems and input methods emit NFD routinely. Unnormalized, two peers
writing the same note produce two assertions — silent, permanent fragmentation of exactly the kind
content addressing exists to prevent.

**Author is inside the frame** (I-1). This is the change with the widest consequences. Authorship
cannot be a body column, because anyone can write any value into a column; and it cannot be only a
signature, because signing is allowed to decline (a locked keychain, a cancelled prompt) and an
optional field cannot carry a mandatory meaning. Folding the author's public key into the frame makes
attribution part of identity: it cannot be rewritten without producing a different assertion.

The cost is real and is accepted: **identical content from two authors no longer deduplicates.** For
notes that is correct — "Alice said X" and "Bob said X" are two facts, and a model that merges them
has lost the more interesting half. For large payloads it does not arise; those key on a caller-
supplied content hash on a separate path.

## 4. Resolution

`current(key)` = highest `rank` among assertions on that key that nothing *validly* supersedes.

**Total order** (R-2). `ORDER BY rank DESC, id DESC`. Ties are not exotic — two writes in one clock
tick, or two offline peers whose hosts emit the same ISO string — and without an explicit tiebreak
the winner is whatever the query plan yields. That is determinism by accident of index shape, and it
does not survive a plan change or a different engine.

**Purity is testable** (R-3). The same assertions inserted in a shuffled order must produce
byte-identical results. This is the cheapest possible defence against the whole class of bug that
otherwise appears only when two peers sync in different orders — which is to say, in production,
once, unreproducibly.

**Forks are surfaced, not resolved** (R-5). Two unsuperseded assertions on one key is a real
disagreement. The storage layer's job is to lose nothing; deciding is a layer up, done by someone
with an opinion. Silent LWW on a fork is how a system stops being believed.

**Rank is declared per kind** (C-4, R-6). Wall-clock rank is the default and the weakest; a kind that
can carry a logical counter should, and then skew moves timing rather than outcome.

## 5. Permissions: two mechanisms, two adversaries

The security question is not "where does the ACL go". It is **who is the adversary**, and there are
two answers that need different machinery.

| | Stops | Costs |
| --- | --- | --- |
| **Filter in the state layer** | readers that cannot reach the bytes: agents, API clients, a service tier | nothing — search intact, grants retroactive, revocation immediate |
| **Separate key** | readers that hold the bytes: peers, stolen devices, hubs, backups | cross-scope search, forward-only revocation, key custody |

v1 builds the **filter**, for everything, and reserves separate keys for a small `sealed` class where
a peer *is* the adversary. That is a deliberate reversal of the tempting order: cryptographic
partitioning is the stronger mechanism and the wrong one to reach for first, because it costs the
retrieval layer its entire premise and buys protection against an adversary who is not the common
case.

The filter is a real boundary for the case that matters most here — an agent reading
attacker-controlled text is one prompt injection away from exfiltrating whatever it can reach, and
an agent that speaks only this API and never sees the key is confined by it.

**Mode bits, not an ACL table** (P-1). Nine bits: `r`/`s`/`x` × author/village/world. Storage is
noise against a 64-character id and a JSON body. Query cost is a few bit tests per candidate, and
"my villages" is a small set bound at open, so the village class is an `IN` over constants rather
than a join.

Three deliberate departures from the Unix model it borrows from:

- **`w` is spelled `s` (supersede)**, because nothing is ever written to. Naming it `write` would
  promise an operation that does not exist.
- **`world` is not "other users"** — it is every peer this store transitively reaches, forever.
  `world+r` is publication. The class is named `world` rather than `other` so nobody sets it casually.
- **`chmod` is a supersession** (P-2), because the mode is inside the frame. Loosening works;
  tightening is forward-only, and P-3 makes the API say so rather than leaving it to be discovered.

**Structural, not remembered** (P-8). SQLite's authorizer denies the base tables and permits only the
filtered views, so a query that forgets the filter fails to *prepare* rather than returning the wrong
rows. The authorizer is compile-time and sees no row values, so it cannot express "rows I authored" —
it is the doorframe. Row-level filtering lives in the view predicate against a principal bound at
open (P-7). Binding at open, rather than allowing a principal to change on a live connection, also
removes the stale-authorization question entirely: a different principal gets a different connection.

**A umask, or the bits will be wrong** (P-9). The only reason nine bits works in Unix is that almost
nobody sets them per file. An agent writing two hundred notes a day will set two hundred modes and
some fraction will be wrong in the direction of over-sharing, which is the direction that does not
announce itself.

## 6. Withdrawal: three tiers

One verb was always going to be wrong, because "I don't want to see this", "keep it where only the
auditor can reach it" and "destroy it" are three different requests with three different reversibility
profiles, and granting them together grants the worst of them.

**`withdraw`** is what most deletes actually mean. Reversible, honest that nothing was destroyed.

**`seal`** is the escrow tier and the interesting one. The seal is an *instruction*, not a payload:
each peer encrypts its own copy in place under a key wrapped to the custodian. Rejected alternative —
carrying the sealed bytes in the tombstone — because it publishes the ciphertext of the thing you
wanted gone to every peer forever, makes a tombstone as large as its target, and throws away the
property that makes the design work: **a tombstone names what to withdraw without carrying it**, since
an id is a hash. Sealing in place keeps that, and makes expiry cheap: at the deadline a peer destroys
the 32-byte wrapped key, not the payload, so a 23 MB blob expires in constant time — and that is
crypto-shredding, so it reaches copies in backups too.

**The remove-set gets its own table**, and that is the fix for the top gap made
structural rather than remembered. In a 2P-Set the add-set and the remove-set are different objects;
`schema.py` gives them different tables, the withdrawal code deletes from `assertion` and never from
`tombstone`, and W-2 therefore holds because there is no statement that could break it. The
alternative — one table plus a predicate excluding `kind='redact'` from the sweep — is one line
shorter and one line away from being forgotten by the next person who writes a sweep.

**`erase`** is destruction. It is also, structurally, a **censorship primitive** — which is why W-5
requires a verified signature from a principal with `x` before a tombstone bites. Without that check,
anyone who can write to any store that ever merges into yours can permanently destroy anything whose
id they know, and then (absent W-2) destroy the evidence they did it.

**The clock works out** (W-7). Expiry is local and monotonic: each peer checks its own clock and
promotes expired seals. No agreement on time is needed, a peer offline for a year erases on first
wake, and one receiving an already-expired seal erases immediately. Skew moves timing, never outcome.
This is why the deadline is absolute from the tombstone's `ts` rather than a duration from receipt.

**Delay is the default** (W-8). Erasure is irreversible and the common failure is a mistake — wrong
id, wrong scope, an agent acting on a misread. A seal with a deadline costs nothing, destroys nothing
a real erase cannot finish, and provides an undo. Immediate erasure is for the case where the window
*is* the risk: a pasted credential, where every day it sits sealed is a day it can be unsealed.

**The reason is a closed vocabulary** (W-9). Requiring a free-text `why` is good for accountability
and unexamined for privacy: it is mandatory, grow-only, propagates forever, and the natural thing to
write in it describes the very thing being erased. A code travels; the sentence stays home.

## 7. Module map, and the SOLID reading of it

Nine modules. Each is small enough to read in one sitting, which is the actual design constraint.

| Module | Holds | Depends on |
| --- | --- | --- |
| `ports.py` | protocols only — `Clock`, `Signer`, `Verifier`, `Custodian`, `KindSpec` | nothing |
| `errors.py` | the refusal taxonomy, each naming a requirement | nothing |
| `model.py` | `Assertion`, `Mode`, `Tier`, `SigState` — data, no behavior | nothing |
| `ids.py` | `frame()`, `compute_id()` | `model` |
| `schema.py` | DDL and the authorizer | nothing |
| `store.py` | open, bind principal, transactions | `ports`, `schema` |
| `writer.py` | `put`, `note`, `supersede` | `ports`, `ids`, `store` |
| `reader.py` | `get`, `current`, `forks`, `visible` | `store`, `acl` |
| `acl.py` | mode arithmetic, umask, the visibility predicate | `model` |
| `merger.py` | `merge`, `push` | `store`, `withdrawal` |
| `withdrawal.py` | `withdraw`, `seal`, `erase`, `sweep`, `promote_expired` | `store`, `ports` |

**Single responsibility.** `ids.py` computes ids and does nothing else — which is what lets A-3 and
A-4 be tested without a database. `clock` is not a function anyone calls; it is a port.

**Open/closed.** A new kind of assertion arrives as a new `KindSpec` — key, rank, canonical bytes,
resolution policy — and no module above changes. This is the one extension point, and it exists
because there are already four kinds (note, task, grant, tombstone) rather than on speculation.

**Liskov.** Every `KindSpec` is usable wherever another is: `put` never asks what kind it has.
Tombstones are the tempting exception — they are the one kind with special sweep behavior — and the
resolution is that the *sweep* knows about tombstones, not `put`. If `put` ever needs `if kind ==`,
the substitution has failed and W-2 will be enforced in the wrong place.

**Interface segregation.** A caller that only reads needs no `Signer`. A caller that only writes
notes needs no `Custodian`. This is why the ports are five small protocols rather than one context
object: a host should never be asked to supply a capability it will not use.

**Dependency inversion.** The state layer calls no clock (C-1), links no crypto, and opens no file.
It receives `Clock`, `Signer`, `Verifier` and `Custodian`. That is what makes a store reproducible
from its inputs, and it is what lets a test drive time and signatures directly rather than waiting
for them — the `T-2` move from the course material, applied to a different domain.

## 8. Rejected

| Rejected | Why |
| --- | --- |
| Encrypt per scope first | Breaks retrieval, which is the product. The filter covers the common adversary at no cost. §5. |
| Tombstone carries the sealed payload | Publishes what you wanted gone, to everyone, forever. §6. |
| ACL rows per assertion | O(assertions × principals) to store and evaluate, for flexibility nobody has asked for. Nine bits is a denormalized cache of that relation; add the relation when a village overlaps. |
| A `deny` bit | Unix never had one and mostly survived. Revisit when "the village except one person" is a real request rather than an imagined one. |
| Vector clocks on assertions | `supersedes` already carries the causality that matters, and a vector clock is per-peer state in a system whose whole premise is that peers are interchangeable. |
| A total log with offsets | A log needs a leader. A set does not, which is why peers here are symmetric. |
| `pytest` | `unittest` is in the standard library. A dependency is a thing every reader must learn, trust, audit and update; this suite needs none. |

## 9. Deferred

Named so they are not mistaken for oversights.

- **Key custody for seals.** `Custodian` is a port with no implementation. Sealing is specified
  (W-7, W-8) and unbuilt.
- **The closed vocabulary of W-9.** Requirements question 4.
- **Multi-village membership.** P-1 has one class. The escape hatch is a grant relation behind the
  same predicate function, which is why `acl.py` exposes a function rather than inlining bit tests.
- **Partial replication.** Every peer holds everything. When that stops being affordable, "converged"
  becomes per-scope and every read needs to report its own blindness — a change to E-2, not to merge.
- **The projection layer.** Chunks, index and vectors are named in M-5 and W-4 and are otherwise
  another document's problem.
