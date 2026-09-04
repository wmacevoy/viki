# REQUIREMENTS — distributed state, v1

What must be **observably true** of the state layer. Not how. Mechanism lives in `DESIGN.md`;
the distance from today's `core/` lives in `GAPS.md`.

Every "must" here has an ID, and every ID is named by at least one test in `tests/`. `grep A-2`
walks from a red test back to the sentence that demanded it.

**Status: every test in this repository fails.** That is the intended state. These requirements
describe a system that does not exist yet, written before it exists so that the first
implementation has something to be wrong against. A test never observed red is a K3 generator
with a green checkmark.

---

## Glossary

Ten words. A new noun is an event that needs a decision, not a synonym.

| Term | Means |
| --- | --- |
| **assertion** | One immutable statement. The only kind of row that is truth. |
| **id** | `sha256` of an assertion's framed content. Identity, not a locator. |
| **key** | What assertions compete on. Several assertions may share one; one wins. |
| **rank** | A sortable string, computed at write time. Highest wins among survivors. |
| **supersede** | To write a new assertion that retires an earlier one. The only mutation verb. |
| **projection** | Derived, rebuildable, never merged: chunks, index, vectors. |
| **store** | One SQLite database holding assertions and projections. |
| **peer** | A store that merges with others. All peers are equal; a hub is a peer that only relays. |
| **principal** | Who is asking. Bound at open, never inferred from data. |
| **tombstone** | An assertion that withdraws another. Named separately because it is the one thing that removes. |

Two words deliberately **not** reused: *delete* (nothing is deleted; things are withdrawn at one of
three tiers) and *write* (nothing is written to; assertions are added).

---

## A — Assertions and identity

- **A-1** Two peers that independently state the same thing produce the same id. Identity is a
  function of content, not of arrival, position in a file, or local sequence.
- **A-2** The id covers **author, kind, key, timestamp, mode, and supersedes** in addition to the
  body. Two assertions differing in any one of those have different ids.
- **A-3** No field value can forge a frame boundary. Given any two distinct field tuples, their
  framings differ — including when a field contains the framing delimiter, a NUL, or a newline.
- **A-4** Text is normalized (NFC) before hashing. The same visible string produces the same id on
  every platform and input method.
- **A-5** Storing an assertion already present is a no-op, and the caller can tell that outcome from
  a first store.
- **A-6** There is no operation that changes a stored assertion. An attempt reports refusal.
- **A-7** An assertion whose id does not match its content is refused on arrival.

## R — Resolution

- **R-1** The current assertion on a key is the highest-ranked one that nothing supersedes.
- **R-2** Resolution is **total**: equal ranks are broken by id. Two stores holding the same set
  return the same winner.
- **R-3** Resolution is a pure function of the set. The same assertions inserted in any order yield
  byte-identical results.
- **R-4** A supersession is honored only when signed by a principal permitted to supersede its
  target. An unauthorized supersession is stored and **ignored**, not stored and obeyed.
- **R-5** A fork — two unsuperseded assertions on one key — is **reported**. A reader is never
  silently handed one arm of a fork as though it were the only one.
- **R-6** Resolution rules are declared once per kind. Two kinds may resolve differently; no kind
  resolves differently in two places.

## M — Merge

- **M-1** Merge is union. `merge(a, b)` is idempotent, commutative, and associative in its effect
  on the resolved state.
- **M-2** Merging a store into itself changes nothing and reports zero added.
- **M-3** A merge that cannot complete reports incomplete. A truncated union is never reported as a
  whole one.
- **M-4** Signatures merge alongside assertions. A signed copy is never shadowed by an unsigned one.
- **M-5** Projections are never merged. After a merge, the number of assertions not yet projected is
  reportable, and it is not inferable from any total.
- **M-6** Push and pull are the same operation with the ends swapped. Everything that happens on
  receipt happens to the destination.
- **M-7** A peer that cannot read an assertion still merges and relays it.

## W — Withdrawal

Three tiers, three words, three different promises.

| Verb | Content | Reversible by | Propagates |
| --- | --- | --- | --- |
| `withdraw` | kept, hidden from every read and index | anyone permitted to supersede | yes |
| `seal` | encrypted in place, dropped from projections | the named custodian | yes |
| `erase` | destroyed | nobody | yes |

- **W-1** A withdrawn assertion is absent from every read path and every index, and is restorable.
- **W-2** **A tombstone is immune to withdrawal at every tier.** It cannot be erased, sealed, or
  superseded away. The set of tombstones only grows.
- **W-3** Erasure destroys the payload as well as the assertion — blob bytes included.
- **W-4** Erasure destroys the projection: ranges, index entries, and vectors.
- **W-5** A tombstone takes effect only when signed by a principal permitted to redact its target.
  An unauthorized tombstone is stored and inert.
- **W-6** A tombstone that arrives before its target takes effect when the target arrives.
- **W-7** A seal carries an **absolute** deadline. Each peer promotes an expired seal to erasure on
  its own clock, at merge and at open. A peer receiving an already-expired seal erases immediately.
- **W-8** The default is a delayed seal. Immediate erasure requires an explicit, separate request.
- **W-9** A tombstone's propagating reason is drawn from a closed vocabulary. Free-text explanation
  is stored locally and does not travel.
- **W-10** No local sequence number survives the assertion it refers to. After any withdrawal the
  store cannot advertise something it cannot produce.
- **W-11** No operation reports that content was erased everywhere. Erasure across peers is
  best-effort and every surface says so.

## P — Permissions

- **P-1** Every assertion carries a mode: `r` read, `s` supersede, `x` redact, for `author`,
  `village`, `world`.
- **P-2** The mode is part of the framed content (A-2). Changing it is a new assertion superseding
  the old.
- **P-3** Loosening a mode is effective. **Tightening is forward-only**, and the API says so at the
  point of tightening rather than in documentation.
- **P-4** `world+x` is refused at write time. So is `world+s`.
- **P-5** The read filter is applied **before** ranking, not after. Asking for `k` results returns
  the `k` best *visible* ones, not the visible subset of the `k` best.
- **P-6** A filtered read reports how many rows were withheld. A caller can always tell "no answer"
  from "not for you".
- **P-7** The principal is bound when the store is opened and cannot be changed on an open store.
- **P-8** A read that bypasses the filter **fails**, rather than returning unfiltered rows. This is
  structural, not a convention every future query must remember.
- **P-9** An assertion written with no explicit mode inherits one from context. Modes are not set
  by hand per assertion.

## I — Identity and provenance

- **I-1** An assertion's author is part of its identity (A-2), not a field in its body.
- **I-2** Three states are distinguishable: **unsigned** (a claim of authorship), **verified** (the
  named author did write it), and **bad** (a signature present that does not verify).
- **I-3** Verification requires no secret. A peer holding no private key can establish who said what.
- **I-4** Village membership comes from signed grants, not from an unsigned field.
- **I-5** Four identities stay distinct and are never conflated: the **author** of an assertion, the
  **signer** of a signature, the **custodian** who can unseal, and the **principal** now asking.

## C — Time

- **C-1** The state layer calls no clock. The host supplies time at every point one is needed.
- **C-2** `ts` (wall clock, meaningful across peers, a lie when the clock is wrong) and `seq`
  (local, monotonic, meaningless to a peer) are separate fields and are never substituted for each
  other.
- **C-3** A malformed or non-normalized timestamp is refused at write time.
- **C-4** Rank is declared per kind. It is not implicitly the timestamp.
- **C-5** Retention deadlines are absolute, computed from the tombstone's own `ts`.

## E — Failure

For every input the layer depends on, four cases have a stated answer: **absent, late, wrong,
lying.**

- **E-1** A write that stores nothing is an error, never a silent success.
- **E-2** A read that is partially blind — unprojected, filtered, or unreadable — says so in its
  result rather than in its documentation.
- **E-3** Every refusal names which requirement refused it.
- **E-4** A merge source that is not a store of this shape is refused, not partially consumed.

---

## Chosen falsehoods (K4)

Each is false. Each is load-bearing. Each has a blast radius.

| Falsehood | Where it is load-bearing | What breaks when it comes due |
| --- | --- | --- |
| Clocks agree well enough | `rank` for kinds that rank by `ts`; seal deadlines (W-7) | Concurrent offline writes on one key resolve by skew rather than by intent |
| Peers apply tombstones honestly | Every claim about erasure (W-3, W-11) | Content survives on a peer that merged and skipped the sweep. Undetectable. |
| An author's key is uncompromised | Authorship (I-1), supersession (R-4), redaction (W-5) | A stolen key can suppress and destroy anything its owner could |
| `sha256` is collision-free | Identity (A-1) | Two different assertions become one row |
| Villages are few and mostly disjoint | The single `village` class in the mode (P-1) | Combination-groups, combinatorially |
| A peer merges again eventually | Erasure reach (W-11) | A peer offline forever keeps everything, including what was erased |

## Non-goals

Written down so they stop being re-argued, and so their absence is a decision rather than a gap.

- **No network.** Merge takes an open store. How it got there is the host's problem.
- **No filesystem.** The host opens the connection and has therefore already chosen the key, the
  location, and the platform's storage rules.
- **No retrieval.** Ranking, embedding and search are a separate layer over this one. This document
  covers only what is true, who may see it, and how it goes away.
- **No write prevention.** In a union-merge store you cannot stop a peer writing. `s` and `x` are
  *fold-time* filters — what this store honors — not gates on what a peer may author.
- **No key management.** Sealing needs a custodian's key; where that key lives is out of scope.
- **No erasure guarantee.** See W-11.

## Not yet answered

Open. Until these are answered, anything depending on them is a guess nobody has agreed to.

1. How many villages, and do they overlap? (Decides whether P-1's single class survives contact.)
2. What is the default retention on a seal — and who is entitled to decide it?
3. Is a hub trusted to sweep? A hub that does not apply tombstones re-serves erased content (M-6).
4. What is the closed vocabulary in W-9?
5. Does an unsigned assertion resolve at all, or only display? (I-2 distinguishes the states; it
   does not say what resolution does with them.)
