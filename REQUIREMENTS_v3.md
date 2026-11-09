# REQUIREMENTS v3 — DRAFT, not confirmed

Drafted by Claude Opus 5.5 from the 2026-11-08/09 conversation with Warren MacEvoy, who has not yet
read it back. *Tell us what is wrong with it.* Nothing here binds until Warren confirms it.

**v3 adopts v2 in parts, never whole.** v2 is `reboot/REQUIREMENTS_v2.md` on
`origin/reboot-state-spec` at `28966cd`: a source to move from, kept there and not edited. The reboot
may never land as a branch; its parts may move one at a time. So:

- **A v2 requirement is in v3 only once it is moved:** its text copied here under its own ID, with
  `(moved from v2 at 28966cd)`, together with the tests that check it. Changing it while moving makes
  it a v3 requirement, marked `(changed in v3)`.
- **A v2 ID cited here and not yet moved** is a pointer to that commit, not a requirement of viki.
  Every such pointer is a part waiting to move, or a citation to remove. The gate (FIN-3) lists them.
- **Until moved, nothing in v2 binds** — including its Deleted, K4 and Open sections.

What v3 itself adds, in order of weight: a mission, a fundamental shape, a goal of iterating to
completion, signed time (C), search (Q) and relations between assertions (L).

Words are used as v2's glossary defines them; the glossary moves with the first part, and is the one
part that must. New words: **coverage**, **relation**, **grounds** (defined where first used).

---

## Mission

- **MISSION-1** *(since v3)* *(judged)* viki is a small, embeddable store for information that
  arrives messy, from many sources, and outlives the place it came from. It keeps that information
  encrypted and tamper-evident, makes it searchable, lets every copy synchronize and merge, says
  where each piece came from, who vouched for it and when, and forgets what ethics or the law
  requires it to forget, as far as a tombstone. viki is its own component: it depends on no outside
  service to do any of this.
  Judge: Warren, at each release. Veto: any FIN-3 gate red.

**Application spaces.** Each names the series that serve it; a series serving none is a candidate for
deletion (FIN-2).

- **AS-1** *(since v3)* **Data ingress.** Unattended robots bring outside content in as references
  plus untrusted text. Served by X, V-5, D, W-12. What an ingress knows about the outside — including,
  optionally, time from an outside time server — it records as ordinary assertions; viki stores them
  and gives them no special meaning.
- **AS-2** *(since v3)* **Memory.** A person's or an assistant's record of what was said, owed and
  decided, kept current by supersession and searchable. Served by A, R, Q, L, C.
- **AS-3** *(since v3)* **Source control.** The history and merging of versioned content across
  peers: R-8's log view, S, M, V-1 provenance. **Content history only**: checkouts and working trees
  are the host's (see SHAPE says no to; Not yet answered 3).

## Iteration to completion

The goal is a viki that can be finished: used, pinned and left alone, the way SQLite is. A fundamental
shape that never stops changing means nothing built on it can finish either (SE1/A12, SE1/D9).

- **FIN-1** *(since v3)* *(judged)* viki iterates toward a **finished 1.0**: a release whose
  requirement set is closed. After 1.0, releases are patches — fixes, and no new meaning. New meaning
  is viki 2: a new major version with its own requirements version, which 1.x users need not take.
  Judge: Warren.
- **FIN-2** *(since v3)* Every iteration ends with all of: a running thing from a clean checkout;
  the full check green; a diff to these requirements, or "none"; a FINDINGS entry; and an answer to
  *what can we delete?* — a requirement, a series, a mechanism (SE1/W9).
- **FIN-3** *(since v3)* Completion is **seen, not reported**. One gate, run by CI, prints for every
  requirement ID in 1.0 scope whether it is green, red, or not reached by any test, and exits
  nonzero on any red. v2's `check.py` is its seed. The gate can veto FIN-1; it cannot pass it.
- **FIN-4** *(since v3)* **1.0 scope is a list, and Warren closes it.** After it is closed, adding
  an item names what leaves or what slips, in the same change (SE1/P1). Until it is closed, this
  document has no 1.0 (Not yet answered 6).
- **FIN-5** *(since v3)* The mission and shape change only by a new requirements version, never by
  an edit, and the change walks up: every requirement citing a moved SHAPE ID is re-read (SE1/W14).
- **FIN-5a** *(since v3)* **A part moves with its proof.** A requirement moved from v2, or from
  `core/`, comes with a test naming its ID that has been seen to fail and then pass here. A part
  with no such test has not moved. 1.0 cites no unmoved v2 ID.
- **FIN-6** *(since v3)* From 1.0, the library's interface and its stored format are promises: a
  field never changes meaning, and a 1.0 store opens in every 1.x (SE1/D11).
- **FIN-7** *(since v3)* **Before 1.0, nothing is a promise.** *"this is a fast-move phase; nothing
  important depends on this, so completely changing it to make it better is fine until 1.0"* —
  Warren, 2026-11-09. Until 1.0, a requirement, an interface or a stored format may be changed or
  thrown away outright when that makes viki better, with no migration and no compatibility path.
  The change still goes through FIN-5 (a new version, for mission and shape) and FIN-5a (a test).

## Fundamental shape

Held by: **Warren.** Most of the job is saying no, including to good ideas.

- **SHAPE-1** *(since v3)* **One kind of record.** An immutable, content-addressed assertion. Notes,
  tasks, references, relations, grants and identities share its shape. Truth is bytes (A-4a).
- **SHAPE-2** *(since v3)* **Change only by adding.** Supersede or tombstone. Merge is union,
  tombstones included, and nothing brings back what was forgotten (W-6).
- **SHAPE-3** *(since v3)* **Forget as far as a tombstone.** Content can go; its id and the fact that
  it was forgotten stay. So signatures and every other proof cover ids, never content alone, and
  erasure leaves them valid.
- **SHAPE-4** *(since v3)* **Every claim says who, from what, and when.** Author and timestamp are in
  the frame and signed with it (I-1, C-6); signatures per signer (I-2); unsigned is a claim (I-7);
  derived names its sources (V-1).
- **SHAPE-5** *(since v3)* **Everything else is a projection**: search indexes, vectors, the cache.
  Rebuildable, never merged (M-5), and every answer states its coverage (Q-2).
- **SHAPE-6** *(since v3)* **Its own component.** viki needs no outside service. The host supplies
  the world through ports: clock, keys, fetching, files, network (C-1); core never opens a socket.
- **SHAPE-7** *(since v3)* **Judgment lives outside.** Robots, agents and people judge; viki records
  what was judged and by whom (V-4a, L-4).
- **SHAPE-8** *(since v3)* **The diary is the unit** of access, sync policy and encryption at rest
  (D-1, D-3).
- **SHAPE-9** *(since v3)* **Small enough to hold.** One person can read the whole core and explain
  every part (SE1/A8). A part nobody can explain is a defect, not a feature.

**Says no to:** judging content; fetching content; depending on an outside service; working trees
and checkouts; an authentication layer inside viki (SYNC.md 0b: a guardrail, not a boundary); a clock
inside core; a row that changes; a proof over content that erasure would break; per-assertion
permissions (v2, Deleted).

**Exceptions, with their reason:** erasure (W-1 `erase`, W-3) is the one operation that destroys
bytes in truth — because ethics and the law may require forgetting.

## Lived with, by decision

- **Forgetting against remembering.** *"the pull between forgetting and remembering is k1 to live
  with (redaction up to tombstones)"* — Warren, 2026-11-09. Provenance and union merge want to keep;
  the law and ethics want to forget. viki forgets content and keeps the tombstone, and does not try
  to resolve the pull further. Where it bites: the id of erased content survives (Not yet
  answered 2), and a peer that never syncs again keeps its copy (W-11).

---

## C — Time: viki's own signed timestamps (additions)

Assumes v2's C-1 to C-5, which are not yet moved. C-1 above all: **the layer calls no clock**; the
host's clock port supplies `ts`.

- **C-6** *(since v3)* A timestamp is **the author's signed claim**: `ts` is inside the frame, so the
  author's signature covers it, and no one can move an assertion's time without breaking that
  signature. It proves what the author said and when the author said it was, not when it was.
- **C-7** *(since v3)* Every read path that shows a time shows whose claim it is. A time is never
  presented as more than the claim of the principal who signed it.
- **C-8** *(since v3)* Outside time evidence is optional and is not viki's. An ingress that tracks it
  writes it as an ordinary assertion about other assertions (AS-1), and viki neither requires nor
  interprets it.

## Q — Search (new series)

v2 excluded retrieval. v3 brings it into scope as a projection, under rules that keep it one.

- **Q-1** *(since v3)* Search is a projection (M-5): rebuilt from truth, never merged, free to
  differ per engine, and discarding it loses nothing.
- **Q-2** *(since v3)* Every answer states its **coverage**, in three states never conflated:
  **found**; **searched and not found** — which assertions, by which method, at which projection
  version; and **blind** — unreadable, uncached, sealed, or not yet indexed. Extends E-2.
- **Q-3** *(since v3)* A "not found" is only as good as its search, so each method carries a
  self-check that finds a planted case, and a method whose self-check fails reports blind, not
  not-found (DM1/U10).
- **Q-4** *(since v3)* A hit is returned with its resolution context: whether it is current or
  superseded, and every standing relation (L) that touches it, each with its author, its grounds,
  and whether those grounds have moved (L-4a).
- **Q-5** *(since v3)* Withdrawn, sealed and erased content never appears in an answer, and erased
  content is gone from every projection (W-3). Search requires `r` (D-3a0).
- **Q-6** *(since v3)* **1.0 has four methods:**
  - **grep** — exact substring over every readable assertion's text, exhaustive.
  - **regex** — a regular expression over the same text, exhaustive, in one stated dialect.
  - **bm25** — ranked full-text search over the same text.
  - **vector** — **optional.** Nearest neighbours by embedding, under one pinned model per diary.
    The model is identified by its bytes, and the embedding is a deterministic function of (content
    id, model id, chunking), so peers that compute it agree. Which model is a design choice, not a
    requirement. A peer without the model stores, syncs and answers everything else, and reports
    vector search as blind (Q-2), never as not-found. Vectors are a projection (M-5), so a peer
    without the model does not receive them from one that has it.
  grep and regex are exhaustive, so their "not found" covers every readable assertion. bm25 and
  vector are ranked, so their "not found" covers only what they returned, and says so (Q-2).
- **Q-7** *(since v3)* A query may combine methods. A combined answer states each method's coverage
  separately; it never reports one coverage for the whole.

## ENV — Where viki runs (new series)

- **ENV-1** *(since v3)* **In 1.0** (Warren, 2026-11-09): viki runs on an ESP32 — which variant,
  and whether PSRAM is required, is not yet stated (an S3 with PSRAM is *invented*) — with everything
  in 1.0 except vector search: store, resolve, sign, verify, merge,
  sync, withdraw, grep, regex and bm25.
- **ENV-2** *(since v3)* Every operation runs in **bounded memory**, stated per operation and
  independent of the store's size: merge, sync, resolution, edge recomputation and search stream
  rather than load. Set by the host, like S-8's receiver budget. *(The bound itself: invented when
  measured, not before.)*
- **ENV-3** *(since v3)* Optional parts are optional at build time: a build without vectors links no
  embedding runtime and carries no model.

## 1.0 scope — DRAFT, not closed (FIN-4)

Drafted by Claude Opus 5.5 on 2026-11-09, from ENV-1's list. **Warren closes it, and until he does
this document still has no 1.0.** Each verb in ENV-1 maps to the v2 series that define it. Under
FIN-5a, a v2 ID listed here binds only once it has moved with a test seen red and then green.

**In: named by ENV-1**

| ENV-1 says | Requirements | Notes |
| --- | --- | --- |
| store | A-1 to A-8; D-1 to D-3e; E-1, E-3; N-7 | A comes first (Not yet answered 9). N-7 (a raw database key) is the "encrypted" in MISSION-1 |
| resolve | R-1 to R-9; C-1 to C-4; C-6, C-7 | |
| sign | I-1, I-2, I-5, I-6; N-1 to N-6 | grants and identities, as far as a root |
| verify | I-3, I-7; A-7; R-4 to R-4b; S-11 | |
| merge | M-1 to M-8; G-1a, G-1b; E-4 | |
| sync | S-1 to S-11; D-4, D-6 | S-3 and S-8 are where ENV-2 bites first |
| withdraw | W-1 to W-11, W-13 | **including `erase`**: MISSION-1 requires forgetting, and W-1 has three tiers, not one |
| grep, regex, bm25 | Q-1 to Q-7, without vector; E-2 | the regex dialect is still open (Not yet answered 7) |
| (on an ESP32) | ENV-1, ENV-2, ENV-3 | |
| (the process) | FIN-1 to FIN-7; C-8 | |

**In: added by Warren** (2026-11-09)

| Part | Requirements | Notes |
| --- | --- | --- |
| references and the cache | X-1 to X-8; W-12, W-14 | serves AS-1 (data ingress) |
| relations | L-1 to L-5 | no relation outranks another (L-4), so nothing needs a grant to judge |
| a robot's output is untrusted | V-5 | serves AS-1 |
| password-wrapped keys | N-8 to N-17, with their lettered parts | a person on a phone or a chip needs a way in. The largest single part of v2 |
| one engine: SQLite | G-1a, G-1b (in the merge row) | Warren: SQLite only for 1.0. Other engines are viki 2 |

**In, because the mission or the shape cites them.** FIN-5a says 1.0 may cite no unmoved v2 ID, so
anything the shape cites has to be in: V-1 and V-1a (SHAPE-4), V-4a (SHAPE-7), M-5, W-6, A-4a, C-1,
D-1, D-3 and I-7. All but V-1, V-1a and V-4a are already in a row above, so the scope includes
**V-1, V-1a, V-4a and V-5**, and no other V requirement.

**Out of 1.0**

- **vector search**: optional under Q-6, and kept off the ESP32 by ENV-1. It can be built in 1.0,
  but no gate requires it.
- **v2's "Recursion" note**, which v2 itself marks as not built.
- **G-2 to G-6, engines other than SQLite** (Warren, 2026-11-09): viki 2.
- **V-2 to V-4 and V-6, publication** (Warren, 2026-11-09): deferred. Since 1.x adds no new meaning
  (FIN-1), deferred past 1.0 means viki 2.

## L — Relations between assertions (new series)

From the stripped-structure trial (`test/stripped/`): a librarian that says which notes correct, copy
or contradict each other is useful when its claims are checkable.

- **L-1** *(since v3)* A **relation** is an assertion of kind `relation` about two assertions:
  `corrects` (directed), `same`, `contradicts`, or `none`. It is **never a supersession**: no
  relation changes `current()` of either assertion it names. Only an author with `s` supersedes.
- **L-2** *(since v3)* A relation's akey is its **author and the sorted pair** of the two ids. So
  each author revises only their own relation on a pair, by superseding it, and two authors who
  disagree hold two standing relations side by side. That is information, not a fork to heal.
- **L-3** *(since v3)* A relation names both assertions in `derived_from` (V-1) and carries its
  evidence as quotes, each a byte range into the assertion it cites. A reader re-checks every range;
  one that no longer matches makes the relation stale, and one into erased content flags it (W-13).
- **L-4** *(since v3)* **No relation outranks another.** Every relation, a person's included, is a
  claim with an author, a time and **grounds**: the assertions it rests on, named in `derived_from`
  (V-1). Grounds may include other relations, so a claim can rest on claims. Whoever reads decides
  what to believe (SHAPE-7). A preference such as "Warren's over the librarian's" is a reader's
  filter by author, outside viki. *Replaces a draft "verdict" that outranked claims: Warren,
  2026-11-09, "vedicts sound like k3 in a moving world. what if a verdicts roots are based on
  claims dertemined to be false? or falser? the structure should avoid rigidness if it can
  represent reasoning about mushrooms."*
- **L-4a** *(since v3)* **Grounds that moved are reported.** A ground has moved when it is
  superseded, withdrawn or erased, or when a standing `corrects` or `contradicts` relation targets
  it. Every relation resting on it is reported as having moved grounds, with which ground and why,
  and is never hidden or deleted for it. This widens L-3 from quotes to all grounds.
- **L-4b** *(since v3)* **How far down is stated.** A read checks grounds to a stated depth, one hop
  unless the caller asks for more, and its coverage says how deep it looked (Q-2), so that checking
  stays within ENV-2's bound.
- **L-4c** *(since v3)* **"Falser" is data, not arithmetic.** A relation may carry its author's
  stated confidence. viki stores and shows it and never combines, compares or ranks by it.
- **L-5** *(since v3)* The vocabulary is stated and closed (D-3b): the four kinds above. An agent
  may write relations without reading anything else in the diary only if granted so (D-3a).

---

## Chosen falsehoods (K4)

| Falsehood | Load-bearing in | Blast radius |
| --- | --- | --- |
| Erased means physically gone | W-3, ENV-1 | On flash, wear levelling can leave erased pages readable to someone with the chip. "Erased" means unreadable through the database; re-keying the diary is what makes old pages useless |
| A peer's clock is roughly right | C-6, C-3a | A device with no RTC writes plausible wrong times, signed. They are claims (C-7), and are shown as such |

## Not yet answered

1. **Moved to design: the embedding model** (Q-6). Precedent, negotiable: `src/` pins
   `all-MiniLM-L6-v2` (qint8 ONNX, ~25 MB) under D-11, run by ONNX Runtime; `core/` takes the model
   in as ordinary blobs and names it by its bytes. The runtime is a dependency, and the design says
   how it is pinned and shipped.
2. **Guessable content.** The id of an erased "yes" or a date can be recovered by hashing guesses.
   A per-domain salted id hides it but breaks A-1 across domains. Undecided.
3. **What source control means.** Proposed: content history and merge only (AS-3). Confirm, or say
   what of Fossil's working-tree model belongs inside.
4. **Engines for 1.0.** *Answered* (Warren, 2026-11-09): SQLite only. G-2 to G-6 and other
   engines are viki 2.
5. *Withdrawn* (2026-11-09): who may give a verdict. L-4 no longer has verdicts.
6. **The 1.0 scope list** (FIN-4). Drafted from ENV-1 under "1.0 scope"; open until Warren closes it.
7. **The regex dialect and engine** (Q-6): POSIX ERE, as `src/`'s `viki grep` used, or another. On
   ENV-1, an engine has to come from somewhere: the platform's libc, if it has one, or a small
   vendored engine, which is a dependency.
8. **v2's open questions 2 to 6** become questions here only if the part they belong to moves.
9. **What moves first.** Proposed: A (identity and framing) and R-1 to R-5 (resolution), with
   their tests from `reboot/tests/`, because every other series stands on them.
10. **The ESP32 variant** (ENV-1): in 1.0, decided; which variant and how much PSRAM, not yet.
