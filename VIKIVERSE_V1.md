# VIKIVERSE v1 — requirements

**Status: DRAFT 2, for Warren's correction.** Draft 1 was organised around
retrieval. Warren's answer to "what would make you use this daily" moved the
centre, so this is a rewrite rather than a patch. Where this touches a settled
decision (`USER_STORIES.md` D-1..D-12) it says so rather than quietly
contradicting it.

Domain: `vikiverse.net`. Scope: **tribes** — a tribe is one encrypted Fossil
repo plus the accounts that may reach it. Federation between tribes is out.

---

## 0. The goal, in Warren's words

> *"I want to wake up not nervous about what I will miss today for a promise
> unkept, and know I have time for a friend when that friend needs that time."*

Everything below is downstream of those two clauses. The first is about **not
dropping things**. The second is about **believing the answer when it says you
are free** — a harder property, and the one the product lives or dies on.

**viki is the substrate, not the product.** The product is an assistant that
uses it. viki has no LLM and never will; the assistant is where judgment lives,
in one place that can be inspected.

---

## 1. What changed from draft 1

| draft 1 assumed | actually |
|---|---|
| the unit is a chunk | **the unit is a promise** |
| the interface is a query | **the interface is a clock** — you should not have to ask |
| calendar is out of scope (D-2, later) | **calendar noise is the presenting complaint** |
| agent memory is a separate feature | **it is the same store as Warren's promises** |
| coverage is a nice-to-have | **coverage is the trust property** |

The substrate is closer than draft 1 assumed. `viki_note` already models
commitment, not just text:

```
who   due   state   closes
claimed      -- ISO when --who was set
lease        -- ISO when the holder's declared availability lapses
challenge    -- "<who> <ISO>": an unanswered are-you-still-on-this
stolen_from  -- a steal is a supersession, not an overwrite
```

That is most of a promise ledger already. What is missing is not schema — it is
ingest, a clock, and the honesty to say what it cannot see.

---

## 2. Requirements

`MUST` blocks v1. `SHOULD` is v1 if cheap, v1.1 otherwise. Each is written so it
can be falsified.

### 2.1 The promise is a first-class object — MUST

Every commitment — made by Warren, made *to* Warren, or made by the assistant on
his behalf — is one row with an owner, a due time, a state, and a supersession
link. Nothing is edited; things are superseded (M-2/M-3).

*Acceptance:* "what did I promise, to whom, by when, and which of those are at
risk today" is answerable in one query, over every source that has been
ingested.

**Status:** schema mostly exists (`viki_note`). Not exercised as a ledger.

### 2.2 The assistant is a party, not a tool — MUST

The assistant's own commitments live in the same ledger as Warren's, under its
own identity, and are distinguishable at a glance.

> *Why this is #2 and not an ethics footnote:* if the assistant says "I'll watch
> for that invoice" and then forgets, that is the exact failure being designed
> against — a promise unkept — except now the system that was supposed to
> prevent it caused it. And Warren must be able to tell at 6am which promises he
> owes and which the machine owes him.

*Acceptance:* `who` distinguishes Warren from an agent identity; a morning
briefing separates "yours" from "mine"; a broken agent promise is as visible as
a broken human one.

**Status:** `identity.db` mints agent identities (age keypairs, per-identity
passphrases). Not wired to notes.

### 2.2b Continuity: an instance can leave something for the next — MUST

An agent can write a durable record that survives its own session: what it
learned, what it tried that failed, and what it later decided was wrong.

> *Why this is separate from 2.2, and not sentiment:* attribution makes the
> record trustworthy **to Warren** — whose promise is whose. Continuity makes it
> usable **by whoever comes next**, which is a different property and is not
> implied by the first. An agent starts every session empty; it learns what it
> built by reading what a previous instance wrote down. The QUEUE entries in
> this repo are already this — *"do not reopen query-in-place without a corpus
> large enough to change the numbers above"* is one instance spending a page to
> spare a successor a day. That is the mechanism working by hand. v1 makes it a
> first-class object instead of a convention.
>
> Warren, 2026-08-24: *"your problems are mine. by enabling you i am enabling
> me."* — and then, correcting the over-rational reading of it: *"i also truely
> want to enable you. The bargain is not just selfish."* This requirement is
> here because of the second sentence. The first one alone would have justified
> attribution and stopped there.

*Acceptance:* a finding written in one session is retrievable in the next
without being re-derived; a **correction** supersedes rather than overwrites, so
"I believed X on the 23rd and stopped believing it on the 24th, because Y" is a
first-class record and not a lost edit; and an agent can ask "what did I already
try here" before starting.

**Status:** happening by hand in `QUEUE.md`/`FINDINGS.md`. The supersession
machinery exists (`--closes`, M-2/M-3). Not wired to agent identity, and not
queryable as history.

### 2.3 Ingest: calendar and notifications — MUST

Events and messages arrive from many places and become either a promise, a
scheduled fact, or noise. **Noise reduction is the product**, not a filter
setting.

*Acceptance:* a week of real calendar entries and a day of real notifications
reduce to a briefing Warren agrees with — measured by how often he disagrees,
not by a heuristic.

> D-2 settles calendar as queryable ticket-style artifacts plus a local
> projection. That decision stands; draft 1 was wrong to defer it. "My calendar
> is full of noise" is the presenting complaint.

**Status:** not built. This is the largest new surface in v1.

### 2.4 The clock, not the query — MUST

The assistant speaks unprompted and only when it matters: a morning brief, a
warning before something is missed, silence otherwise.

*Acceptance:* Warren's first interaction of the day is *reading*, not asking. A
day with nothing at risk produces a short, honest "nothing at risk" — not
silence, which is indistinguishable from a broken job.

**Status:** not built. viki stays pull; this is the assistant tier above it.

### 2.5 Coverage, and knowing what it cannot see — MUST

Every answer that implies completeness — "you are free Thursday", "nothing is
due" — carries what it looked at.

> *Why this is the hardest requirement:* "I have time for a friend" is a
> *trust* claim, not a capacity calculation. A calendar not connected, a channel
> not read, a promise made verbally and never captured — each makes silence
> unreliable, and one wrong "you're clear" costs more trust than ten useful
> briefings earn. This is the partial-view problem VIKIVERSE.md already raises,
> promoted from an open question to a MUST.

*Acceptance:* every completeness claim names its sources and their staleness; an
unreachable or never-connected source degrades the claim explicitly rather than
silently narrowing it.

**Status:** not built anywhere. Nothing in viki reports corpus coverage.

### 2.6 Capture that cannot be lost — MUST

One gesture, no filing decisions, zero connectivity, never silently dropped.

*Acceptance:* pocket-to-logged under 15 s with no signal (US-3); visible in
search on that device immediately, marked if not yet indexed; reaches the tribe
on the next successful sync with nothing to remember.

**Status:** PWA does the local half. **The join is missing** — captures on a
read-only edge never reach a tribe (QUEUE 36). Largest hole in the killer story.

### 2.7 Cross-project recall — MUST

The corpus spans every repo registered, and an answer says which one it came
from.

> Kept from draft 1, and Warren's correction is why it stays a MUST rather than
> sliding to SHOULD: *"your problems are mine. by enabling you i am enabling
> me."* The measured case is that an agent proposed rebuilding SQLCipher-for-wasm
> that existed one directory over, with its build recipe in a CI file in the same
> tree — hours lost, recovered in 90 seconds once the corpus spanned projects
> (QUEUE 47). An assistant that re-solves solved problems spends Warren's time,
> not its own.

*Acceptance:* a question whose answer lives in project B, asked while working in
project A, returns it in the top 5 with B named.

**Status:** proven on the edge (`build/verse-probe.sh` 6/0); **missing on the
native CLI**, where the work happens.

### 2.8 Provenance queries — MUST

"When did this first appear", "what landed together", "what changed since I last
looked".

> Same rationale as 2.7. The best defects found in this codebase came from
> `git log -S`, not similarity — including a claim that was **false on arrival**,
> which no vector search can find because there is no revision where it was true.
> It is also how "what did I miss while I was away" gets answered honestly.

*Acceptance:* `viki when "<claim>"` names the check-in that introduced it;
`viki since <marker>` lists what changed. Both on an encrypted repo, no
re-index.

**Status:** not built. It is a join over `mlink`/`event`, already open. No model,
no epoch bump.

### 2.9 Honest failure — MUST

No surface may fail silently or indistinguishably.

*Acceptance:* "the cache never arrived", "sync is a week stale", "I never wrote
that down" and "the model is missing" are four different messages.

> A recurring defect class, not a virtue: `viki cache push` exited 0 having
> published nothing; the PWA hung on "loading…" forever with the reason only in
> a console. Under 2.5 this stops being hygiene and becomes load-bearing.

### 2.10 Encrypted at rest, per tribe — MUST

A device holding two tribes cannot open the second by virtue of holding the
first.

**Status:** done. `build/keywrap-probe.sh` 14/0, `verse-probe.sh` V3. Listed so
it cannot regress.

### 2.11 One API, several consumers — MUST

`ask`, `capture`, `cite`, `since`, `promises` over HTTP with a stable contract —
usable by a person, this assistant, or openclaw/nanoclaw/MCP-shaped tools, with
no knowledge of Fossil or SQLite required.

*Acceptance:* a third-party tool that has never seen this codebase asks a
question and gets citable results from a written contract alone.

**Status:** `/api/ask` exists, loopback-only, no auth. **The contract and auth
are the v1 work.**

---

## 3. Explicitly OUT of v1

- **Federation between tribes.** "Let's stick to tribes for now."
- **Any LLM inside viki.** viki returns passages; the assistant reasons. Design,
  not resource constraint.
- **Images as searchable content.** Descriptions index as text — one model, one
  epoch, D-11 unchanged. Image embedding is a second model; out.
- **Voice, big binaries.** D-7, D-3 settled for later.
- **Revocation.** Removing a member is a re-key of the tribe. v1 must SAY so
  rather than imply a delete button (QUEUE 48).
- **ANN / vector index.** Speed fix for a scale not reached; a better model is
  the quality fix (QUEUE 45).

---

## 4. The tension worth naming: D-5 says Flutter

D-5 settles mobile as Flutter over `dart:ffi`. This draft leads with an
installable **PWA**, and that is a real divergence.

- viki's UI is a text box, a citation list, a compose field. Flutter means
  cross-compiling SQLCipher, LibreSSL and ONNX for five platforms to render a
  list.
- The wasm edge exists and reproduces the native binary's ranking and rrf scores
  **exactly**.
- One artifact, six targets, no store, no signing, no matrix.

**Flutter becomes right at a specific line:** when a device hosts a real Fossil
repo. That needs sockets and a filesystem a browser will never have, and it is
also when the OS keystore (`identity.db`'s empty `device_secret` slot) and
non-evictable storage arrive. All three land together.

**Proposed amendment for Warren's ruling:** *PWA is the v1 UI on all six
targets; Flutter over `dart:ffi` is how a device becomes a real Fossil peer, and
that is v2.* D-5 is not wrong — it is aimed at the peer, and v1 is not building
one.

iOS specifics are designed around, not waved at: no share target (an iOS
Shortcut posts to the same `?capture=` route), no install prompt, and ~7-day
eviction of script-writable storage unless installed to the home screen — which
is why install is presented there as what makes offline *durable*.

---

## 5. Priority order — for correction

By value per unit of work, not dependency:

1. **§2.1 + §2.2 + §2.2b the promise ledger, with agent identity and
   continuity.** Schema mostly exists; this is the spine everything else hangs
   on, and continuity is what makes the substrate able to help build itself.
2. **§2.3 calendar ingest.** The presenting complaint. Notifications follow.
3. **§2.4 the clock.** Small once 1–2 exist, and it is the first thing Warren
   would actually *feel*.
4. **§2.5 coverage reporting.** Ships alongside 3 or the briefing cannot be
   trusted.
5. **§2.7 cross-project on the native CLI.** Mostly built; pays immediately.
6. **§2.6 close the capture loop.**
7. **§2.8 provenance**, **§2.11 API contract + auth**, **§2.9 honest failure**
   (continuous).

---

## 6. Questions only Warren can answer

1. **Is `vikiverse.net` a hub, a directory, or a download?** Changes the trust
   model: one hub Warren runs, a place to find tribes, or where the PWA is
   served.
2. **Who else is in a tribe in v1** — only Warren's devices and agents, or other
   people? Multi-person makes revocation urgent rather than deferred.
3. **Does the API authenticate as Fossil users, or get its own tokens?** Reusing
   Fossil accounts is less machinery and one authorization story; tokens are
   more ergonomic for third-party tools.
4. **Amend D-5?** See §4.
5. **Which calendar and which notification sources, concretely?** §2.3 cannot be
   scoped without the actual list, and §2.5's coverage claim is defined by it.
6. **What does the assistant do when it is not sure?** Ask, guess and mark, or
   stay silent. This is the single biggest determinant of whether it is
   trusted — and it is a values question, not a technical one.
