# VIKIVERSE v1 — requirements

**Status: DRAFT for Warren's correction.** Nothing here is settled. Where this
touches a decision already made (`USER_STORIES.md` D-1..D-12) it says so rather
than quietly contradicting it.

Domain: `vikiverse.net`. Scope for v1: **tribes** — a tribe is one encrypted
Fossil repo plus the accounts that may reach it. Federation between tribes is
out.

---

## 0. The goal, in one sentence

**A memory that spans projects, survives being offline, and can be read by a
person or an agent through the same door** — so that neither has to remember
where they wrote something down, and neither re-solves a problem the other
already solved.

Everything below is downstream of that sentence. If a requirement does not
serve it, it belongs in v2.

---

## 1. Why v1 is "> fossil"

Fossil stays the transport and the truth store — that is D-4, D-6, D-9 and it
is not being reopened. What "greater than Fossil" means concretely:

- **Fossil is the substrate, the API is the product.** A consumer should never
  need to know Fossil exists. It asks, captures and cites over HTTP.
- **Fossil accounts remain the authorization layer** (ARCHITECTURE.md). v1 adds
  no second identity system for *access*; `identity.db` handles keys at rest,
  which is a different problem (QUEUE 48/49).
- **The corpus spans repos.** This is the actual v1 delta. Fossil is per-repo by
  nature; a memory that is per-repo is the failure mode described in §2.1.

---

## 2. Requirements

Each is written so it can be falsified. `MUST` blocks v1; `SHOULD` is v1 if
cheap, v1.1 otherwise.

### 2.1 Cross-project recall — MUST

The corpus spans every repo the owner registers, and an answer says which one
it came from.

> *Why this is #1:* on 2026-08-23 an agent proposed building SQLCipher-for-wasm
> that already existed one directory over, and its build recipe was sitting in a
> CI file in the same tree. Measured afterwards: 80 files from three sibling
> projects, one index, and the missed prior art returns at **rank 1**. Ninety
> seconds of work against hours lost. That is QUEUE 47, and it is the single
> highest-value thing in this document. Coverage dominates ranking: the literal
> leg bought recall@1 0.302→0.372, while cross-project indexing turned a **0%
> into a rank-1 hit**, because the document was not ranked badly — it was not in
> the corpus.

*Acceptance:* a question whose answer lives in project B, asked while working in
project A, returns it in the top 5 with project B named on the hit.

**Status:** built and proven for the edge (`build/verse-probe.sh`, 6/0). Missing
on the **native CLI**, which is where most work actually happens.

### 2.2 Capture that cannot be lost — MUST

One gesture, no filing decisions, works with zero connectivity, and is never
silently dropped.

*Acceptance:* pocket-to-logged under 15 s with no signal (US-3); the capture is
visible in search on the same device *immediately*, marked as not-yet-indexed if
that is what it is; it reaches the tribe on the next successful sync without the
user remembering to do anything.

**Status:** the PWA does this locally. **The join is missing** — captures made on
a read-only edge never reach a tribe. That is QUEUE 36 and it is the largest
hole in the killer story.

### 2.3 Provenance queries — MUST

"When did this claim first appear", "what landed together", "what changed since
I last looked".

> *Why this is #2 and not a nice-to-have:* the highest-value defects found in
> this codebase were found by `git log -S`, not by similarity search. One claim
> in `test/retrieval-corpus.sh` was **false on arrival** — `git log -S` resolves
> the claim and its own refutation to the same commit — and no vector search can
> find that, because there is no revision where the claim was true. viki indexes
> check-in *text* today but cannot answer a single one of these questions, and
> they are the questions an agent asks most.

*Acceptance:* `viki when "<claim text>"` names the check-in that introduced it;
`viki since <marker>` lists what changed since. Both must work on an encrypted
repo without a full re-index.

**Status:** not built. The data is in Fossil's `mlink`/`event` tables, already
open. This is a join, not a model.

### 2.4 Honest failure — MUST

No surface may fail silently or indistinguishably.

*Acceptance:* "the cache never arrived", "sync is a week stale", "I never wrote
that down" and "the model is missing" produce four different messages. A stale
cache says how stale.

> This keeps being violated by *me*, not in theory: `viki cache push` exited 0
> having published nothing; the PWA hung on "loading…" forever; `(no matches)`
> covers four distinct causes. It is a requirement because it is a recurring
> defect class, not because it is a virtue.

**Status:** partially. QUEUE 36 gap 3 is open.

### 2.5 Encrypted at rest, per tribe — MUST

Every cache and every repo is encrypted. A device holding two tribes cannot open
the second by virtue of holding the first.

*Acceptance:* `build/keywrap-probe.sh` (14/0) and `build/verse-probe.sh`'s V3
already assert this. It stays a MUST because it must not regress.

**Status:** done — SQLCipher end to end, age-wrapped tribe keys, per-identity
passphrases.

### 2.6 One API, several consumers — MUST

`ask`, `capture`, `cite`, `since` over HTTP with a stable contract, reachable by
a person, by this agent, or by openclaw/nanoclaw/MCP-shaped tools, with no
knowledge of Fossil or SQLite required.

*Acceptance:* a third-party tool that has never seen this codebase can ask a
question and get citable results from a written contract alone.

**Status:** `/api/ask` exists and is loopback-only with no auth. **Auth and a
frozen contract are the v1 work**, and they should be designed deliberately
rather than accreted.

### 2.7 Agent memory with attribution — SHOULD

Agents may write findings; every written claim carries who wrote it, when, and
whether it was verified.

> *Why attribution is load-bearing:* on 2026-08-23 a sweep produced 103 candidate
> findings; 12 survived adjudication and 5 were kept. **Unattributed, the other
> 91 would have been indistinguishable from the 5.** An agent memory without
> "who claimed this and was it checked" fills with confident guesses and becomes
> worse than no memory.

*Acceptance:* every agent-written note has an author identity and a verified
flag; `ask` can exclude unverified claims; a retracted claim supersedes rather
than deletes (M-2/M-3, `--closes` already ships).

**Status:** the note model exists; identity and verification do not.

### 2.8 Sync when it can — SHOULD

Opportunistic, partial-progress-tolerant, cheap when nothing changed.

*Acceptance:* a repeat pull with no change costs one 304; one unreachable tribe
does not prevent the reachable ones from updating; the device can always say
when it last succeeded.

**Status:** built for pull (`tribe pull --all`, etag/304). Bidirectional sync
arrives when the edge is a real Fossil peer, which is post-v1.

---

## 3. Explicitly OUT of v1

Named so they do not creep in:

- **Federation between tribes.** Warren: "lets stick to tribes for now".
- **Any LLM inside viki.** viki returns passages; the caller reasons. This is not
  a resource constraint, it is the design (D-10 in spirit).
- **Images as searchable content.** Descriptions are indexed as text — one model,
  one epoch, D-11 unchanged. Image *embedding* is a second model and a second
  epoch; out.
- **Calendar, voice, big binaries.** D-2, D-3, D-7 are settled for later.
- **Revocation.** Removing a member is a re-key of the tribe. v1 must SAY this
  rather than imply a delete button (QUEUE 48).
- **ANN / vector index.** A speed fix for a scale not yet reached; a better model
  is the quality fix. Do not conflate (QUEUE 45).

---

## 4. The tension worth naming: D-5 says Flutter

`D-5` settles mobile as **Flutter over `dart:ffi`**. v1 as drafted leads with an
installable **PWA**, and that is a real divergence, not an oversight:

- viki's UI is a text box, a citation list and a compose field. Flutter means
  cross-compiling SQLCipher, LibreSSL and ONNX for five platforms to render a
  list.
- The wasm edge already exists and reproduces the native binary's ranking and
  rrf scores **exactly**.
- One artifact covers all six targets with no store, no signing, no matrix.

**Flutter becomes right at a specific line**, not a vague later: when the edge
hosts a real Fossil repo. That needs sockets and a filesystem a browser will
never have, and it is also when the OS keystore (`identity.db`'s empty
`device_secret` slot) and non-evictable storage arrive. All three land together.

**Proposed amendment to D-5, for Warren's ruling:** *PWA is the v1 UI on all six
targets; Flutter over `dart:ffi` is how a device becomes a real Fossil peer, and
that is v2.* D-5 is not wrong — it is aimed at the peer, and v1 is not building
a peer.

The iOS caveats are real and are designed around, not waved at: no share target
(an iOS Shortcut posts to the same `?capture=` route), no install prompt, and
~7-day eviction of script-writable storage unless the PWA is installed to the
home screen — which is why install is presented there as *what makes offline
durable*, not as a nicety.

---

## 5. Priority order — for Warren's correction

Ordered by *value per unit of work*, not by dependency:

1. **§2.1 cross-project on the native CLI.** Highest value, mostly built. This
   is the one that pays for itself immediately.
2. **§2.3 provenance queries.** No model, no schema epoch; a join over tables
   Fossil already keeps. Answers the questions agents actually ask.
3. **§2.2 close the capture loop.** The killer story is broken at the last hop.
4. **§2.6 API contract + auth.** Gates all interop; design deliberately.
5. **§2.4 honest failure.** Ongoing; cheapest when done alongside each of the
   above.
6. **§2.7 attribution.** Needed before agents write at volume, not before they
   write at all.

---

## 6. Questions only Warren can answer

1. **Is `vikiverse.net` a hub, a directory, or a download?** It changes the trust
   model completely: one hub Warren runs, a place to find tribes, or just where
   the PWA is served from.
2. **Who else is in a tribe in v1** — only Warren's devices and agents, or other
   people? Multi-person makes attribution and revocation urgent rather than
   SHOULD.
3. **Does the API authenticate as Fossil users, or does it get its own tokens?**
   Reusing Fossil accounts is less machinery and keeps one authorization story;
   tokens are more ergonomic for third-party tools.
4. **Amend D-5?** See §4.
5. **What is the smallest thing that would make you use this daily?** Everything
   above is inference from watching the work; that answer would reorder §5.
