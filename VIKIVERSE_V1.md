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

**Status:** BUILT 2026-08-24. `viki promises [--me NAME] [--horizon 7d] [--all]`.
Live tasks only, ordered by due date, risk marked OVERDUE/TODAY, and it states
its own coverage on every run. `build/promise-probe.sh` 12/0.

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

**Status:** BUILT 2026-08-24 as far as the ledger needs. `--me` names what
counts as yours; every other holder is shown by name, agent or human.
Deliberately NO agent flag in the schema -- an agent identity is just a name,
matching identity.db's names by convention, and a column recording which is
which would be a second source of truth for something the name already says.

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

### 2.2c Authority has degrees, and the ledger records which was used — MUST

Anything done on Warren's behalf is done at a stated level of authority, and the
record says which.

    observe   read only; never writes anywhere
    draft     prepares something for Warren to send
    act       sends/posts/replies as Warren, and signs a record of it
    (silent)  acts without a record — DOES NOT EXIST, deliberately

> *Warren, 2026-08-24:* "I am never present to see a fb post, the transactions
> are necessarily through a digital surrogate, so there is an assignment of
> self-dom. I can assign power of attorney to a human assistant to act on my
> behalf. The next step is to what can I assign that authority to."
>
> **The reframing is right and it changes the design.** The surrogate is not
> something being introduced — a browser already renders Facebook *for* Warren,
> already orders and filters it. He is never present either way. So the question
> was never "delegate or don't", it is *which surrogate, and holding what
> authority*. The API boundary this document treated as a limit is an
> engineering accident of which companies published endpoints, not a moral line.
>
> **Where the power-of-attorney analogy strains, and it is the useful part.**
> PoA works because the agent is a *person*: identifiable, liable, revocable,
> bound by fiduciary duty. Software has none of that, and liability does not
> transfer — it stays entirely with Warren. So this is closer to operating a
> machine than to appointing an attorney.
>
> What substitutes for the accountability personhood supplies is **legibility**:
> an inspectable, signed record of everything done in his name, so the authority
> is auditable even though it is not liable. `identity.db` already mints agent
> identities with real keypairs, so an agent can sign what it did. That is the
> mechanism; this requirement is the policy.

*Acceptance:* the ledger distinguishes three parties, not two — *mine*,
*someone else's*, and **acted as me**; every `act` carries a signed record
naming the agent identity, the authority level and the time; and no code path
exists that acts without producing one.

**Status:** the ledger has two parties (§2.2); authority levels are not built.
`edge/chrome/` is the `observe` rung, BUILT 2026-08-24 — the first and safest,
and the one needing no policy settled first. `build/reader-probe.sh` (9/0)
enforces the claim rather than trusting it: no `.click(`, `.submit(`,
`dispatchEvent`, `execCommand`, `innerHTML =` or `document.write` anywhere; the
only destination is `127.0.0.1` with no setting for it; and no
`tabs`/`scripting`/`cookies`/`history` permission.

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

**Status:** BUILT 2026-08-24 as `assistant/brief.sh`, and deliberately OUTSIDE
viki. Warren: *"isn't this something that a digital assistant agent manages —
not viki itself but an agent using viki to stay on top of things?"* It was
written as a `viki brief` subcommand first, which was the second boundary
crossing in one day. viki now exposes `coverage` as a pure query; every
judgment — what counts as stale, what is worth asking — lives in `assistant/`.
The sharpest test: viki cannot ask questions, so a brief that asks one was
never a viki feature.

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

**Status:** `viki coverage [--json]` BUILT 2026-08-24 — per-source last-seen
times, no thresholds. The brief in `assistant/` turns that into "STALE" and a
bounded SIGN IN list, because a threshold is a judgment about Warren's day
rather than a fact about the corpus.

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

**Status:** BUILT 2026-08-24. `viki ask "<q>" --verse` over a registry at
`$VIKI_VERSE` / `~/.viki/verse.tsv`, written by `build/verse-index.sh`.
Warren's verse: **110 projects, ~139,000 chunks** — 126 owned indexed in full,
24 untouched clones skipped, 8 refused as too big (each needs a `.vikiignore`).

Ownership decides depth, per Warren's rule: an `upstream` remote marks a FORK
and only the files you changed are indexed. That case is not hypothetical —
`wmacevoy/sqlcipher-libressl` looks owned by its remote and is 31,057 chunks of
upstream SQLCipher.

**Five defects found by running it, four of them ranking or honesty bugs:**
`.vikiignore` indexed itself, putting an identical chunk in every project;
merging by rrf across corpora let a 2-chunk project's rank-1 tie a 5,000-chunk
project's, so "where have I written about horses" returned four `.vikiignore`
files; ranking on cosine alone then discarded the literal leg, so a voting
question missed a project holding 61 chunks that say "vote"; the script refused
to refresh its own stale ignore files; and `VIKI_VERSE_MAX=64` truncated a
110-project registry while printing **"64 of 64 project(s) searched"** — silent
truncation reported as full coverage, which is exactly what §2.5 forbids.

Cross-corpus ranking is now **exact-match first, then cosine**: cosine is
comparable across projects because one pinned model means one space (D-11), and
the literal leg's *fact* is comparable even though its 1/df score is not.

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

## 5b. Roadmap

Four phases. The split is **what is blocked on a decision** versus what is not —
because the substrate is essentially done and the remaining risk is choices, not
code.

### Phase 0 — status 2026-08-26

| | work | state |
|---|---|---|
| P0.1 | **Promise ledger** (§2.1, §2.2, §2.2b) | **DONE.** `viki promises` / `viki why`; `build/promise-probe.sh` 24/0. P4 is the assertion that matters: a superseded promise must LEAVE the ledger. |
| P0.2 | **Provenance** (§2.8) — `viki when`, `viki since` | **NOT BUILT.** Still the next unblocked item: a join over `mlink`/`event`, no model, no epoch bump. |
| P0.3 | **Verse hygiene** | **DONE.** All 110 registered projects carry a `.vikiignore`. Measured on this repo when the file was introduced: the corpus went from 9,578 chunks to 779, and 83.4% of what it had been indexing was vendored SQLCipher/LibreSSL source. |
| P0.4 | **Retrieval quality at verse scale** | **OPEN — and still the one that could reorder everything.** |

**Shipped in Phase 0 that the original table did not anticipate**, because
writing `SCOPES.md` and `SYNC.md` turned up work that had no name yet:

- `viki sql` — the RAW surface (SCOPES §1b). Agents could not do a vector query
  at all before it: a stock `sqlite3` on `cache.db` gets `no such function:
  ndvss_cosine_similarity_f`, so `ask` was the only door.
- `viki coverage` — a query with no judgment in it, and `assistant/brief.sh` as
  the L3 consumer that supplies the judgment.
- The literal leg in `viki ask`, which makes `ask ⊇ grep` for exact strings.
- Key custody signing, and `cache pull` verifying the epoch pin (SYNC.md).

**P0.4 measured 2026-08-26, and it reordered things — but not the way this
paragraph predicted.** Corpus fp `6cfd14b5fded16b3`, n=43 indexed-answer
queries: recall@1 0.372, recall@5 0.605, MRR 0.476, against a BM25-only control
of 0.256 / 0.535 / 0.403. So the vector leg is earning its place. The draft
above blamed MiniLM's cosine and pointed at an epoch bump. The failure taxonomy
says the binding constraint is somewhere else:

- **RIGHT DOCUMENT, WRONG CHUNK in 17 of 43 queries** — 12 of them at rank 1.
  Another chunk *of the gold document* outranks the chunk that holds the answer.
  **No model change fixes this.** Fixed 40-line chunks with no overlap split an
  answer away from the vocabulary that would find it.
- **The keyword leg is not selecting.** The OR-of-terms MATCH selects a median
  of **189 of 190 chunks**; 42 of 43 queries match >90% of the corpus. `porter
  unicode61` carries no stopword list, so BM25 is ranking the whole corpus
  rather than a candidate set.
- Worst classes are `vocab-mismatch` (recall@1 0.000, MRR 0.028 on dev) and
  `superseded` (recall@1 0.000, though recall@5 0.800 — it finds them, it just
  ranks the retired version first).

So the ordered work is **chunking (overlap, and boundaries that respect
structure)**, then **keyword-leg selectivity**, and only then the model. That is
cheaper and more testable than an epoch bump, and `test/retrieval-eval.sh`
gates all three.

**And chunking is blocked by a defect the measurement exposed.** `chunk_params`
is in D-11's determinism claim but in neither the cache key nor the skip test,
so two peers that chunk differently silently double-index the same lines —
reproduced end to end through real `cache push`/`pull`. See FINDINGS.md,
*"chunk_params is missing from the cache key"*. **Fix that first**, or the first
chunking change corrupts every cache it syncs with. Recommended fix: fold
chunking into `model_id`, reusing the mixed-epoch coexistence m1's J1–J4
already prove.

### Phase 1 — UNBLOCKED 2026-08-24. Q5 and Q6 answered; see §5c.

| | work | blocked on |
|---|---|---|
| P1.1 | **Google Calendar + O365 ingest** (§2.3) | UNBLOCKED (Q5 answered) — derived, not mirrored |
| P1.2 | **Gmail / Outlook / GitHub ingest** (§2.3) | UNBLOCKED — the four readable channels first |
| P1.2b | **Chrome reader** for Facebook / Discord (§2.3) | BUILT 2026-08-24. Facebook verified against a live logged-in account (30 rows, 28 captured); Discord's message selectors still unverified |
| P1.2c | **Chrome reader: D2L** (§2.3) | BUILT + VERIFIED 2026-08-24 against a live account: Quick Eval, 20 rows captured. Targets grading OWED, not announcements sent |
| P1.2d | **Chrome reader: O365 / Outlook** (§2.3) | BUILT 2026-08-24 (`edge/chrome/sites/outlook.js`), verified against a live mailbox. **Teams is NOT built.** |
| P1.3 | **The clock: morning brief** (§2.4) | **DONE** — `assistant/brief.sh`, and deliberately NOT a viki subcommand: it asks questions, and viki structurally cannot (SCOPES §3). |
| P1.4 | **Coverage reporting** (§2.5) | **DONE** — `viki coverage` reports; the brief decides what "stale" means and prints the bounded SIGN IN list. |
| P1.5 | **Capture as the bridge** (§2.6) | PROMOTED — the only coverage mechanism unreadable channels have |

This phase is where it stops being a search tool. P1.3 is the first thing you
would actually *feel*.

### Phase 2 — needs Q1, Q2, Q3.

| | work | blocked on |
|---|---|---|
| P2.1 | **API contract frozen** (§2.11) | Q3 — Fossil accounts or tokens |
| P2.2 | **Auth beyond loopback** | Q3, and Q2 if other people are in a tribe |
| P2.3 | **vikiverse.net stands up** | Q1 — hub, directory, or download |
| P2.4 | **Close the capture loop** (§2.6) | partly Q1: where a phone's captures go |

### Phase 3 — post-v1, and deliberately so.

Flutter over `dart:ffi` and a device as a real Fossil peer (Q4); federation
between tribes; revocation as a real operation rather than a documented re-key.

### What is already done, so it does not get re-litigated

Substrate: retrieval core with three legs; nine indexed artifact classes;
encryption end to end; age-compatible key custody with `identity.db` and a tribe
registry; the wasm edge, hybrid and encrypted, several tribes at once; an
installable PWA; snapshot pull over HTTPS; and the verse — 110 projects, one
question. Since: the promise ledger, `viki sql`, `viki coverage`, the morning
brief, `.vikiignore`, ed25519 signing and a `cache pull` that verifies the epoch
pin. Tests: m1 90/0/0, plus eleven probes.

**And the two boundary documents, which are the reason the list above stopped
sprawling**: `SCOPES.md` (four levels, and the one-line test — *can viki compute
this without an opinion?*) and `SYNC.md` (what a tribe may carry, and what it
must refuse). Both were written because the same boundary got crossed twice in
one day, and both have since caught work before it landed in the wrong place.

---

## 5c. Q5 and Q6, answered 2026-08-24 — and what they cost

### Q5: the sources

**Calendars:** Office 365 and Google Calendar. Warren: *"office 365 is fairly
polluted so something derived seems better."* That is D-2's design arriving from
the other direction — do not mirror the calendar, derive a cleaned projection
from it. iCloud *"may be the final location for my things"*: noted, not settled,
and a destination question rather than an ingest one.

**Notifications:** Outlook, Gmail, Facebook, Discord, WhatsApp, Signal, texts,
GitHub. And the sentence that explains the whole mess: *"some groups have no
common choice."* The fragmentation is not disorganisation, it is **imposed** —
each group picked its own channel, so consolidation is not available to Warren
and the assistant must work across channels rather than force convergence onto
one.

### THE HARD CONSTRAINT: HALF THOSE CHANNELS CANNOT BE READ BY A MACHINE

This is the most consequential fact in the document and it must not be
discovered during implementation.

| channel | machine-readable? |
|---|---|
| Gmail | yes — real API |
| Outlook / O365 | yes — Microsoft Graph |
| GitHub | yes — real API |
| Google Calendar | yes — real API |
| Discord | only channels a **bot** is in; reading a user's own account is against terms |
| texts / iMessage | on **macOS** only, via the local Messages database with Full Disk Access. Not on iOS |
| WhatsApp | no *sanctioned* API. **Baileys** impersonates a linked device and works — at a real, permanent, unpredictable ban risk (QUEUE 51). Warren's call, not a technical one |
| Signal | **no, by design.** Reading it would mean prying into the local encrypted store, which defeats the reason to use Signal |
| Facebook | **effectively no.** The personal notification surface is not exposed |
| Office 365 / Teams | **web only** (Warren, 2026-08-24) — Graph exists but is not reachable for him |
| D2L / Brightspace | **web only** — course notifications, and the same story |

*(Product-design facts rather than transient API terms, but verify before
building — this determines what §2.5 may ever claim.)*

### WHAT THAT DOES TO §2.5, WHICH IS THE POINT OF THE PRODUCT

"You have time for a friend" is a trust claim. If Signal and WhatsApp are
invisible, the assistant **can never honestly say "nothing is pending"** — only
*"nothing is pending in the seven places I can see, and I cannot see Signal or
WhatsApp."* That is not a degraded version of the feature. It is the honest
version, and §2.5 already requires it: every completeness claim names its
sources.

### THE LOGIN TAX IS THE DISEASE, NOT THE OBSTACLE (2026-08-24)

> Warren: *"SSO with MFA is already brutal — welcome to my life and why I miss
> so many things. This may be mitigated by a morning routine of logging in for
> 5-10 minutes. I am sure I am not alone."*

This document had authentication filed as friction *the reader* must survive.
That is backwards. **MFA friction is why Warren misses things in the first
place** — six systems, each demanding a phone tap, so they do not get checked,
so a promise made in Teams on Tuesday is found on Friday. The product is not
working around his auth problem; it is working around the *same* problem that
creates the need for it.

**That makes the morning login a feature rather than a concession.** If he signs
in once, deliberately, for five or ten minutes, that window is when every
browser-only channel is readable. Which turns the whole shape inside out:

    before:  log into six systems, and check each one, all day, forever
    after:   log into six systems ONCE, and be told for the rest of the day

Three consequences, and they are design changes rather than sentiment:

1. **Harvest hard during a live window, not lazily on a timer.** A 120-second
   poll is right for an always-open tab and wrong for a ten-minute session — it
   might sample a page twice before the tab closes. The reader should sweep
   immediately when a page becomes visible, and again shortly after, because the
   session is *known to be short*.

2. **Coverage must report FRESHNESS, not just status.** "Teams: last read
   Tuesday" is the sentence that makes the brief honest. §2.5 says a
   completeness claim names its sources; under a login-window model it must also
   name *when* each was last actually seen, because a stale channel and a broken
   one fail identically from the reader's side and differently for Warren.

3. **"These need you to sign in" is itself a promise.** It has an owner (him), a
   due time (this morning), and a cost if skipped (a channel goes dark). Putting
   it in the ledger turns the login routine from an open-ended chore into a
   bounded, checkable task — and one that shrinks on days when three of the six
   sessions are still alive.

**"I am sure I am not alone" is the generalisable part.** MFA fatigue in
institutional settings is near-universal, and the usual response is either to
weaken the auth or to nag the user. This does neither: it makes the login the
user already performs pay for the entire day.

### THE BROWSER IS THE PRIMARY PATH, NOT A FALLBACK (corrected 2026-08-24)

> Warren: *"the only way to reach office 365 or teams is through the web. Also
> d2l for classes."*

That reverses this section's framing. The Chrome reader was filed as a way to
recover three social channels an API could not reach — a supplement. But O365,
Teams and D2L are **the work calendar, the work chat and the course system**,
and if the browser is the only way in, then the browser is the ingest path for
the majority of what §2.3 must read. Microsoft Graph exists; it is not available
to *him*, which is the only fact that matters here.

So `edge/chrome/` stops being a nice-to-have for Facebook and becomes the
load-bearing route for P1.1 as well as P1.2 — and its `blind` reporting stops
being a scraper nicety and becomes the thing that keeps the *calendar* honest.
An API integration that breaks returns an error; a scraper that breaks returns
silence unless it is built not to, which is why that machinery was worth the
effort before anyone knew how much would depend on it.

**Consequence for the roadmap:** the reader needs three more extractors
(Outlook/O365 calendar, Teams, D2L) and they are worth more than the two it has.
Each is an institutional tenant, so the markup is more stable than a consumer
social app's — but each is also behind SSO, which the reader must detect as
`loggedout` rather than `blind`, exactly as the Facebook and Discord redirects
taught.

### AND IT PROMOTES §2.6 FROM CONVENIENCE TO MECHANISM

One-gesture capture stops being a nicety the moment half the channels are
unreadable. **It is the only coverage mechanism those channels have**: a promise
made on Signal enters the ledger because Warren captures it in fifteen seconds,
or it does not enter at all. That reframes the capture loop as load-bearing
infrastructure rather than a phone feature, and it raises §2.6 in the priority
order.

The forward-to-inbox pattern covers some of the gap — several of these can be
made to email an address Warren controls, converting an unreadable channel into
a readable one — and should be tried before writing any integration, because it
is configuration rather than code.

### Q6: what the assistant does when it is not sure

**It asks.** Warren: *"I think the assistant asks, somewhat like you do now. I am
ok if questions drive values instead of merely accepting them."*

The second sentence is the load-bearing one and it licenses something a servile
assistant could not do. A question is permitted to be **formative, not merely
extractive** — *"you have moved this three times; is it actually a promise?"* is
a question that changes what Warren decides, not just what the machine records.
Without that sentence, asking it would be presumptuous. With it, declining to
ask is the failure.

Three consequences for the build:

1. **Never guess silently.** An uncertain classification is a question, not a
   confident row. This is §2.9 applied to judgment rather than to errors.
2. **Questions are batched into the brief, not pushed as they arise.** The
   product exists to reduce interruption; an assistant that interrupts to
   resolve its own uncertainty has taken the problem and handed it back.
3. **A question is itself a promise** — it has an owner and a due time, and an
   unanswered one is visible rather than silently dropped. `viki_note` already
   has `challenge`, which is exactly *"an unanswered are-you-still-on-this."*

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
