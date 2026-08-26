# SCOPES — what is viki, and what merely uses it

**Status: DRAFT for Warren's correction.** Written because the same boundary got
crossed twice in one day, which is a naming problem rather than a discipline
problem.

> *"the foundational tool is private distributed optionally offline state
> management. this should include mcp / cli / api interfaces. producers and
> consumers — agents and humans and robots work directly or through connectors,
> or whatever resources they have (openclaw, nanoclaw, claude cowork)."*
> — Warren, 2026-08-25

---

## 0. The problem, concretely

**"viki" currently names six different things**, and which one is meant has to
be inferred every time: the C binary, the retrieval core, the cache format, the
CLI, the HTTP API, and the whole system colloquially. "vikiverse" names three:
the network of tribes, the product, and a domain.

That ambiguity has a cost, and it has already been paid twice:

- `d2l.coloradomesa.edu` was written into viki's checked-in manifest — a campus
  hostname inside the substrate.
- `viki brief` shipped as a viki subcommand, deciding what counts as "at risk"
  — judgment inside a tool that has none by design.

Neither was carelessness. Both happened because **there was no name for the
thing being violated.**

**And the tree still shows it.** `edge/` holds three unrelated layers:
`edge/tools/viki-identity.c` and `viki-key-wrap.c` are **key custody** — as
foundational as anything in `src/` — sitting under a directory named for the
read-only wasm tier, next to `chrome/`, which is a connector.

---

## 1. Two corrections, and the second undoes half of the first

### 1a. The foundation is STATE MANAGEMENT, not retrieval.

This document previously behaved as though viki were "the thing that does
semantic search". That framing puts the boundary in the wrong place, and it is
the root of both violations above. Retrieval is *one projection over state*. So
is the promise ledger. So is coverage. Name the foundation after one projection
and every other projection looks like a stranger.

State is: **private** (SQLCipher end to end), **distributed** (Fossil sync),
**optionally offline** (`caching = {none, optional, required}`), and
**content-addressed** (a hash is the citable identity).

### 1b. But STATE INCLUDES SEARCH — reduction by question is not a layer above it

> *"state includes search — there must be a way to reduce a state by a question,
> otherwise retrieval becomes impossible."* — Warren, 2026-08-25

The first draft of this file put retrieval in L1 and state in L0, which draws
the line in a place that cannot hold. **A state store you cannot reduce by a
question is a bucket, not a store.** SQL is the canonical case: a database is
state *and* query, and nobody calls the query engine a projection over the
tables.

So the levels below are **not** state-then-search. They are one store with a
**gradient of abstraction over the same reduction**:

    RAW        direct SQL and vector queries.  Full power, no opinions.
    CURATED    ask, grep, muse, promises, why, coverage.  Convenience.
    DELIVERED  cli · http api · mcp · wasm.  How either reaches a caller.

**Agents belong at RAW, and until 2026-08-25 they could not get there.** A stock
`sqlite3` opening `.viki/cache.db` gets `no such function:
ndvss_cosine_similarity_f` — the vector engine is statically linked into viki
and registered at open, so viki's own process is the only place a vector query
can run. Without a sanctioned entry point the raw layer was not inconvenient, it
was *unreachable*, and `ask` was the only way to ask anything. `viki sql` exists
because of this correction.

**The curated verbs are conveniences, not gatekeepers.** `ask` is a good default
and must never be the only door. An agent that wants a different fusion, a
different filter, or a join viki never anticipated should write the query, and
the schema is the contract.

---

## 2. Four levels

### L0 — STATE. The truth.

Encrypted Fossil repos and their unversioned blobs; the keys that open them.
Append-only, synced, survives being offline. Nothing here interprets anything.

    fossil-see repos · uv blobs · identity.db · tribe keys · the tribe registry

### L1 — PROJECTIONS. Derived, disposable, rebuildable (D-10).

Everything computed *from* state, **including the structures that make search
possible**. Never truth; deletable at any time; rebuilt by re-indexing.

    the chunk index + embeddings · the note ledger · source coverage
    the FTS index · viki_source liveness

Note the distinction §1b forces: the *index* is a projection, but the *ability
to reduce state by a question* is not a layer — it is what L0 and L1 are for.
Deleting the index costs speed and recall; it does not turn viki into something
that cannot answer.

### L2 — INTERFACES. Peers, all thin.

Faces on L0+L1. **These are peers, not a hierarchy** — that is the part this
project has wrong today. The CLI is primary and the HTTP API is a wrapper
around the same core, which is right; MCP must join as a *third face on the
same functions*, not as a wrapper around the CLI.

    RAW      viki sql (direct SQL + vector, read-only)
    CURATED  ask · grep · muse · promises · why · coverage · capture
    FACES    cli · http api · mcp (not built) · wasm edge (read-only)

Every face should carry both rungs. An MCP server exposing only `ask` would
recreate exactly the gate §1b just removed.

The rule that keeps them honest: `viki_ask_query()` is the single
implementation, and every face is a thin caller. When two faces can disagree
about an answer, the layering has failed.

### L3 — CONNECTORS. Producers and consumers. **Not viki.**

    producers  browser readers · calendar/mail ingest · captures
    consumers  the morning brief · agents · openclaw / nanoclaw / claude cowork

An agent, a human, or a robot reaches L2 **directly** or **through a connector**
— whichever it has. viki does not care which, and must not know.

---

## 3. The placement test

Four questions, in order. The first "yes" is the level.

1. **Is it truth that must survive a disk dying?** → L0
2. **Can it be deleted and rebuilt from L0?** → L1
3. **Does it expose L0/L1 without deciding anything?** → L2
4. **Does it decide, judge, ask, or reach outside?** → **L3, and not viki**

The sharpest form, and the one that catches real mistakes:

> **Can viki compute this without an opinion?**

`coverage` prints last-seen times and no thresholds — L1. The moment something
needs a number like "12 hours" or a word like "at risk", it is a decision, and
decisions are L3.

And the tell that settles arguments: **viki cannot ask questions.** It has no
LLM and never will. Anything that asks one is L3 by construction.

---

## 4. Scope names, so the ambiguity stops

| term | means, exactly | does **not** mean |
|---|---|---|
| **viki** | the L0–L2 tool: state, projections, and the three faces | the assistant; the readers; the network |
| **a tribe** | one L0 instance — a repo plus the accounts that may reach it | a person; a device |
| **the verse** | the set of tribes reachable from one device | a federation between tribes (out of scope for v1) |
| **the edge** | the read-only L2 face for a device that cannot host a repo | the browser reader |
| **a connector** | anything at L3 producing into or consuming from viki | part of viki |
| **vikiverse** | the product and the domain — viki plus its assistant and connectors | the C tool |

---

## 5. What this says about MCP

Under §2, MCP is **not** an integration to be built later on top. It is the
**third peer interface**, and its absence is why every consumer today must
either shell out to the CLI or speak an HTTP API with no contract.

The tools it should expose map exactly to L1 projections and read like the
placement test predicts — every one a query, none a judgment:

    ask · grep · muse · capture · promises · why · coverage · since

That is §2.11, and it is what makes "adapt, don't invent" (QUEUE 51) real:
nanoclaw, openclaw or claude cowork consume viki without knowing Fossil,
SQLite, or a Chrome extension exists.

---

## 6. Concrete misplacements, found by writing this down

| what | where it is | where it belongs | why |
|---|---|---|---|
| `viki-identity.c`, `viki-key-wrap.c` | `edge/tools/` | **L0** | Key custody is truth. It is not an edge concern, and it is needed by any peer that touches an encrypted tribe. |
| `viki-cache-encrypt.c` | `edge/tools/` | **L0** | Converts state at rest. Same argument. |
| `viki-http.c` | `edge/tools/` | L0 support | A TLS client used by the puller. Nothing edge about it. |
| `edge/chrome/` | `edge/` | **L3** | A connector, sitting under a directory named for an interface. `sites/` already carries the "yours, not viki's" note, which was the first half of this fix. |
| `assistant/` | top level | **L3** | Correctly placed already, and the model for the rest. |

**Proposed:** `edge/` keeps only the wasm read-only face (L2). Key custody moves
to something L0-shaped; the browser reader moves next to `assistant/` as the
other half of L3.

**Not done yet — this is a proposal, not a refactor.** Moving files breaks
paths in CLAUDE.md, AGENTS.md, four probes and two build scripts, and it is
worth doing in one deliberate commit rather than piecemeal.

---

## 6b. The canonical shape: robot → agent → viki

> *"a robot scrapes outlook emails, passes them to an agent for
> categorization/tagging, and stores the 'not junk' versions in the viki
> personal digital assistant tribe for search and retrieval. That last story is
> specific to me. vikiverse is a generalized tool for this kind of problem."*
> — Warren, 2026-08-25

    ROBOT            scrapes.  Mechanical, no judgment.       L3 producer
      ↓
    AGENT            categorises, tags, discards junk.        L3
      ↓
    viki (a tribe)   stores what survived; answers about it.  L0–L2
      ↓
    ACTIONS          search, retrieval, the brief.            L3 consumer

**The judgment happens BEFORE storage, and that is the load-bearing detail.**
viki is not filtering the junk — an agent already did, and viki holds the
result. Which is why viki needs no opinion about what junk is, and why a
`--junk-threshold` flag would be a scope violation rather than a feature.

It also explains why connectors are **agent-specific** rather than viki
plugins: the robot that scrapes Outlook and the agent that tags it are Warren's
choices about Warren's mail, and someone else's tribe will use different ones
for the same reason their calendar is not his. That is what "leverages other
toolchains" buys — nanoclaw already has the robots.

**And it settles §7.3:** this pipeline is Warren's instance; **vikiverse is the
generalized tool for the class of problem.** So `vikiverse` names the product,
and the network of tribes needs its own word or none at all.

---

## 7. Open questions

1. **Is "viki" the right name for L0–L2 together**, or does the state layer
   deserve its own name so "viki" can mean the projections and faces? The
   argument for splitting: L0 is useful to something that never does retrieval.
   The argument against: one more name to keep straight.
2. **Does `assistant/` stay in this repo?** It is L3 and explicitly not viki.
   Keeping it here makes the boundary legible; moving it out makes it
   enforceable.
3. ~~Is `vikiverse` the product name or the network name?~~ **Settled 2026-08-25:
   the product.** "vikiverse is a generalized tool for this kind of problem."
   The set of tribes on one device is *the verse*; whether that word survives
   contact with anyone but us is a separate question.
4. ~~**Should `viki sql` be exposed over HTTP and MCP too?**~~ **Settled
   2026-08-26 (V2_DESIGN.md §6b): no, not on the MCP face.** The argument that
   closed it is not the loopback one this question was resting on — that was a
   perimeter argument, and §0b of SYNC.md is the reason perimeter arguments are
   weak here. It is **aggregation**: `ask --k 5` returns five chunks, `sql`
   returns the corpus in one call. Against a prompt-injected client that is the
   difference between leaking a snippet and leaking everything, so the raw rung
   stays where its caller is a person or an agent already holding the key.
   §2's "every face should carry both rungs" is therefore **amended**: it holds
   for faces whose caller already has the key, and not for a face built to be
   driven by untrusted text.
