# viki — episodic memory for agents

*(Companion to `VIKI_DESIGN.md`, which specifies the retrieval layer. This
file specifies what is retrieved. Read `VIKI_DESIGN.md` first; D-10/D-11/D-12
constrain everything below and are not relitigated here.)*

## The missing property, stated once

`viki_chunk` is keyed `(content_hash, model_id, chunk_ix)`. There is no time
in that key, no actor, no supersession, no provenance. A store shaped like
that can tell you *what text exists*. It cannot tell you *what is true now*,
and it will answer the second question with the first, confidently.

This repo has already paid for that twice, and both incidents are on the
record:

**1. The forum bug (FINDINGS.md, 2026-08-13).** An edited forum post leaves
the old artifact in `event` with `type='f'` forever. `viki index` selected on
type alone, so `viki ask "graphite rope"` returned at **rank 1** text that
Fossil's own `/forumthread` page renders **zero** times. Fossil had modelled
the supersession all along — the `fprev` card — and viki did not read it. The
fix was one SQL clause. The lesson is not the clause: it is that *content
without a supersession edge cannot be served correctly no matter how good the
ranking is*.

**2. The 54/90 contradiction.** `test/m1.sh` grew from 54 to 90 assertions on
2026-08-13. `54 passed, 0 failed, 0 skipped` was a correct measurement.
`90 passed, 0 failed, 0 skipped` is also a correct measurement. Both sentences
are in the tree right now — CLAUDE.md line 139 ("it has already scored a full
green (54/54 …)") and line 155 ("all present → `90 passed, 0 failed, 0
skipped`") — and neither is wrong. What is missing is the **validity window**:
54 was true against a
54-assertion file, and nothing in the storage model records what a number was
measured *against* or when it stopped applying. The repo's response was to
bolt a hand-written window onto the whole corpus, at the top of FINDINGS.md:

> **Entries are dated snapshots.** A figure inside an entry describes the tree
> as of that entry's date, not today. […] re-run the command before quoting
> the number.

That paragraph is a per-claim validity window implemented in prose, applied
file-wide because there was nowhere else to put it. It is the strongest
evidence available that this design is needed: an agent felt the defect and
patched it with the only mechanism the system offered.

There is a third, smaller incident worth carrying because it names the failure
mode from the other end. AGENTS.md's "Not yet built" list called the
`msys-2.0.dll` gap "a real gap" for **14 hours after commit 478b8e3 closed
it** — and 478b8e3's own message says it closed it. That is not a supersession
failure; it is a *reachability* failure. The agent who fixed the thing did not
know the stale claim existed, three screens away in a 790-line file. Any
memory design that only makes writing easier makes this worse.

**Design thesis.** A memory is not a document. It is a **claim** carrying a
validity window, a provenance, an actor, an episode, and an explicit edge to
whatever it replaced. Store the claim in a Fossil artifact; derive everything
queryable. That is D-10 applied to memory rather than to vectors.

---

## The envelope is already Fossil's

Before inventing a format: a Fossil **tech note** manifest, measured this
session against `vendor/fossil-see/build/dist/fossil-see`, looks like this.

```
C memory:\stagged                                        <- claim (timeline text)
D 2026-08-17T03:15:27.335                                <- RECORD time
E 2026-08-18T00:00:00 922cddc2ce074402…506029            <- EVENT time + STABLE ID
N text/x-markdown
P fc2361114baf6365…5cfca568                              <- PREDECESSOR (edits only)
T +sym-about-strtok_r *                                  <- subject tags
T +sym-kind-negative *
U tester                                                 <- actor
W 12                                                     <- counted body
tagged body

Z 4d78fb3feff41221…
```

Every field the memory model needs except provenance, confidence and episode
already has a native card:

| memory property | Fossil card | notes |
|---|---|---|
| the claim | `C` | also the timeline text; escaped, needs `unquote_fossil()` |
| when it was true | `E` (first field) | **settable**, independent of record time |
| when it was written | `D` | set by fossil, not by the author |
| stable identity | `E` (second field) | 40-hex technote id, survives edits |
| supersession edge | `P` | fossil's own predecessor pointer, same role as `fprev` |
| subject axis | `T +sym-…` | real rows in `tag`/`tagxref`, queryable |
| actor | `U` | the fossil account, per D-4/US-2a |
| body | `W <n>` | counted string, same shape `index_forum()` already parses |

**Decision M-1: a memory is a tech note.** Not a file, not a wiki page, not a
forum post. The reasons are measured, not aesthetic:

- **The event time is the point.** `fossil wiki create -t <DATETIME>` sets
  `event.mtime` to a time the author chooses. Verified: a note created at
  `2026-08-13T19:48:15` on 2026-08-17 reports `E-time 2026-08-13 19:48:15`.
  Nothing else in Fossil separates "when this was true" from "when this was
  written" without extra machinery.
- **Fossil's own edit rule gives "current by default" for free.**
  `vendor/fossil-see/vendor/fossil/src/manifest.c:2542` DELETEs the old
  `type='e'` row before re-inserting, so
  `event.type='e'` is *always* exactly the current revision set. The forum
  bug's shape — both revisions live in `event`, and the reader picks wrong —
  is structurally impossible here. Wiki pages do **not** have this property
  (both `+Name` and `:Name` events survive; that is the forum shape again),
  which is why a memory is not a wiki page.
- **Cheap.** Measured this session: **236 ms** per `fossil wiki create` on an
  encrypted `.efossil`, **8 ms** plaintext, and the manifest envelope costs
  **~250 bytes** over the body (255-byte artifact for a 12-byte body). The
  artifact-coverage audit measured ~470 ms per `fossil sql` invocation on its
  larger encrypted repos; treat "a fifth to half a second per fossil
  subprocess, ~30–70× plaintext" as the planning figure, not either number
  alone.
- **Auditable by construction** (US-2f): it is in the timeline, has a
  `/technote/<id>` page, and syncs like everything else.
- **Already nearly indexed.** The artifact-coverage audit specified the
  `note:` extractor as `index_forum()` with three substitutions
  (`type='f'`→`'e'`, card `H`→`C`, prefix `forum:`→`note:`). The memory
  extractor is that plus a header parse.

### Two measured traps that decide the operational rule

**Trap 1 — editing a note by ID silently rewrites when it was true.**

```
$ fossil wiki commit "…" body -t 80c1e6ac…9ee5      # edit by TECHNOTE-ID
Updated tech note 2026-08-17 03:14:51.               # event time moved to NOW
$ fossil wiki commit "…" body -t "2026-08-17T03:14:51"   # edit by DATETIME
Updated tech note 2026-08-17 03:14:51.               # event time preserved
```

The one-argument `-t` is overloaded: given an id it updates the note and
resets `E` to now; given a datetime it preserves it. There is no spelling that
takes both. An edit therefore *destroys the validity window by default* —
which is the exact class of bug this document exists to prevent.

**Trap 2 — an edit that does not restate its tags drops every tag.** Measured:
a note tagged `viki-memory kind-negative about-strtok_r`, edited without
`--technote-tags`, produced a manifest with **no `T` cards at all**, and
`SELECT … WHERE tagname='sym-viki-memory'` stopped returning it. The memory
fell out of its own namespace.

**Decision M-2: memories are append-only. Never edit a memory; supersede it.**
Both traps vanish, and — the deeper reason, §"Supersession" — an append-only
set of immutable artifacts converges under sync in any arrival order, while an
edit does not.

Consequence for the extractor: **identify a memory by its body header, not by
its tag.** The body is supplied by the author on every write; the tags are not.
Tags are a prefilter, never the truth. (This is the same lesson as the four
supersession shapes: do not trust a derived index when the artifact is
available.)

---

## The memory envelope

The body of a memory note begins with a header block, then free prose. The
header is *in the artifact*: it syncs, it renders in the web UI, it is
readable without viki, and viki parses it into derived columns. Source of
truth in the artifact, projection in the cache — D-10, applied to memory.

```
viki-memory: 1
id: 922cddc2ce074402…506029          # the technote id; assigned by fossil
claim: test/m1.sh reports 90 passed, 0 failed, 0 skipped
kind: measurement                    # decision|measurement|defect|negative|
                                     # convention|question|episode
valid-from: 2026-08-13T19:48:15Z
valid-until: open                    # or an instant; set by the successor
provenance: measured                 # measured|derived|read|reported|assumed
evidence: bash test/m1.sh
witness: build/dist/viki@74f7bf40…   # path@content_hash — see "Provenance"
actor: claude
episode: ep-2026-08-13-m1-close
task: close Milestone 1
about: test/m1.sh                    # repeatable: path, subsystem, identifier,
about: viki_ask.c                    # or content_hash
supersedes:                          # repeatable memory ids; see below
rejected: keep the 54-assertion file # repeatable; the alternatives NOT taken

The 90 assertions are the 54 plus 36 added when the forum/hash/staleness
fixes landed. Do not read the two 36s as the same 36.
```

Rules that make this survivable:

- **Frozen at v1.** The artifact-coverage audit established that because
  `content_hash = sha256(composed extracted text)`, the composition recipe is
  de facto part of the D-11 sharing contract even though `viki-manifest` says
  nothing about it. Two peers composing a memory differently produce different
  hashes for the same memory, both embed it, and both appear in `viki ask`.
  That is cache fragmentation, not corruption, and it is **not** an epoch bump
  — but the format must be frozen when it lands and any later change called
  out as cache-fragmenting.
- **Unknown keys are preserved and ignored**, so a newer writer's fields do
  not break an older reader.
- **`claim:` is the retrieval unit.** It is repeated at the head of every
  chunk of the memory, exactly as `index_forum()` composes `title + "\n\n" +
  body`.
- **Memories are short, and the reason is measured.** The tokenization survey
  found `VIKI_MAX_SEQ_LEN = 256`, a 254-token content window, of which 31.8%
  of consumed tokens are single punctuation characters — an effective window
  of **~170 words**. A memory whose body runs past that has its tail invisible
  to the vector leg while fully visible to BM25; the two legs then index
  different documents, which the survey identified as a previously unmeasured
  cause of hybrid losing to BM25 at rank 1. `viki remember` warns above ~150
  words and suggests splitting. **Many small memories, never one long one.**

---

## Time and validity

Three clocks. Conflating any two of them is the 54/90 bug.

| clock | where it lives | who sets it |
|---|---|---|
| **t_event** — when the claim was true | `E` card / `valid-from` | the author, deliberately |
| **t_record** — when the memory was written | `D` card | fossil |
| **t_until** — when it stopped being true | `valid-until`, or derived from a successor's `valid-from` | *nobody at write time* |

The third is the one that matters and the one no writer can supply. `54
passed` had no way to know that a later session would make it false. So
`valid-until` is **not primarily authored — it is derived from the
supersession edge**: a memory superseded by a successor whose `valid-from` is
`T` has `valid-until = T`. An author may close a window explicitly (`this
stopped being true when we removed the flag`), but the common case is
automatic. That is the whole mechanism: *supersession is what produces
validity windows, which is why supersession is first-class and validity is
derived from it.*

### Query semantics

- **`viki ask "<q>"` — as-of-now, the default.** Candidates whose memory is
  not current are excluded after fusion. Non-memory content (files, wiki,
  tickets, check-in comments) is unaffected: it has no window and is always
  eligible.
- **`viki ask --as-of 2026-08-13T12:00Z "<q>"`** — the same retrieval,
  filtered to memories whose window contains that instant. This answers "what
  did we believe when we made that decision", which is the question you ask
  when reading a six-month-old commit.
- **`viki ask --history "<q>"`** — no window filter; every revision, each hit
  labelled with its window and its successor.
- **`viki why <memory-id>`** — the chain: what this superseded, what
  superseded it, the evidence at each hop, the alternatives each hop
  rejected.

**The interaction with D-10 is the clean part.** Validity lives in artifacts;
the cache is a projection; therefore `--as-of` is **a predicate over a current
index, not a restored old index**. Historical query needs no historical cache
and no snapshotting. Delete `.viki/cache.db`, re-index, and every window comes
back identically. That is the property that makes this design compatible with
D-10 rather than a violation of it.

**The one behaviour it changes in existing code, and it is load-bearing.**
`gc_orphan_chunks()`/`sweep_sources()` today delete chunks that no live source
references, and FINDINGS.md is explicit that this scoping is safety-critical.
A superseded memory is **not** withdrawn content — it is content with a closed
window, and `--as-of` cannot work if it is deleted. So the sweep must
distinguish:

- *unreferenced* (the source is gone: file deleted, note deleted) → delete, as
  today;
- *superseded* (the source is a memory with a closed window) → **retain**,
  filter at query time.

That is a real cost: the memory index grows monotonically. Priced from the
artifact-coverage audit's measured figures (~6.4–6.9 KB of cache per chunk,
~7.6 ms wall to embed one chunk): 10,000 one-chunk memories ≈ **~70 MB of
cache and ~76 s of one-time embedding**, and under D-12 that ~70 MB is a uv
blob pushed to every peer, with no unchanged-content short-circuit in
`fossil uv add` (FINDINGS.md). Forgetting is not free either — see "What
cannot be built".

---

## Supersession

### Why D-11's last-write-wins is not sufficient

D-11's LWW is a statement about **one key with equivalent values**: two peers
independently embed the same `(content_hash, model_id, chunk_ix)`, ONNX float
wobble makes the vectors non-identical, cosine does not care, so whichever
lands last is fine. VIKI_DESIGN.md says exactly this and it is correct.

Supersession is the opposite case: **two different keys with contradictory
values, where one is right and one is wrong, and which is which depends on
facts neither peer holds.** Concretely, with the incident this repo actually
had:

```
T1  agent A, offline:  "test/m1.sh reports 54 passed"     (true at T1)
T2  agent B, online:   "test/m1.sh reports 90 passed"     (true at T2 > T1)
T3  agent A syncs
```

LWW at the cache level resolves by arrival order at the hub, which is a
function of network topology. A's 54 arrives last and wins. The system is
confidently wrong, and nothing anywhere flags it — which is, within a
rounding error, what happened.

### The rule

**Decision M-3: supersession is an explicit, immutable edge over stable ids,
written by the superseding memory.**

```
supersedes: <memory-id>        # repeatable
retracts:   <memory-id>        # different edge, different semantics — below
```

Why an edge and not a timestamp, a version number, or a value:

- **Edges converge under set-union; values do not.** Artifacts are immutable
  and additive, so every peer eventually holds the same edge set regardless of
  arrival order, and "which memory is current" is a *reduction over that set*
  — order-independent by construction. This is the property LWW lacks and it
  is the entire argument.
- **It is discoverable from the superseded side.** `viki why` walks it
  backwards. The 14-hour stale-bullet incident was a failure to find the old
  claim from the new one; a backwards-walkable edge is what fixes it.
- **It is Fossil's own answer.** `fprev` for forum posts, `P` for technote
  revisions, `ecomment` for amended check-ins. viki should copy the mechanism
  it already got burned for ignoring.

`current(M)` ≡ no memory `N` exists with `supersedes: M` where `N` is itself
not retracted. Nothing about arrival order, wall clocks, or who pushed first
appears in that definition.

### Supersede vs. retract — not the same edge

- **`supersedes`** — "this was true, and stopped being true." The old memory
  keeps its window and is still the correct answer to `--as-of` inside it.
  `54 passed` is superseded: it was a correct measurement of a file that no
  longer exists in that form.
- **`retracts`** — "this was never true." No window is valid. The
  `msys-2.0.dll is absent` bullet was not superseded by a later fact; it was
  contradicted by a commit that already existed when it was written. `--as-of`
  must not resurrect it.

Collapsing these loses the ability to read old decisions honestly, which is
the main thing an episodic memory is for.

### Concurrent supersession: fork, and do not resolve it

Two agents, both offline, both supersede M. On sync, M has two successors and
neither supersedes the other. That is a genuine conflict and viki must **not**
pick.

**Decision M-4: a forked supersession is surfaced, never resolved.** Both
successors are returned, both flagged `CONTESTED`, and `viki remember` opens a
ticket assigned to the humans — ARCHITECTURE.md step 4 and US-2e ("commit mine
to a branch and open a ticket rather than auto-merging over a person"),
applied to memory. A memory system that silently picks a winner between two
contradictory agent claims is worse than no memory system, because it launders
the disagreement.

This has never been exercised. See "Multi-agent merge: what breaks".

---

## Provenance and confidence

This repo's culture is one sentence — *measure, do not assert* — and its
hardest retrieval problem is that the sentence is invisible in stored text. A
memory that says "hybrid is worse than BM25 at rank 1" reads identically
whether somebody ran the harness or guessed.

**Decision M-5: `provenance:` is a required field with a weak default.**

| value | means | admissible evidence |
|---|---|---|
| `measured` | a command was run; its output is quoted | `evidence:` naming the command **and** the quoted result |
| `derived` | read out of source or a spec, not run | `evidence:` naming file:line |
| `read` | read from documentation/upstream | `evidence:` naming the doc |
| `reported` | another agent's handoff said so | `evidence:` naming the memory or session |
| `assumed` | reasoning, no evidence | none |

Three rules, each traceable to an incident:

1. **The default is `assumed`.** Design principle 0 says filing decisions kill
   capture, so `viki remember "<claim>"` must work with no flags — and
   laziness must therefore produce the *weakest honest label*, never a
   flattering one. Provenance is upgraded by supplying evidence, never by
   omitting it.

2. **Provenance does not upgrade by copying.** A memory whose only evidence is
   another memory is `reported`, full stop. FINDINGS.md names this channel
   precisely: a handoff report correctly stated `test/m1.sh` "contains zero
   occurrences of forum"; by the time it could be transcribed the true count
   was 8, and *"pasting its sentences into a doc launders an unverified claim
   into documented truth."* The provenance ladder is exactly a laundering
   detector, and `viki remember` refuses `--provenance measured` unless
   `--evidence` names something runnable.

3. **`measured` is checkable, not decorative — the witness hash.** A
   measurement is about a subject that can move. `90 passed` was measured
   against `build/dist/viki` at a specific mtime, and FINDINGS.md has an entire
   entry on why that matters: *"A green `test/m1.sh` says nothing about the
   source tree — it passed 54/54 against a binary 13 hours older than the fixes
   sitting in `src/`."* So `witness: <path>@<content_hash>` records what the
   claim was measured against. viki already computes content hashes for
   everything it indexes; on recall it compares, and a memory whose witness no
   longer matches is printed `STALE-WITNESS`:

```
[1] rrf=0.0164  mem:922cddc2…#0   measured 2026-08-13  STALE-WITNESS
    test/m1.sh reports 90 passed, 0 failed, 0 skipped
    witness build/dist/viki@74f7bf40… no longer matches (now <current-hash>)
```

That single line converts "measured" from a label into a **predicate that the
system evaluates at read time**, and it is the cheapest high-value thing in
this document. It would have caught both the 54/90 confusion and the green-run
trap without anyone having to remember anything.

**Confidence is deliberately not a number.** A 0–1 score invites arithmetic
nobody can justify. `provenance` + `witness` + `valid-from` age are three
facts an agent can act on; a float is a fact it cannot.

---

## Actor and episode

`actor:` is free — the fossil account (D-4: agents are ordinary users; US-2a:
the timeline shows `author: claude`). The `U` card carries it.

`episode:` has no fossil equivalent, so it is bootstrapped from the memory
model itself: **an episode is a memory of `kind: episode`**, opened at session
start with a `task:` line, and every memory written during that session
carries `episode: <its id>`. `$VIKI_EPISODE` in the environment makes this
zero-friction for a coding agent.

This is what makes *"what did we try last time"* a query rather than an
archaeology exercise:

```
viki episodes --about viki_ask.c        # sessions that touched this, newest first
viki ask --episode ep-2026-08-13-m1-close "what did not work"
```

The motivation is US-2b's requirement that an ephemeral agent's state "lives
in the repo, none in my head." The current implementation of that requirement
is a 790-line AGENTS.md plus a 2,645-line (137 KB) FINDINGS.md, appended to by
every session — and which FINDINGS.md itself documents going stale four
separate ways in a single file, *because the rule "update the doc in the same
commit" implicitly assumes each claim lives in exactly one place.* Episodes plus
per-claim ids remove that assumption: a claim lives in one artifact, and
everything else cites its id.

---

## Negative knowledge

The eval baseline says this is the worst class in the set:

```
negative-trap [dev]   n=6   recall@1=0.333  recall@5=0.333  recall@k=0.333
negative-trap [test]  n=3   recall@1=0.000  recall@5=0.000  recall@k=0.000
```

The stated problem is real: the useful query is *the problem you are facing*,
not the term you would only know in hindsight. Nobody searches "strtok_r" —
they search "a ticket field came back empty."

There are two channels, and conflating them is why this class fails.

### Channel 1 — symptom indexing (a write-time obligation, measurable today)

**Decision M-6: `kind: negative` requires at least one `symptom:` line.**

```
kind: negative
claim: strtok_r silently corrupts TSV parsing when fields are empty
tried: strtok_r on fossil's TSV output
symptom: a ticket field came back empty when it was not
symptom: two adjacent fields merged into one
symptom: record has fewer fields than the header
about: viki_index.c
rejected: keep strtok_r and pre-scan for empty fields
```

`claim:` is the cause, in the vocabulary of somebody who already knows.
`symptom:` is the same knowledge in the vocabulary of somebody who does not —
which is the vocabulary they will actually type. This is not a ranking trick;
it is a content change, it costs nothing at query time, and it is directly
measurable against the `negative-trap` and `vocab-mismatch` classes with the
existing harness.

### Channel 2 — subject triggering (the thing you cannot query for)

For the case in the brief — *the thing you do not know exists, so cannot query
for* — the honest answer is: **do not retrieve it by query. Trigger it by
subject.**

`tried:` and `about:` name identifiers, files, APIs and approaches. `viki` can
then answer a question nobody asked:

```
viki warn --about viki_index.c        # negative memories about this file
viki warn --about strtok_r            # negative memories about this symbol
```

The agent does not search; the *workflow* does the lookup when it opens a
file, names a symbol, or writes a `viki remember`. As shown above,
`--technote-tags about-strtok_r` becomes a real `sym-about-strtok_r` row in
`tag`/`tagxref`, so this is a tag join, not a similarity search — exact,
cheap, and unaffected by ranking quality.

**Two honest limits.** (a) The trigger needs an integration point — a hook, an
MCP verb (ARCHITECTURE.md's v2 layer), or an editor. v1 ships the verb; wiring
it into an agent's loop is M2. (b) `test/retrieval-eval.sh` **cannot measure
channel 2 at all**. It scores ranked retrieval for a typed query; channel 2 has
no query. It needs a different harness — precision/recall over "was the right
warning fired for this subject" — and claiming the existing numbers cover it
would be exactly the kind of unearned claim this repo keeps catching.

---

## The write path: `viki remember`

Today the only write path is editing a markdown file, and that is what
produced the contradiction: one 137 KB file, every agent appending, each claim
restated in a narrative section *and* a layout table *and* a to-do list.

```
viki remember "test/m1.sh reports 90 passed, 0 failed, 0 skipped" \
  --kind measurement \
  --provenance measured \
  --evidence 'bash test/m1.sh' \
  --witness build/dist/viki \
  --about test/m1.sh \
  --supersedes mem-3f2a… \
  --rejected 'keep the 54-assertion file' \
  --valid-from 2026-08-13T19:48:15Z \
  [body on stdin]
```

Everything except the claim is optional. Defaults: `actor` = fossil user
(`viki_fossil_user()`, already implemented), `episode` = `$VIKI_EPISODE`,
`valid-from` = now, `kind` = `note`, **`provenance` = `assumed`**.

Mechanically it is one subprocess:

```
fossil wiki create "<claim>" <bodyfile> \
   -t "<valid-from>" -M text/x-markdown \
   --technote-tags "viki-memory kind-<kind> about-<a> about-<b> …"
```

**Cost, measured this session:** one blob, one `event` row, one `tag` row per
new subject, ~250 bytes of manifest envelope over the body, and **236 ms** on
an encrypted repo (8 ms plaintext). That is fine for a deliberate act and
firmly *not* fine for automatic capture of everything an agent thinks — which
is the right answer anyway. Remembering is a decision.

### The one feature that would have prevented incident 3

**Decision M-7: `viki remember` searches for supersession candidates before
writing.** It runs the claim through `viki ask` restricted to current
memories with an overlapping `about:`, and shows what it found:

```
$ viki remember "msys-2.0.dll is bundled next to viki.exe" --about build.sh
3 current memories may be affected by this claim:
  [1] mem-8ac1…  "CI does not copy msys-2.0.dll into the release artifact — a real gap"
                 assumed · 2026-08-12 · about build.sh, .github/workflows/build.yml
  [2] …
Supersede any? [1,2,3/n/all]
```

FINDINGS.md's own prescription after the 14-hour incident was manual: *"before
editing a doc claim, grep the whole file for the other places the same fact
appears."* This is that rule mechanized, and it is the highest-value feature in
the design, because it attacks *reachability* — the failure mode that better
writing cannot fix.

### Markdown is not abandoned

A memory may live in **any** indexed artifact as a fenced block, so FINDINGS.md
entries can carry structured headers without changing the workflow that
produces them:

````
```viki-memory
id: mem-7c31…
kind: negative
claim: strtok_r silently corrupts TSV parsing when fields are empty
symptom: a ticket field came back empty when it was not
provenance: measured
evidence: see repro below
```
````

One parser, two containers; tech notes are the *preferred* container for
memories written by `viki remember`, not the only one. This matters for two
reasons: it makes the design incrementally landable, and it forces a
correctness point — **memory identity is the `id:` in the block, never the
containing file's `content_hash`**, because editing any other paragraph of
FINDINGS.md changes the file's hash and must not orphan the memory. For a tech
note the id is the technote id; for a fenced block it is assigned at write
time (`sha256(claim ‖ actor ‖ valid-from)`, truncated).

---

## Local schema (per peer, derived)

D-10 compliance in one sentence: **nothing below is source of truth, nothing
below changes `viki_chunk`, and `rm -rf .viki && viki index` reproduces all of
it from artifacts.**

```sql
/* Memory metadata. Keyed by memory id, joined to the shared chunk store by
** content_hash. Every column is parsed out of the artifact body. */
CREATE TABLE viki_memory(
  memory_id    TEXT PRIMARY KEY,  -- technote id, or block-assigned id
  content_hash TEXT NOT NULL,     -- of the COMPOSED memory text; joins viki_chunk
  container    TEXT NOT NULL,     -- 'mem:<id>' or the host source path
  claim        TEXT NOT NULL,
  kind         TEXT NOT NULL,
  provenance   TEXT NOT NULL,
  evidence     TEXT,
  witness_path TEXT,              -- 'measured' is a predicate, not a label:
  witness_hash TEXT,              -- compared at read time -> STALE-WITNESS
  actor        TEXT,
  episode      TEXT,
  task         TEXT,
  valid_from   TEXT NOT NULL,     -- ISO-8601 UTC, from the E card
  valid_until  TEXT,              -- NULL = open; usually DERIVED from an edge
  recorded_at  TEXT NOT NULL,     -- from the D card
  retracted    INTEGER NOT NULL DEFAULT 0
);

/* The supersession DAG. Edges only -- 'current' is a reduction over this
** table, never a stored flag, so it cannot go stale and cannot depend on
** arrival order. */
CREATE TABLE viki_memory_edge(
  newer_id TEXT NOT NULL,
  older_id TEXT NOT NULL,
  rel      TEXT NOT NULL,         -- 'supersedes' | 'retracts'
  PRIMARY KEY(newer_id, older_id, rel)
);

/* Subject axis: the channel-2 trigger. Mirrors the sym-about-* tags. */
CREATE TABLE viki_memory_about(
  memory_id TEXT NOT NULL,
  subject   TEXT NOT NULL,        -- path, identifier, subsystem, or content_hash
  PRIMARY KEY(memory_id, subject)
);

/* Current-as-of, order-independent. */
CREATE VIEW viki_memory_current AS
  SELECT m.* FROM viki_memory m
   WHERE m.retracted = 0
     AND NOT EXISTS (
       SELECT 1 FROM viki_memory_edge e JOIN viki_memory n ON n.memory_id = e.newer_id
        WHERE e.older_id = m.memory_id AND n.retracted = 0);
```

Three constraints on this schema, each of which is a trap avoided:

1. **Validity never goes on `viki_chunk`.** A `viki_chunk` row is shared
   between peers by `(content_hash, model_id, chunk_ix)` under D-11. Adding a
   window to it would make the shared row a function of local state and
   fragment the compute-once cache. Validity is a property of the *source*,
   not of the *content* — which is also why two memories with byte-identical
   text correctly share one chunk row and keep separate windows.

2. **The vector leg is never filtered by validity.** `ndvss_cosine_similarity_f()`
   scans embeddings; making that scan depend on local metadata is the same
   mistake in a different place. Validity is a **post-fusion predicate**
   joining `viki_memory` by `content_hash`. Same for BM25.

3. **Filtering changes ranking, so it must be measured, not asserted.**
   Removing superseded chunks raises precision and can *lower* recall when the
   current memory is missing or unindexed. The artifact-coverage audit already
   measured the symmetric effect from the other direction — adding correct
   content cost precision@1 (0.184 → 0.132 over the same 38 queries) while
   raising MRR 0.303 → 0.378. Nobody gets to claim this one for free.

### Epoch status, stated explicitly

**None of this is an epoch bump.** `model_id`, `chunk_params`, `src/tokenizer.c`
and the chunker are untouched; no `content_hash` already in a shared cache
moves; nothing another peer computes changes. What it *is*: a schema-version
bump on `.viki/cache.db` (new tables) and a **frozen text-composition recipe**
for `mem:` sources, which is a cache-fragmenting contract, not an epoch pin —
exactly the distinction the artifact-coverage audit drew for `ckin:`/`note:`.

The one thing here that *would* be an epoch bump, named so nobody proposes it
by accident: making chunk boundaries respect memory boundaries. Chunk params
are in the D-11 pin (D-11/D-12 settled), so a memory-aware chunker is a
fleet-wide re-embed. Not in v1. Short memories (§"envelope") sidestep it.

---

## Multi-agent merge: what breaks

ARCHITECTURE.md step 4 is "Conflicts → branch + ticket, never overwrite." It
has never been exercised with concurrent agents. Everything in this section is
**reasoned from measured mechanics, not observed** — labelled `derived`, per
the ladder above — and the experiment that would settle it is specified at the
end.

**1. Technotes have no merge, and Fossil's fallback is LWW.** Fossil's
crosslink DELETEs the old `type='e'` row and inserts the new
(`manifest.c:2542`, gated on *this artifact has a parent* and *no subsequent
version is already crosslinked*), so two offline edits of one note resolve by
**crosslink order** — the very thing §"Supersession" argues is insufficient.
Worse, `manifest.c:2533` carries Fossil's own comment above the `subsequent`
query: `/* BUG: this check is only correct if subsequent version has already
been crosslinked. */`, so an out-of-order sync or a `fossil rebuild` can in
principle leave a stale row. (Line numbers verified this session against
`vendor/fossil-see/vendor/fossil/src/manifest.c`; the artifact-coverage audit
reported 2531/2543, which is off by two against this pinned Fossil 2.28 — a
small live demonstration of why `provenance: reported` must not be recorded as
`measured`.)
**Decision M-2 (append-only, never edit) is the mitigation**, and this is its
primary justification: it converts a merge problem into a set-union problem,
which fossil sync already solves.

**2. A branch does not fork a technote, a wiki page, a ticket or a forum
post.** Fossil branches are a check-in-level concept; every other artifact
class syncs globally. So ARCHITECTURE.md's "conflicts become a branch" is a
*file* answer that does not apply to any container a memory can live in. The
ticket half still works and is the half this design uses (M-4). **This is an
unstated hole in ARCHITECTURE.md and should be corrected there**, not papered
over here.

**3. `viki cache push` races silently, and D-12 says it must.** uv files are
latest-wins with no history *by design*. Two agents pushing caches concurrently
means one agent's compute-once work is discarded — not corruption (D-10 makes
it rebuildable) but the D-11 claim degrades from "computed once" to "computed
once per race". At the sizes the artifact-coverage audit priced (a
5,000-check-in project ≈ 45 MB uv blob, with no unchanged-content
short-circuit in `fossil uv add`), that race is expensive. Untested.

**4. Sweeping is per-machine authority, and memories make it dangerous.**
FINDINGS.md already records that "a cache pulled from a peer and then
re-indexed in a tree that lacks those files *will* retire their rows." With
memories, an agent that has not pulled sees fewer of them, and — because its
`note:` extractor exited 0 — *claims authority* to retire the rest. The
existing not-authoritative rule does not save this case: it distinguishes "I
could not look" from "I did not see it", and here the agent genuinely looked
and genuinely did not see. **New requirement: never sweep `mem:` unless the
local repo's sync state is at least as new as the cache's.** This is the one
place the design demands a change to existing safety-critical code, and
`sweep_sources()`'s scoping comment must be read before touching it.

**5. `fossil sql` exits 0 when the query fails.** The artifact-coverage audit
measured this and showed `index_forum()`'s documented safety property is
therefore false: a repo that never had a forum post claims authority and
deletes every `forum:` row. Any `mem:` extractor inherits the trap. **The
memory extractor must use the sentinel form** (`… ; SELECT '#viki-eof';`) and
treat authority as *exit 0 **and** last line is the sentinel*.

**6. Clock skew.** `valid-from` is wall-clock from the recording machine. The
supersession DAG is order-independent and therefore immune, but `--as-of` and
"which is newer" are not. v1 orders by DAG first and clock only as tiebreak,
and **prints** a detected disagreement (a successor whose `valid-from`
precedes its predecessor's) rather than resolving it.

**7. Nothing above has been run.** The experiment, specified so it can be
built: two clones, two agents, both offline; both write memories about one
subject; both supersede the same memory; sync in **both** orders; assert (a)
the retrieval result is byte-identical in both orders, (b) both successors are
returned `CONTESTED`, (c) a ticket exists, (d) `viki ask` never returns the
superseded text unflagged. That is a `test/m2-concurrent.sh`, and it is the
definition of done for this part of the design.

---

## v1 / M2 split

**v1 — implementable now, no epoch bump, measurable with the existing harness:**

- the `viki-memory` envelope, frozen; both containers (tech note, fenced block)
- the `mem:` extractor: sentinel-guarded, one `fossil sql` call, `index_forum()`'s
  card parsing with `C`→claim and `unquote_fossil()` (the escaped-title bug is
  the same bug in the same shape — do not repeat it)
- `viki_memory` / `viki_memory_edge` / `viki_memory_about` + the current view
- `viki ask` default as-of-now; `--as-of`; `--history`; `CONTESTED` flag
- `viki remember`, defaults biased to weak provenance, with supersession-
  candidate search
- witness hashes and `STALE-WITNESS` on recall
- `viki why <id>`
- `viki warn --about <subject>` (the verb; not yet the trigger)
- the `mem:` sweep guard (§merge item 4)

**M2+:**

- channel-2 triggering wired into an agent's loop (needs the MCP layer
  ARCHITECTURE.md already schedules for v2, or editor/hook integration)
- `test/m2-concurrent.sh` — the concurrent-agent experiment above
- episode lifecycle (open/close, auto-summary at session end)
- recency or provenance weighting inside ranking — **only** with numbers; the
  eval harness must show it before it lands
- a second harness for channel 2, which the current one structurally cannot
  measure
- cross-repo memory (a memory about project A retrieved while in project B)

**Explicitly not, in any milestone as currently understood:** anything that
changes `viki_chunk`'s key, the chunker, `src/tokenizer.c`, or `model_id`.
Those are epoch bumps and this design does not need one.

### What cannot be built, at any milestone

**There is no `viki forget`.** Fossil history is immutable (D-3's rationale;
US-7: "delete is a tombstone, not destruction"). `retracts` removes a memory
from every validity window and from every default query, and the artifact
remains in the repository forever. That is the correct behaviour for an audit
trail and the wrong behaviour for anything that should never have been written
down, so **memories must never carry secrets** — the same rule `concealed`
already implies for Fossil's own contact table.

---

## What this must be measured against

Per repo culture: a change with no measured gain is a change that does not
land. The baseline this design must move, from `test/retrieval-eval.sh`
(corpus fp `944601a216257d69`, 114 chunks, 59 queries):

| what the design targets | baseline now | mechanism |
|---|---|---|
| `superseded` class, dev, n=5 | r@1 0.200, r@5 0.200 | validity filter; Q23 is the clean STALE_OUTRANKS repro — the superseded answer sits at rank 8 while the current one is not retrieved at all |
| `negative-trap`, dev n=6 / test n=3 | 0.333 / **0.000** at every cutoff | `symptom:` lines (channel 1) |
| `vocab-mismatch`, dev n=4 | r@1 0.000, r@5 0.000 | `symptom:` + `claim:` restated at chunk head |
| `provenance` class, dev n=4 | r@1 0.000, MRR 0.175 | explicit `provenance:`/`evidence:` fields become indexed text |
| index coverage | **0 of 16** un-indexed-artifact queries answerable | `mem:` is a new class; the audit's `ckin:`/`note:` work is the prerequisite |
| regression to watch | overall r@1 0.256 hybrid / 0.302 BM25-only | adding *or* filtering content moves rank 1; both directions measured, neither assumed |

Two harness facts to respect: 18 of the 59 queries are **held out** (`--split
dev` cannot see them), and the corpus is built from this repo's own docs, so
**editing FINDINGS.md or AGENTS.md moves the baseline** — quote the `corpus
fp` with every number.

And one measurement this design owes that no harness yet performs: the
`STALE-WITNESS` predicate should be evaluated against the repo's own history —
replay the memories a 2026-08-13 agent would have written and check that the
54/90 contradiction is flagged. If it is not, this document is wrong.

---

## Open questions

- **OQ-M1: is `about:` a free-text tag or a resolved reference?** Free text is
  cheap and lossy (`viki_ask.c` vs `src/viki_ask.c` vs `viki ask`); a resolved
  content_hash is precise and breaks on every edit. v1 takes free text,
  normalized, and records this as a known weakness.
- **OQ-M2: should `viki index` write memories?** An indexer that notices "this
  file's claim contradicts a current memory" is powerful and is also an agent
  making unsupervised assertions. Leaning no.
- **OQ-M3: episode granularity.** One episode per agent session is obvious and
  possibly too coarse — a long session covers several tasks. Sub-episodes, or
  just more memories?
- **OQ-M4: does the claim line belong in `C` (the timeline comment) when it is
  long?** The timeline is a human surface and design principle 0 says show
  less. A 200-character claim in the timeline is spam.
- **OQ-M5: what happens on `fossil rebuild`?** The technote `event` row is
  reconstructed by crosslink, and `manifest.c:2531`'s own BUG comment says the
  ordering assumption is not guaranteed. Unmeasured. Worth a probe before v1
  ships, because it is the one path that could silently resurrect a superseded
  memory.
