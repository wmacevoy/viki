# PLAN — from a red suite to a running server

What has to happen, in what order, and what proves each step is done. Sizings marked *(measured)*
come from `FINDINGS.md` B-1, where a reviewer implemented ~660 lines and got 92 of the tests green;
everything else is an estimate and says so.

**The shape of the recommendation, up front:**

1. **Python first**, as the reference implementation *and* the conformance oracle. C second, against
   it. Python does not ship to a phone or to wasm, so C is not optional — but a differential oracle
   is worth more than any dependency on the list.
2. **Launch single-tenant.** One person's devices, one hub. That defers the abuse problem (R-1
   below), which is the only item here that is legal exposure rather than engineering.
3. **The server is a peer, and it can read metadata.** That is decided (G-6a) and it is a disclosure
   obligation, not a detail.

---

## Phase 0 — Decisions (2–3 weeks, and not coding weeks)

These need the owner, not an engineer, and a wrong guess on any of them costs rework downstream.
B-1 listed eight; three have since been answered. What is left:

- **0-1 Scale, with a horizon.** How many assertions in year one, and in year three? How many
  devices? The answer decides whether the digest's 256 buckets are right and whether a full-scan
  fallback is ever acceptable.
- **0-2 Availability budget, with a period and a beneficiary.** The unstated default is 100%, which
  quietly forbids deploys and schema changes. A hub that may be down four hours a month is a
  different system from one that may not.
- **0-3 Recovery.** What does it take to get back, who does it, and how long? This is not answered
  until someone has *run* it and timed it — see the Phase 3 gate.
- **0-4 Retention.** How long does a sealed item sit before erasure (W-7), and who is entitled to
  decide? Currently unset.
- **0-5 Eviction policy** (open question 6). What goes first, who sets the bound, and does the user
  get told before something becomes unrecoverable (X-5)?
- **0-6 The reference-format probe** (open question 8). Do Gmail, Microsoft Graph and Drive each
  expose a usable etag? X-7 assumes so. **This is a probe, not a decision** — a day's work, and the
  only open item that can still move a requirement.
- **0-7 `b`, confirmed against real hardware.** N-16d derives 11 from a one-minute budget, a
  memory-hard KDF at ~0.5 s and eight workers. Measure the KDF on the actual phone before trusting
  the arithmetic.

**Gate:** each has a written answer in `REQUIREMENTS_v2.md`, and 0-6 has a run.

---

## Phase 1 — The reference implementation (5–7 weeks *(measured)*)

Python, SQLite only, no network. All 293 tests green.

**Order matters, because 71% of the suite currently dies in `writer.py`** and cannot exercise what it
names until that module exists:

| Step | Modules | Unblocks |
| --- | --- | --- |
| 1-1 | `ids`, `schema`, `store` | framing, normalization, the DDL — testable with no store |
| 1-2 | `writer`, `reader`, `diary` | ~92 tests *(measured)*, and every later test's setup |
| 1-3 | `withdrawal` | 32 tests, and the module no test currently reaches at all |
| 1-4 | `merger` | 33 tests |
| 1-5 | `refs`, `derive` | 26 tests |
| 1-6 | `sync`, `secret` | 20 + 19 tests |

**Two things about the gate that are easy to get wrong.**

`check.py` today asserts **nothing passes** — it proves the suite is a valid *unimplemented*
specification. The moment implementation starts, that assertion is exactly backwards. The gate needs
a mode flag, and whoever runs it needs to know which question they are asking. A green
`check.py` in week three would mean the suite had stopped testing anything.

And **the requirement-coverage check must keep running** through implementation. It is the thread
that makes `grep W-2` walk from a test to the sentence that demanded it, and it is the first thing
that rots when people are busy making tests pass.

**Dependencies:** `hypothesis`, test-only. The top-line claim of the system is a semilattice law
currently checked at a handful of hand-written points; generated operation sequences with shrinking
are the difference between believing merge is confluent and holding a minimal counterexample. Attack
sweep confluence (G-6) with it first — that is where the hardest correctness risk lives.

**Gate:** 293 passed, 0 failed, 0 uncovered requirements, and a property run over shuffled merge and
sweep orders that has actually found nothing.

---

## Phase 2 — Wire format and the second engine (+4 weeks, estimated)

- **2-1 `export()` / `ingest()`** (G-1a, G-1b). The real path; store-to-store sync becomes a
  convenience over it. Nothing can cross a network until this exists.
- **2-2 Postgres peer.** Bodies encrypted at the application layer, since SQLCipher has no PG
  analogue. `bytea` everywhere (A-4a already forces this), explicit conflict targets on every upsert
  (G-2), and an isolation level or advisory lock for the merge sequence (G-6) — SQLite's single
  writer supplies that for free and MVCC does not.
- **2-3 Cross-engine conformance.** The same suite, parameterized over the engine. `Store` already
  takes a connection rather than a path, so this is a fixture change.

**Gate:** the suite green on both engines, **and** a SQLite peer and a Postgres peer syncing to
identical digest roots. That second half is the one that matters — the first only proves each engine
is self-consistent.

---

## Phase 3 — The server (3–4 weeks, estimated)

- **3-1 HTTP over the S protocol.** `export`/`ingest` behind endpoints. No auth in viki, by standing
  policy — a fronting container makes the access decisions, because a viki-layer control is a
  guardrail and a container is a layer that can hold a boundary.
- **3-2 Fronting proxy.** TLS, rate limits, request-size caps by route. The size cap is not
  hygiene: merge is O(difference) but a hostile sender still chooses what to send.
- **3-3 Key custody.** The PG peer holds the database key. Decide where it lives — environment,
  secrets manager, or an operator-entered key at start — and write down who can read it. This is the
  step most likely to be done casually and hardest to fix afterwards.
- **3-4 Backups**, and the restore runbook.

**Gate:** **someone other than the author restores from a backup, following the runbook, and times
it.** A restore procedure nobody has executed is the canonical anxiety-reducing artifact with the
risk left in place. That number answers 0-3.

---

## Phase 4 — Connectors (unsized, and honestly so)

Out of scope to viki by V-4a, which is exactly why nobody has estimated them.

- **4-1 The Gmail and Outlook robots.** OAuth, rate limits, incremental fetch, and whatever 0-6
  discovers about etags.
- **4-2 The triage API.** One artifact in, a bounded verdict out, on an append-only log. Deterministic
  redaction by a robot, two lightweight reviewers, escalation to a capable model only past the gate.
  A connector behind its own API.
- **4-3 The assistant agent.** Reads the source diaries, publishes summary and calendar. Granted `r`
  on sources and `s` on the published diary — never `x`.

**This is the phase that touches other people's APIs, so it is the one that will slip.** Size it after
0-6, not before.

**Gate:** a real message arrives in Gmail, is triaged, is summarized, and the summary appears on a
phone that was offline while it happened.

---

## Phase 5 — Launch, single-tenant

One person, their devices, one hub. Deliberately not multi-tenant.

**Gate:** thirty days of use by someone who is not the author, with the diary surviving a device
loss and a restore.

---

## Risks, ranked

**R-1 — Abuse has no in-model remedy, and it is the only legal exposure here.** Grow-only, anyone who
can write can write anything, and erase authority is held by the author. So an operator cannot remove
CSAM, malware, doxxing, or a leaked credential except by destroying the store. `Reason.LEGAL` exists
in the vocabulary; the authority to act on it does not. **Single-tenant launch defers this entirely**
— you are the only writer. It becomes a blocker the day a second tenant exists, and it needs an
operator class in the rights model plus inbound admission control. Do not let multi-tenancy arrive by
accident.

**R-2 — The operator sees the metadata graph.** G-6a, accepted as a K4: `akey` must be queryable to
resolve and `id` to join, so a Postgres peer's operator sees ids, timestamps, kinds, keys and the
shape of derivation — who corresponds with whom, how often, across how many topics. Fine when the
operator is you. A disclosure obligation the moment it is not.

**R-3 — Connectors are unsized and touch systems you do not control.** Rate limits and etag
availability are discovered, not designed.

**R-4 — The C implementation is 14–18 weeks and is not optional.** The phone and the wasm tier need
it. Starting it before Phase 1 is finished loses the oracle; starting it too late makes the phone a
year away.

**R-5 — Key custody done casually in Phase 3.** The cheapest thing to get wrong and among the most
expensive to fix, because rotating means re-keying every peer (N-17).

---

## Rough total

**Four to six months to a single-tenant launch**, with Phase 4 the wide error bar. Phases 0 and 1 are
the ones with measured numbers; 2 and 3 are estimates against known work; 4 is a guess until 0-6 runs.

The C implementation runs *after* Phase 1 and in parallel with 2–4, adding 14–18 weeks of its own on
a track that does not block launch.

## Not doing

- **Multi-tenancy**, until R-1 has an answer.
- **The retrieval layer** — chunks, embeddings, search. Named in M-5 and W-4 as obligations and
  otherwise another document's problem.
- **Auth inside viki.** Standing policy; a fronting container decides.
- **The wasm tier**, until the C implementation exists.
- **Any dependency not in `README.md`.** The design is near-minimal and a fourth review confirmed
  almost nothing would help.
