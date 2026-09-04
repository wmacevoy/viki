# REQUIREMENTS v2 — derived from use cases

v1 is kept, not edited. It was written from theory; this is written from two stated use cases, and
the diff between them is the useful artifact. Where v2 deletes something, the deletion and its reason
are recorded at the bottom rather than silently dropped.

Findings that forced changes are cited as `C-n` / `F-n` from `FINDINGS.md`.

---

## The use cases

**U-1 Personal assistant.** A viki diary of Gmail and a viki diary of Outlook, each populated by a
robot. Both hold a great deal that must never sync anywhere. A digital assistant agent reads both and
builds a summary and a calendar. That summary and calendar sync to an offline copy on a phone.

**U-2 Project management.** A viki diary per project. A digital project manager reads them and builds
a summary across all projects. That summary syncs to a cloud PM which orchestrates work.

**What both have in common, and it is the whole architecture:** the sensitive store never moves. What
moves is smaller, derived, and deliberately authored.

**U-3 The assistant diary syncs.** Phone and laptop, and it holds sensitive content. So a diary that
syncs is not thereby a diary that is safe to publish — it is a diary replicated inside one trust
domain.

**U-4 Sources are gigantic and impermanent.** Mail and documents are too large to carry and may be
deleted, revoked, or edited at the source. A diary therefore holds **references plus a bounded
cache**, not the content.

---

## Glossary

Fifteen words, because v1's ten were policing a design with fifteen (C-9).

| Term | Means |
| --- | --- |
| **assertion** | One immutable statement. Truth. |
| **id** | `sha256` of an assertion's framed content. |
| **akey** | What assertions compete on. *Never called "key" alone* — that word had five meanings in v1. |
| **rank** | A sortable string. Highest wins among survivors. **Inside the frame** (C-2). |
| **supersede** | To write an assertion that retires an earlier one. The only mutation verb. |
| **diary** | One store, one sync policy, one trust domain. **The unit of access control.** |
| **domain** | The set of principals holding a diary's key. Everyone in it reads everything in it. |
| **reference** | A stable name for content held elsewhere: system, id, and version. |
| **cache** | A local, bounded, evictable copy of referenced content. Never syncs. |
| **projection** | Derived, rebuildable, never merged: index, vectors, and the cache. |
| **derivation** | The one-way relation from source assertions to an assertion computed from them. |
| **publication** | Writing into a diary whose sync policy is wider than the source's. The boundary. |
| **tombstone** | A record that withdraws an assertion or a reference. Its own table, its own frame. |
| **principal** | Who is asking. A public key. Humans and agents alike. |
| **robot** | An unattended producer that writes a source diary from an external system. |

Words deliberately not reused: *delete* (things are withdrawn at a tier), *write* (assertions are
added), *key* unqualified, *rank* for retrieval relevance.

---

## D — Diary and sync scope

**This is the primary security mechanism.** v1 tried to make per-assertion permissions the boundary;
both reviews showed it cannot be one, and the use cases do not ask for it.

- **D-1** A diary declares its sync policy: `never`, `domain` (named principals), or `published`
  (a named hub). The declaration is enforced by the code that syncs, not by documentation.
- **D-2** A diary whose policy is `never` has no sync path at all. There is no flag, no override, and
  no function that takes it as a source.
- **D-3** Access is granted **per principal, per diary**: `r` read, `s` supersede, `x` redact. There
  is no intra-diary or per-assertion control. Humans on a shared project diary hold all three (friendly
  access); an agent holds the narrowest set its job needs.
- **D-3c** **`s` is safe to grant precisely because it destroys nothing.** A superseding assertion adds
  an entry; the predecessor stays and the chain shows both. Only `x` removes. So an agent holding `s`
  on a verdict log can write a later verdict but cannot un-write an earlier one — the flag and the
  un-flag are both in the log, and what changes is only which entry the head view returns. See R-9 for
  the obligation that puts on the reader.
- **D-3a** `s` without `r` is the load-bearing case, not an edge one. An agent that may write a status
  and read nothing else is confined by the data model rather than by prompt engineering — and a fully
  compromised such agent still cannot read. Every grant is stated as the capability of an agent that
  has already been taken over.
- **D-3b** A diary an agent may write is a diary with a **stated vocabulary**. Free text is unbounded
  bandwidth; an enum and a score are close to none. The residual covert channel is the entropy of that
  vocabulary times the number of items processed, and it is stated rather than assumed away.
- **D-3e** A diary nobody holds `x` on is **structurally un-erasable**, which is a legitimate design —
  an audit log should be — but it is only safe paired with D-3b. An append-only diary with an unbounded
  vocabulary is a store you are legally unable to clean. The bound and the immutability are one
  decision, not two.
- **D-4** A relay that carries a diary between domain members cannot read it. What a relay may learn
  is stated explicitly (see K4) rather than left to be discovered.
- **D-5** An agent is granted access **per diary**. Its grant is an assertion in a diary the granting
  human controls, so an agent's authority is traceable to something a person signed and revocable by
  superseding it.
- **D-6** A diary reports its own policy, its domain, and its last successful sync per peer. A store
  that cannot say where it sends its contents is not usable.

## X — References and the cache

Everything in this section follows from U-4, and none of it exists in v1.

- **X-1** An assertion may name external content by a **reference**: system, stable id, and a version
  or etag. The reference is inside the frame; the content is not.
- **X-2** A referent has exactly three distinguishable states, and no read path may conflate them:
  **cached** (bytes are here), **away** (the reference is known, the bytes are not here, and they may
  be fetchable), **gone** (deleted at the source, or access revoked).
- **X-3** "Away" and "gone" are never reported as each other. A phone with an empty cache must not
  read as a mailbox that was emptied.
- **X-4** The cache is a projection: bounded by a stated size, evictable, and **never merged**. Each
  device caches independently.
- **X-5** Eviction may be irreversible, and the API says so at the point of eviction. A cache of
  external content is a projection that can become permanently unrebuildable once the source is gone —
  a category v1 did not have, because chunks were always rebuildable from stored text.
- **X-6** Content may be **pinned**, which exempts it from eviction and makes it truth rather than
  cache. Pinning is the deliberate act of deciding to carry something.
- **X-7** A reference carries a version, so a summary can detect that its source changed since it was
  written. Without this a summary silently describes a document that no longer says that.
- **X-8** A diary is usable with an empty cache. Every read path degrades to references and states
  which content it could not show.

## V — Derivation and publication

- **V-1** A derived assertion names the assertions and references it was derived from. The edges are
  queryable in both directions.
- **V-2** Publication — writing into a diary with a wider sync policy than the source — is an explicit
  operation, never a side effect of an ordinary write.
- **V-3** A publication produces a reviewable record of what crossed: which assertions, derived from
  what, into which diary. **This is the security boundary, so it must be visible.**
- **V-4** Publication from a `never` diary into a syncing one is refused unless the operation is named
  as a publication. Nothing crosses by accident.
- **V-4a** **Judging an artifact is out of scope.** Triage — deciding whether a message is safe, what
  it is about, whether it carries an injection — is a connector behind its own API, not a viki verb.
  The isolation that makes it safe (one artifact in, a bounded verdict out, no other reachable state)
  is what also makes it separable, and a layer that can be separated on that argument must be. viki
  stores the artifact, stores the verdict, records the derivation edge, and enforces the grant.
- **V-5** What a robot writes is untrusted. Anyone can send mail, so a source diary is adversarial text
  by construction, and no content in one may be interpreted as an instruction by anything that reads it.
- **V-6** An agent's published output is bounded by V-3's record. The blast radius of a prompt injection
  is what reached a syncing diary, and that is enumerable rather than argued about.

## A — Assertions

- **A-1** Two peers stating the same thing produce the same id.
- **A-2** The id covers author, kind, akey, ts, **rank**, reference, supersedes, and body. Rank is in
  the frame because a host-supplied rank function otherwise produces identical ids with different
  ranks — same set, different winner, undetectable (C-2).
- **A-2a** **Rank is exactly 16 bytes**, refused at write time if it is not. A width, not a
  convention: it makes ordering bytewise on every engine, removes prefix ambiguity, and turns a
  deployment constraint ("configure your collation") into a write-time validation — the same move as
  length-prefixed framing and a refused timestamp.
- **A-2b** The 16 bytes have a **stated encoding**: big-endian milliseconds since epoch (6 bytes),
  then a logical counter (8 bytes), then a kind-defined tiebreak (2 bytes). "Sixteen bytes, your
  problem" would put `FINDINGS.md` C-2's original defect back where it was — two hosts producing
  different ranks for the same logical content. Precision, not width, is what makes ties common
  (T-3), so the millisecond field is the part to revisit if ties appear.
- **A-3** Length-prefixed framing. No field value can forge a boundary.
- **A-4** NFC normalization before hashing.
- **A-4a** **Every value that is hashed, compared or ordered is BYTES — UTF-8 for text, after NFC.
  There are no TEXT columns in the truth tables.** One rule, and it closes four separate leaks at
  once: no collation exists for bytes on either engine; Postgres `text` rejects `\x00` and invalid
  UTF-8 while `bytea` does not; SQLite sorts every BLOB after every TEXT regardless of content, which
  is only dangerous in a *mixed* store and cannot arise if nothing is text; and `substr()` counts
  characters on TEXT and bytes on BLOB, which is a live off-by-multibyte bug in the predecessor
  (`FINDINGS.md` T-2). Human-readable rendering is a projection, not truth.
- **A-5** Storing something already present is a no-op and is reported as one.
- **A-6** No operation changes a stored assertion.
- **A-7** An arriving assertion whose id does not match its content is refused.
- **A-8** **A tombstone has its own framing rule** with a distinct domain tag, and its id is verified
  the same way. v1 gave tombstones their own table and never gave them a frame, which let any
  signature the victim ever produced authorize an erasure (F-2).

## R — Resolution

- **R-1** `current(akey)` returns the single unsuperseded arm of the chain, and raises `Forked` when
  there is more than one. Following successors to the head is the whole of it; **rank is not consulted
  here**, because a chain has one head.
- **R-2** `resolve(akey)` picks deterministically among the arms of a fork — **rank descending, then id
  descending, both compared as bytes (A-4a)** — and reports that it resolved one. The `id` half is not
  decoration: `core/`'s apparent tiebreak today is a query-plan artifact that changes if an index is
  dropped (T-3), so an implicit tiebreak is nondeterminism waiting for a schema change. It exists because a summary on a phone must produce
  an answer without prompting anyone. Two stores holding the same set resolve identically.
  v1 put the tiebreak on `current` and also required a unique arm, which made the tiebreak unreachable
  code and R-1 and R-5 inconsistent (C-7). Separating the two views is what makes both satisfiable.
- **R-3** Resolution is a pure function of the set. Shuffled insertion yields identical results.
- **R-4** A supersession is honored only when signed by a principal permitted to supersede — which,
  under D-3, means a member of the diary's domain, not a per-assertion right.
- **R-5** More than one unsuperseded arm is a **fork**, and it is reported. R-1 and R-5 were
  inconsistent in v1: if any second arm is a fork, rank is never consulted and R-2 is unreachable
  (C-7). R-1 now states its precondition.
- **R-6** One resolution statement for every kind.
- **R-7** A fork is healable by any domain member, because within a domain everyone may supersede.
  v1 made a fork permanent and unhealable, which was a denial-of-service costing one row (F-4).
- **R-8** **A chain of successors carries two views over one mechanism**: the **log** view — every
  entry, in order, nothing hidden — and the **replacement** view — the head alone. A conversation and
  a file's version history are the same edges read two ways, not two data models. This is the
  conceptual-integrity win the design was missing: a reader who learns supersession can now guess how
  a log works, and be right.
- **R-9** **A read whose correctness depends on history must use the log view.** Anyone holding `s` can
  change what the head returns without destroying anything, so a security or audit decision that reads
  only the head is flippable while the tampering sits visible one view away. Which view a consumer
  reads is part of that consumer's contract, not a display preference.

## M — Merge

- **M-1** Union: idempotent, commutative, associative.
- **M-2** Self-merge changes nothing.
- **M-3** A merge that cannot complete reports incomplete.
- **M-4** Signatures merge with assertions.
- **M-5** Projections — index, vectors, **and the cache** — are never merged. Unprojected count is
  reportable after a merge.
- **M-6** Push and pull are one operation with the ends swapped.
- **M-7** A bad row is **quarantined with a reported count**, not aborted. v1 required abort, which made
  one planted row a permanent denial of sync (F-8).
- **M-8** Merge is possible only between diaries of the same identity in the same domain (D-1).

## Engines

**A peer may be SQLite or PostgreSQL.** Phones and laptops run SQLite; a hub or a shared project
store may run Postgres. They are peers, not tiers — they reconcile through the S protocol, which
speaks ids and digests, and neither needs to know the other's engine.

- **G-1** **The wire format is the S protocol, never a database file.** The predecessor's merge binds
  two live handles and its host ships an entire checkpointed SQLite file, so the wire format *is* the
  SQLite file format and no other engine can participate at any layer (`FINDINGS.md` T-12). Sync is
  defined over ids, digests and rows.
- **G-2** Truth is engine-portable by construction: bytes everywhere (A-4a), identity computed in
  application code and never by an engine's SQL functions, and merge expressed as insert-or-ignore
  **with an explicitly named conflict target** — blanket suppression hides constraint failures other
  than the one intended, which has now bitten two independent codebases (T-12).
- **G-3** Projections — index, vectors, cache — are **licensed to differ per engine**. FTS5 here,
  tsvector there, none at all on a phone. M-5 makes this safe by never merging them, and that single
  decision is what makes a heterogeneous fleet survivable. Stated explicitly rather than left implied.
- **G-4** Local tables — `arrival`, `peer`, `verified` — are likewise free to differ, because nothing
  in them travels (C-2).
- **G-5** **Sequence semantics are not portable and must not be load-bearing.** Postgres sequences
  allocate before commit and are not gap-free, so `max(seq)` can name a row that has not landed —
  which is exactly S-6's dangerous direction arriving from the engine rather than from a caller. A
  watermark advances on acknowledged rows, never on a sequence maximum.
- **G-6a** **A Postgres peer holds the database key and encrypts bodies at the application layer**,
  because SQLCipher has no Postgres analogue. Open question 5 answered: Postgres is a **peer**, not a
  relay. The consequence is stated rather than discovered — see the K4 table: **metadata is in the
  clear** on a Postgres peer, because `akey` must be queryable for resolution and `id` for joins.
- **G-6** **The merge sequence is order-dependent and must be atomic against concurrent merges.**
  SQLite's single writer supplies that for free; under MVCC two merges interleave and a sweep can
  judge authority that has not committed. An isolation level or an advisory lock, stated, not assumed.

## S — Synchronization

Anti-entropy: how a peer learns what it lacks and gets it. The M series is the *algebra* of union;
this is the *protocol*. v1 and v2-before-this-section had neither, so every sync was a full scan that
re-hashed every body and re-verified every signature — and `FINDINGS.md` F-8 flagged it: *"the sender
chooses the receiver's CPU cost."*

This section is also what justifies the `arrival` table, which until now was a mechanism whose only
requirement was to clean itself up (C-8).

- **S-1** A peer can learn **what another holds that it lacks, without receiving it.** The question
  and the transfer are separate operations.
- **S-2** **Two peers that have already converged transfer nothing.** This is the property the
  full-scan design could not express at all, and it is the one the phone case hits first.
- **S-3** The exchange that answers S-1 is **bounded** — its size does not grow in proportion to the
  store. Shipping a list of every id to discover that nothing changed is the failure this forbids.
- **S-4** A watermark is **sender-side**: "I have given P everything up to my N." `seq` orders my
  receipts, not their writes, so my watermark is meaningless to a peer and a watermark is kept per
  peer, never as one number.
- **S-5** A watermark is an **optimization over the manifest diff, never a substitute.** The diff
  stays ground truth and is the falsifier: asking what a peer lacks must give the right answer even
  when every watermark is wrong.
- **S-6** **Round a watermark down, never up.** Too low costs bandwidth and merge is idempotent; too
  high silently skips rows and reports success. When the two errors are not symmetric, the API must
  not let a caller pick the dangerous one by accident.
- **S-7** A sync interrupted partway **resumes rather than restarting.** Partial progress is durable,
  and the report carries what is needed to continue.
- **S-8** **The receiver bounds its own cost.** A sender does not choose how much verification,
  storage or bandwidth a receiver spends, and hitting a bound is reported rather than silent —
  otherwise a truncated sync is indistinguishable from a converged one.
- **S-9** **A relay and a peer are different roles, and only one of them can sweep.** A **peer**
  holds the database key, reads, and applies tombstones. A **relay** holds no key, carries ciphertext,
  applies nothing, and **must not claim to** — it cannot judge a tombstone's authority (N-2) because
  it cannot read the identity that signed it.
- **S-9a** A relay therefore **does** re-serve withdrawn content, and that is inherent rather than a
  defect to patch: a relay carrying an opaque blob cannot know what is inside it. The mitigation is
  the container, not the sweep — what a relay carries is superseded wholesale by a later snapshot,
  which is the `owned` recursion noted below. v1's and v2's "the hub must still sweep" was the wrong
  demand, and it was the third leg of the contradiction that made the whole authority triangle
  unsatisfiable (B-4).
- **S-10** Sync obeys diary scope. No S operation reaches a `never` diary (D-2), and a sync between
  different diary identities is refused (M-8).
- **S-11** Rows already verified are not re-verified. Without this, incremental *transfer* still
  costs a full *verification*, and "incremental" is a claim rather than a property.

## W — Withdrawal

- **W-1** Three tiers: `withdraw` (hidden, reversible, with a real restore verb), `seal` (recoverable
  by a custodian), `erase` (destroyed).
- **W-1a** `s` and `x` operations are recorded in an **undo log**, and the displaced value moves to an
  opaque **heap** rather than being destroyed in place. So the default destructive act is recoverable,
  and true destruction is purging the heap — which makes the heap, not the assertion table, the thing
  a retention deadline (W-7) applies to.
- **W-2** Tombstones, **grants, identities and root claims** are immune to withdrawal at every tier
  — the immune class is everything authority is derived from, and N-1/N-3 join it for exactly the
  reason grants did. Note the asymmetry that makes this work: **supersession is safe and erasure is
  not.** "Unsuperseded" is a pure function of the set, so revoking an identity or a root by
  superseding it is order-independent; *erasing* one would make the sweep's outcome depend on sweep
  order, which is C-2's defect. Revocation is supersession; removal is a re-key (open question 7). v1 immunized only
  tombstones, so erasing a grant made the sweep order-dependent — the same set reaching two terminal
  states depending on sweep order (C-2). The immune class is *everything authority is derived from*.
- **W-3** Erasure destroys the assertion, its payload, its cache entries, and its projection.
- **W-4** **Sealed content leaves `assertion` for a separate table.** v1 sealed in place, which
  contradicted A-6 and broke A-7, so one sealed row poisoned every merge that peer sourced (C-3).
- **W-5** A tombstone takes effect only on a verified signature over `(purpose ‖ id)` from a domain
  member. Domain separation is required (F-2).
- **W-6** A tombstone arriving before its target takes effect when the target arrives.
- **W-7** A seal carries an absolute deadline; each peer promotes expired seals on its own clock.
- **W-8** The default is a delayed seal.
- **W-9** A tombstone's propagating reason is a code. Free text stays local.
- **W-10** No local sequence number survives its assertion.
- **W-11** Erasure across peers is best-effort and every surface says so.
- **W-12** A tombstone may target a **reference**, and everything naming that reference is findable.
  This is how erasing a source email reaches the summary derived from it — the content-addressed
  tombstone of v1 could not, because a derivative holds different bytes.
- **W-13** Erasing a source **flags** its derivatives for review rather than destroying them. A summary
  covering ten emails must not vanish because one was deleted; the decision belongs to a person.
- **W-14** A source that becomes `gone` (X-2) is not an erasure. Withdrawal is a decision; unavailability
  is a fact.

## I — Identity

- **I-1** An assertion's author is inside its frame.
- **I-2** Signature state is **per signer**, never one verdict per assertion. v1's single value let any
  peer push a junk signature and move any assertion to `BAD`, discrediting it network-wide (F-8).
- **I-3** Verification requires no secret.
- **I-4** Domain membership comes from signed grants, and grants are immune (W-2).
- **I-5** Author, signer, custodian and principal stay distinct. **An authority check names whose
  authority it is testing** — v1's took only the reading principal, which made whether a hostile
  tombstone bit depend on who opened the store (C-9).
- **I-6** Keys rotate. An assertion binds an identity, and an identity binds a key over a stated
  validity window, so a compromised key is not retroactively and permanently authoritative (F-9).
- **I-7** An unsigned assertion does not resolve. It is stored, readable and reported as a claim, and
  it never becomes current. This answers v1's open question 5, which left author-spoofing a supported
  operation that wins by rank (F-10).

## N — The root of authority

The gap every review converged on: v2 said grants confer rights and never said **who may issue
one**, so the only implementation passing the whole suite was "a relay applies every tombstone it
receives" — a censorship amplifier for the fleet (`FINDINGS.md` B-4, T-1c).

The anchor is the **container**, not the content. That is what makes it unforgeable by anything
merged in: a row can claim anything, but a row cannot let you open a database.

- **N-1** An **identity is an assertion**, kind `identity`, carrying a name, a public key, and
  optionally that identity's secret key encrypted under a password. Not a table of its own: a table
  lives at the database level and would get no merge, no search, no permissions and no supersession.
  As an assertion it gets all four for free, and revoking an identity is superseding it.
- **N-2** An identity row is **authoritative only when signed by a root-authority key.** An unsigned
  or unrootable identity is stored, readable, and confers nothing.
- **N-3** **A root-authority key is one to which the database key is wrapped.** A root claim is an
  assertion, kind `root`, carrying the recipient's public key, the sealed database key, and a
  **proof that any key-holder can verify** — a MAC over the recipient's public key under the
  database key. So the anchor is cryptographic rather than structural: a peer cannot promote itself
  by writing a row, because it cannot produce the proof without the key.
- **N-3a** **The root set therefore travels.** That is the point of it being an assertion rather
  than container metadata: a newly enrolled device learns who the roots are by merging, instead of
  being told out of band. It is also why the proof must be verifiable by *any* key-holder and not
  only by the recipient — nobody else holds the recipient's private key.
- **N-4** A **grant is honored only when** its issuer's identity is authoritative (N-2) **and the
  issuer already holds the right being granted.** Delegation narrows; it never escalates.
- **N-5** Root authorities hold every right. That is what root means, and it is what terminates the
  delegation chain rather than leaving it circular.
- **N-6** A grant that cannot be traced to a root is **stored and inert, and counted** — the same
  shape as an unauthorized tombstone (W-5), and for the same reason: an attempted escalation is
  evidence.
- **N-7** **The database key is random and full-entropy, so it is used raw with no key derivation.**
  Stretching exists to compensate for low-entropy input; there is none here, and the cost is not
  academic — 5.99 ms to open with a raw key versus 345.93 ms with a passphrase, measured.
- **N-8** **A password-encrypted secret key DOES require a slow KDF**, because its input is a
  password. N-7 and N-8 must never share a code path: the single most likely way to get this wrong is
  one "unlock" function that treats both inputs alike.
- **N-17** **Device removal is a re-key.** Unwrapping a recipient does not un-tell them a key they
  already hold, so removal means generating a new database key, re-wrapping it to the remaining
  roots, and re-encrypting. The removed device keeps everything it had and learns nothing after —
  forward-only, like every other revocation here. Shredding a diary and shredding its keys are the
  same operation, which is what makes this tractable at all.
- **N-9** **Anyone holding the database key can add a root**, because producing the proof requires
  only the key — which is the same bound the verification has, and deliberately so. This is stated rather than defended — it is the same
  guardrail-not-boundary line the project already draws, and it means root authority is exactly as
  strong as database-key custody and no stronger.

### The travelling secret

Open question 4 answered. There is no separate pepper value: the hardening is **how many low bits of
the salt are dropped when the row travels.**

    salt           = 32 random bytes, REDRAWN if its low `b` bits are all zero
    key            = kdf(password, salt)
    secret_wrapped = encrypt(secret, key)

The identity assertion carries the salt **with its low `b` bits zeroed**. A device that wants the
secret searches the `2^b` values of those bits, recovers the original salt, and keeps it locally.

- **N-10** **One seal, one key, one field.** The salt is the whole mechanism: full on a device that
  has it, truncated in the row that travels. Two ciphertexts under two keys was an earlier draft and
  bought nothing, because the derived key is the same either way.
- **N-11** **The salt is redrawn if its low `b` bits are all zero.** Without that, one identity in
  `2^b` ships a salt identical to its real one and has no hardening at all — silently, and
  indistinguishably from every other identity.
- **N-12** **The recovered salt is stored locally and never syncs.** It is the answer to the search,
  so a peer that shared it would broadcast the answer and the hardening would evaporate. Each device
  therefore searches once; the creator searches never, because it drew the salt.
- **N-12a** **A device that has searched resets its own row and never searches again.** Enrolment is
  the only time anyone pays. An attacker pays it **on every password guess**, forever — and a `kdf`
  slow enough to impose the same per-guess cost would be paid by the owner on *every* unlock, which
  is exactly why it is not an alternative.
- **N-12b** **The salt is a local column, initialized from the assertion's truncated value and reset
  to the recovered one after a search.** Nothing forks: the assertion's copy stays truncated and
  immutable, so its id never moves, and what changes is local state that never syncs. "Reset the row"
  is an ordinary local write, not an edit to an immutable assertion.
- **N-13** **A time-lock puzzle was considered and rejected**, and the reason is worth keeping
  because it is counter-intuitive. Rivest–Shamir–Wagner makes each attempt *serial*, which sounds
  like it defeats parallel attack and does not: cracking parallelizes across **guesses**, not within
  one, so serial work is a per-guess cost multiplier exactly like this search. The truncated salt
  then wins three ways — **the defender may parallelize its search while the attacker's advantage is
  unchanged**, so for a fixed enrolment wall-clock the attacker's per-guess work is larger by the
  defender's core count; the whole search is `kdf` evaluations, so memory-hardness multiplies through
  all `2^b` of them, where modular squaring is low-memory and ASIC-friendly; and it adds no
  dependency, where the time-lock needs bignum arithmetic.
- **N-14** **The `kdf` must be memory-hard**, and here that is load-bearing rather than hygiene: an
  attacker evaluates it `2^(b-1)` times per password guess, so its resistance to parallel hardware
  multiplies through the entire search.
- **N-15** **There is no structural break.** Truncated entropy is entropy — no factoring assumption,
  no trapdoor, no shortcut for anyone who learns a parameter, and no quantum endgame beyond Grover
  halving the effective bits.
- **N-16** `b` and the `kdf` parameters travel **with** the identity: a peer that cannot tell how many
  bits were dropped cannot search for them, and raising `b` later must not orphan identities sealed
  under the old value.
- **N-16a** **A password verifier travels alongside**, so a wrong password fails in one `kdf` rather
  than after exhausting `2^b`. Without it a typo costs the full search and is indistinguishable from
  a salt not yet found — both terrible to use and a denial of service against yourself.
- **N-16b** The search cost is **probabilistic**, expected `2^(b-1)` and worst case `2^b`, so
  enrolment time varies by up to a factor of two. Stated because a progress indicator that assumes a
  fixed cost will lie.
- **N-16c** **`b` buys `b` bits of password strength**, and that is the honest way to size it. The
  search multiplies an attacker's per-guess cost by `2^(b-1)`, which is arithmetically the same as
  lengthening the password — so the question is never "how much search" but "how many bits am I
  adding, and to what."
- **N-16d** **`b = 11`, from a one-minute enrolment budget.** With a memory-hard `kdf` tuned to
  ~0.5 s per evaluation (the most an everyday unlock should cost) and eight workers,
  `2^(b-1) = 60 × 8 / 0.5 ≈ 960`, so `b ≈ 11`. The parameter moves by one bit per doubling of the
  budget, the core count, or the `kdf`'s speed — and the `kdf` is the one of those three that must
  not be sped up, since its memory-hardness is what caps the attacker's parallelism (N-14).
- **N-16e** **This does not rescue a weak password, and the arithmetic says so.** Eleven bits turns a
  20-bit password from about six core-days of cracking into about sixteen core-years, which is
  decisive; against a 40-bit password both numbers are already beyond reach and the search is
  irrelevant. The construction is worth most exactly where passwords are worst, which is where they
  usually are — but it is a multiplier, not a floor, and nothing here removes the need for a
  password that was not going to fall anyway.

## C — Time

- **C-1** The layer calls no clock.
- **C-2** `ts` (wall clock, shareable, a lie when wrong) and `seq` (local, monotonic) are never
  substituted.
- **C-3** A malformed or non-UTC timestamp is refused, and `ts` is bounded against the receiving
  peer's own clock on arrival (F-4).
- **C-4** Rank is declared per kind, framed (A-2), and exactly 16 bytes in the stated encoding
  (A-2a, A-2b). A kind returning any other width is refused at write time.
- **C-5** Retention deadlines are absolute.

## E — Failure

- **E-1** A write that stores nothing is an error.
- **E-2** A read that is partially blind says so in its result — including *unreadable* and
  *uncached*, which v1's `Result` had no channel for.
- **E-3** Every refusal names its requirement, including the base refusal class, which in v1 carried
  an empty one (C-7).
- **E-4** A merge source of the wrong shape is refused whole.

---

## Deleted from v1

| Deleted | Why |
| --- | --- |
| **The entire P series** (nine mode bits, umask, view filter, authorizer) | No stated requirement needs per-assertion permission. D-3 answers "who may read this" with "which diary is it in." Both reviews showed the filter is not a boundary; the use cases show it is not needed either. It returns, in one predicate function, the day someone states the requirement. |
| **M-7 (relay what you cannot read)** | Written for a network the use cases do not describe. It plus full replication produced the top finding in both reviews. |
| **"Every peer holds everything"** (v1 DESIGN §9) | Filed as an affordability question; it was a confidentiality failure. Diaries are the replication unit and most of them never leave. |
| **`store.query` raw SQL** | Added to prove the filter was structural; it opened onto `DELETE FROM tombstone` (F-1). With no filter to prove, the door has no purpose. |
| **Per-assertion village** | The single class could not survive overlapping groups. Domains replace it, and a principal may be in many. |

## Chosen falsehoods (K4)

| Falsehood | Load-bearing in | Blast radius |
| --- | --- | --- |
| Clocks agree well enough | rank, seal deadlines | Concurrent offline writes resolve by skew |
| Team members are friendly | D-3 | One compromised member device exposes an entire project diary and can supersede or redact anything in it. **Accepted deliberately** — the alternative is intra-diary crypto, which costs retrieval |
| A constrained write vocabulary leaks nothing | D-3b | It leaks at the rate of its own entropy; an agent choosing among four statuses across a thousand messages has a slow but real channel |
| A relay learns only ciphertext and volume | D-4 | It also learns timing and row counts; a hub knows the assistant diary gained 47 rows today |
| A Postgres peer's operator sees only ciphertext | G-6a | **False, and accepted.** Bodies are encrypted at the application layer, but `akey` must be queryable to resolve and `id` to join, so the operator sees ids, timestamps, kinds, keys and the shape of the derivation graph — who corresponds with whom, how often, across how many distinct topics |
| `b` dropped bits is enough search | N-10 | Chosen against today's hardware; too few and the seal is a speed bump, too many and enrolment is unusable. The one tuning number here with no safe default |
| A robot's source is faithful | U-1, V-5 | A compromised connector writes plausible assertions nobody can distinguish |
| The cache can be refilled | X-4 | It cannot once the source is gone; X-5 and X-6 exist because of this |
| `sha256` is collision-free | A-1 | Two assertions become one row |

## Recursion: a diary as an encrypted blob — noted, not built

An assertion's body may be an entire **encrypted diary**. That makes the container recursive — a
diary holds assertions; an assertion holds a diary — and it unifies three things that otherwise need
separate machinery: a relay carrying ciphertext it cannot read (S-9), a payload kept out of the
assertion row, and an archive or backup. All three become ordinary push/pull over an ordinary
assertion, and the relay needs no special mode because there is nothing special about a body it
cannot decrypt.

**Deliberately not built, and one constraint has to be settled first.** Encrypting the same database
twice produces different bytes, so two peers each snapshotting their own copy produce different blobs
with different ids, and union accumulates every snapshot rather than converging. An
encrypted-diary-as-blob is therefore **`owned`** — one declared writer, a stable akey, each snapshot
superseding the last, `current()` giving the newest and the log giving history (R-8's replacement
view). It is not grow-only and it does not merge; two concurrent snapshots are a fork nobody can heal
by merging, because healing would require decrypting both.

Recorded here so the shape is not rediscovered, and kept out of the requirement set until that
ownership rule is decided.

## Open

1. ~~U-2's project diaries: does a team share one?~~ **Answered: yes, under a friendly-access
   assumption, alongside a personal diary.** Resolved by D-3's per-principal grant; per-assertion
   permissions do not come back. What is now open instead: friendly access is a K4, and nothing states
   what breaks when a project member's device is compromised.
2. **Eviction policy.** X-5 says eviction may be irreversible. What is evicted first, who decides the
   bound, and does the user get told before something becomes unrecoverable?
3. **What the cloud PM in U-2 is trusted with.** It receives published summaries, so it is an operator
   adversary with a bounded view — but the bound has never been written down.
4. **Does `secret_wrapped` travel?** N-1 lets an identity carry its own
   password-encrypted secret key. Syncing it is what lets a new device enrol by password alone;
   it is also **offline password-cracking material for anyone who obtains the database key**, which
   makes every member's password the weakest link the moment a laptop is lost. Public keys and
   signatures clearly travel. This column is a separate decision and is currently unmade.
5. **Is a Postgres hub a peer or a relay?** N-3 anchors root authority in the database key, and
   SQLCipher has no Postgres analogue. As a **relay** holding opaque blobs, Postgres works and the
   objection dissolves. As a **peer** it needs the key, which means encrypting `body` at the
   application layer and accepting that metadata — ids, timestamps, akeys, the shape of the graph —
   sits in the clear where the operator can read it. `FINDINGS.md` T-12 asked this and it is still
   the question that decides the G series.
6. **The reference format.** A Gmail message id, a Graph id and a Drive id have different stability
   and different versioning. X-7 assumes an etag exists everywhere; that needs checking per system
   before it is a requirement.
7. ~~How a phone joins a domain, and how a lost one is removed.~~ **Answered.** Enrolment is a
   `root` assertion wrapping the database key to the new device (N-3). **Removal is a re-key, not a
   delete** — see N-17.
