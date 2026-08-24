# VIKIVERSE — backup, relay, sync, serve

Status: **design, 2026-08-21.** Nothing in this file is built unless it
says so. Where something IS built, the test that proves it is named.

This extends `ARCHITECTURE.md`'s hub-and-spoke rather than replacing it.
That document already says every spoke holds a *complete* clone; this one
says what that means when there are several repos, several devices, and an
intermittent connection.

## The result first: the vikiverse is mostly not viki code

Each verb, honestly attributed:

| verb | who does it | viki code |
|---|---|---|
| **relay** | Tailscale (WireGuard mesh, NAT traversal, DERP fallback) | none |
| **sync** | Fossil (`fossil sync`, `fossil uv sync`) | none |
| **backup** | whole-repo clones — every spoke is a replica | verification only |
| **serve** | `viki serve`, local by default | finish the API it has |
| **model** | one per machine, its own repo | fix a duplication defect |
| **caching** | per-repo policy | the one genuinely new thing |

If this table stops being true — if a relay starts needing to understand
viki, or sync grows a custom protocol — that is the signal to re-open the
question of whether the vikiverse is a separate project. Today it is a
subcommand and some `server/` ops, and it should stay that way. See
"Separate project?" below.

## The constraint that determines the shape: who holds PLAINTEXT

**An earlier draft of this file said the axis was "who holds the key."
That was wrong in both directions, and two independent reviews caught it
from opposite sides.** The corrected version:

- **A Fossil sync server MUST hold the key.** It cannot answer a sync
  request without running SQL against `blob`/`delta`, which means opening
  the SQLCipher database. `ENCRYPTION.md`'s "Bug found & fixed" section
  exists precisely because `cmd_webserver` failed to obtain the key and
  "every open fails SQLITE_NOTADB"; the fix was to give the server one, and
  it is provisioned via systemd `LoadCredential`. So **sync is not
  key-free**, and the node most wanted as a custodian — the rendezvous
  hub — is the one node that cannot be one.
- **A viki reader needs NO key.** `FOSSIL_SEE_KEY` appears exactly **once**
  in all of `src/`, in a comment on the optional in-process indexing path.
  `viki_db.c` opens `.viki/cache.db` with a plain `sqlite3_open_v2`.
  Verified directly: with no key set, no `fossil` on PATH and no network,
  `viki index` + `viki ask` return the planted answer at rank 1.

So the key protects the `.efossil` **at rest, and nothing else**. What
actually exposes content is the **plaintext cache**, and it is key-free.

### And the hub as scripted today is not encrypted at all

An earlier draft said "the hub holds a `.efossil` — ciphertext at rest,
asserted by `test/m1.sh` E1/E3." **That citation was wrong in scope.**
E1/E3 assert about a scratch repo the test itself creates with
`fossil-see`; they say nothing about a deployed hub. And
`server/setup-hub.sh` installs **apt `fossil`**, creates `pm.fossil`, and
sets no key anywhere — a stock fossil cannot open a SQLCipher repo at all.
By this project's own control (m1's E2), a `.fossil` name means plaintext.

`ENCRYPTION.md` already lists the deployment deltas needed to close this
("use fossil-see", "name repos `*.efossil`", "key via `LoadCredential`");
they have not been folded in. **So a VPS provisioned today is a trusted
reader storing plaintext**, and anyone reading the tier table above without
this paragraph would believe otherwise. Filed as QUEUE §37.

### The cache is the crown jewel, not the key

`.viki/cache.db` is unencrypted by design (D-10), holds `chunk_text` verbatim,
and — for the six artifact classes that never exist as checkout files
(`wiki:`, `ticket:`, `tchg:`, `forum:`, `ckin:`, `note:`, `attach:`, `uv:`) —
is the **only plaintext copy on disk anywhere**. Anyone who obtains that
one file has the entire corpus, with no key and no fossil binary.

`ENCRYPTION.md`'s threat model has a row for plaintext *checkout files*
and no row for this. That is the gap.

### Three roles, not two

| role | holds | zero-knowledge? |
|---|---|---|
| **blob custodian** | `.efossil` files it never opens | **yes** — storage box, S3, a friend's disk |
| **relay** | packets in flight, nothing at rest | **yes** — Tailscale, Caddy |
| **sync peer / reader** | the key, or the cache, or both | **no** — fully trusted |

"Any VPS, any friend's box, zero trust required" is true **only** of the
blob-custodian role. A host running `fossil server` is a sync peer. A host
running `viki serve` is a reader. Both are trusted.

### And today one host is all three

`server/setup-viki-serve.sh` refuses to run unless the hub's Caddyfile
already exists — so it runs **on the hub host** — and it requires an open
Fossil *checkout*, chowns it, and points the service at it. That host then
holds the repo key, a plaintext checkout, and a plaintext cache.

This directly contradicts `ENCRYPTION.md`'s own mitigation, which reads
"server holds no checkouts (repos only — full coverage there)". That
sentence becomes false the moment `setup-viki-serve.sh` runs.

**Rule that follows: do not run `viki serve` on a host you are treating as
a custodian.** If remote search is wanted, it belongs on a different host
from the sync hub, so one compromise does not yield both roles.

### `viki cache push` publishes the corpus to anyone with `Read`

`viki cache push` does `fossil uv add <cache> --as viki-cache.db`. Fossil
serves `/uv/FILE` behind a single check — `g.perm.Read` — and `/uvlist`
enumerates names and sizes behind the same one. `SERVER_SETUP.md` offers
anonymous read-only browse as a configuration option.

So on a repo with public browse enabled, **the entire plaintext corpus is
one unauthenticated HTTPS GET**, and encryption at rest is irrelevant
because the server decrypts and serves. Two things limit the blast radius
today, and both should be stated rather than relied on: plain `fossil
clone` does not carry unversioned content, and uv is latest-wins with no
history.

**The pushed cache needs the HIGHEST capability in the repo, not the
lowest.** Filed as QUEUE §35.

## The repo is the unit of everything

Caching granularity is the **whole repo**. Not a chunk, not a document.

This is not a simplification for its own sake — the repo already carries
four other meanings, and adding a fifth boundary would have been the
mistake:

- **encryption** — one repo, one `FOSSIL_SEE_KEY`
- **access control** — the hub runs `--repolist`; separate user tables and
  capabilities per repo (`server/SERVER_SETUP.md`)
- **sync** — `fossil clone`/`sync` operate on a repo
- **distribution** — `viki cache push/pull` is already whole-cache

It is also what Fossil was built for: a clone is complete by design;
partial checkout was never the model.

**What this buys, and it is the main reason to prefer it:** there is no
"30% of the corpus" state to detect. A partial cache would return a
confident, complete-looking ranking over whatever happened to be local,
and this project has already established that no corpus-independent
confidence threshold exists to catch that (FINDINGS.md: a correct
retrieval scored 0.1859 while a garbage query scored 0.2174). Whole-repo
granularity turns completeness from a hidden statistical property into an
**enumerable fact**.

### The residual, which is real but cheap

With several repos, a query spanning five while you hold two still ranks
confidently over the two. That is now a list rather than an inference, so
the fix is a line of output, not a mechanism:

    answered from: pm, monument-rock
    not local:     icpc, field-2026

**NOT BUILT.** `viki ask` reports nothing about which repos it consulted,
because today it consults exactly one cache. This is the first thing the
vikiverse needs and the only part that can silently mislead. See
"First task" below.

## Caching policy: `{none, optional, required}`

Per repo, per device.

- **`none`** — transient. Query a remote trusted reader; store nothing.
  A borrowed machine, a short-lived container, a shell on someone else's
  box.
- **`optional`** — laptop. Clone and cache when it is useful and the
  bytes are available; tolerate not having it. The promotion trigger
  (a use cap, say) lives here.
- **`required`** — offline. A phone in a truck with no signal. The clone,
  the cache and the model must be present, and their absence is an error
  rather than a fallback to remote.

**An earlier draft claimed `required` "already works end to end" and cited
M1–M9. That overclaimed, and a field review took it apart.**

What M1–M9 actually prove is **D-12 blob integrity and compute-once**: push
writes three uv blobs under exact names, pull materialises exactly those
three byte-identically, both are sha256-checked against the manifest pin,
and a clone that never ran `viki index` answers in hybrid mode with the
planted document at rank 1. That is a real and valuable proof.

What they do **not** prove, and the gap is the whole `required` tier:

- **Nothing is ever disconnected.** The "fresh clone" is a local `fossil
  clone` from a hub on the same filesystem, and the hub stays present and
  reachable for every assertion including the retrieval one. No assertion
  removes it and re-asks. M9 proves *no HTTP peer was configured*, which
  is not the same as *worked while offline*.
- **One machine, one key, one tree.** No second device, no key
  provisioning, no per-device key — which `ENCRYPTION.md` says is the
  actual model.
- **The seed model came from the internet**, via `build/build.sh`. M9
  proves the *clone* fetched nothing; it says nothing about how the first
  model reached the system.
- **The whole group is conditional** on `HAVE_MODEL`; without a model it
  skips and the run still exits 0.
- **Subprocess-only throughout** — see the next section.

So `required` is the **least**-designed tier, not the most-proven one.

### Three field gaps that need no networking to fix

Found by role-playing the phone-in-a-truck case against the actual code.
None of these needs Tailscale, multi-repo coverage, or the model fix this
document leads with:

1. **Captures never become searchable on a device with no shell.**
   `/api/capture` returns `reindex_required`, and `/api/reindex` rebuilds
   only the `viki_note` projection — its own comment says "chunk
   re-indexing stays a deliberate `viki index` from a shell." Verified.
   On a phone there is no shell, so `viki ask` can never reach anything
   captured today. **This is US-3, the killer story, broken on the target
   device.**
2. **`viki cache pull` is online-only even when the bytes are already
   local.** It runs `fossil uv sync` unconditionally and returns on
   failure, before ever attempting `uv export` — which reads the *local*
   repo. Recovery therefore fails in the field even when nothing is
   missing. Roughly a five-line fix: try export, sync only if it fails.
3. **Every failure looks identical.** An absent cache is silently created
   empty (`SQLITE_OPEN_CREATE`), and zero results prints `(no matches)`.
   "The cache never arrived", "the sync is a week stale" and "I never
   wrote that down" are byte-identical outputs. In the field the third is
   the one you believe, and you drive away.

(3) is the single-repo form of the completeness problem this document
already raises for multiple repos — and it is far likelier on a new
device. Filed with the rest as QUEUE §36.

What is new for the vikiverse proper is `none` (query remote, store
nothing) and `optional` (decide when to promote).

## The model factors out — and today it does not

**This is a live defect, not a future refinement.**

D-11 pins ONE rung-2 model, universal across peers. That universality is
what makes an embedding a deterministic function of `(content_hash,
model_id, chunk_params)` — computed once by whoever sees the content
first, then shared. D-12 then distributes that model as `fossil uv` files
**inside each repo**:

    viki-model/model.onnx          (~23 MB)
    viki-model/vocab.txt
    viki-model/viki-manifest.json

Each decision is sound alone. They compose badly at N > 1: five repos
means five copies of an artifact D-11 defines as a singleton. A todo list
carrying its own copy of a universal model is the tell.

**The cost is at-rest duplication, not bandwidth** -- a distinction
surfaced by musing over these docs rather than reasoning about them.
FINDINGS.md records that `viki_cache.c` already skips all three model
blobs when the published manifest matches the local one, and that fossil
compares hashes before shipping, so a repeat push of an unchanged model
costs 586 wire bytes. What is real is the first push per repo (~23MB
each) and the copy every repo keeps forever, on the hub and in every
clone of it. Argue this on storage and on D-11/D-12 coherence; do not
argue it on bandwidth.

**The machinery is already most of the way to the fix.**
`$VIKI_MODEL_DIR` is a single global pointer, so at *runtime* the model is
already shared across every repo on a machine. `viki_chunk.model_id`
already names the model rather than embedding it, so a cache already
refers to a model it does not contain. `viki cache push --no-model`
already exists.

What is wrong is the **default and the bootstrap**: the model should be a
machine-level artifact, fetched once, living in its own repo exactly as
`me.viki` is its own repo — and per-repo pushes should carry `model_id`
only. That is a polarity flip plus a bootstrap path, not new mechanism.
It also makes the epoch story cleaner: one place to pin, verify and roll
forward, instead of N manifests that can disagree.

Filed as QUEUE §34.

## Serve locally by default — it is the cheapest transaction

The vikiverse prefers a **local** `viki serve` and treats remote as the
priced exception. A long-lived local server holds the cache open, the
model loaded, and — with `libfossilsee` — the repo open with no per-call
SQLCipher KDF. Cost per transaction goes to roughly zero. Every remote
transaction costs latency, bandwidth and possibly money.

This is also the justification `libfossilsee` was missing. When it landed,
the honest note was that its beneficiary is a long-lived host process,
"which viki does not yet have" (FINDINGS.md). A local-server-by-default
vikiverse **is** that host.

**Consequence for priorities:** `viki serve` has no `/api/grep` and no
`/api/muse` (QUEUE §31). If local serve is the default transaction path,
those are not missing conveniences — they are holes in the transaction
path itself, and every agent and UI pays for them.

## Backup is replicas plus a claim nobody checks

DVCS gives replication for free: every spoke is a complete copy, and
`server/setup-hub.sh` adds nightly `fossil backup` with 7-day rotation and
optional Storage Box rsync.

Two things are missing, and an earlier draft named only one of them.

**Mechanism, first.** `fossil clone` does **not** carry unversioned content
unless asked (`-u`), and `viki_cache.c` says so in its own comments. So
spokes replicate *artifacts* and do **not** replicate the cache or the
model — which means the hub is the only copy of precisely the things the
vikiverse exists to distribute. "The hub is also the backup"
(`ARCHITECTURE.md`) is therefore circular for the uv layer specifically.
Worse, `setup-hub.sh` wrote its snapshots to a directory on the **same
filesystem** as the repos, with the off-box rsync commented out. That is now
fixed: both suffixes are globbed, `set -eu` is on, a missing off-box target
warns loudly, and a `LAST_SUCCESS` stamp exists so a backup that has been
failing for six months no longer looks identical to one that works.

**Verification, second.** Nothing yet answers "is this repo on at least N
machines, and is each copy readable?" An unverified backup is a belief —
and note that a real verifier must check *uv content*, not just that the
repo opens, since the two things clones do not carry are exactly the things
a repo-opens check cannot see. This is small — enumerate peers, check
each holds the repo, check it opens with the key — and it belongs with the
repo-coverage work, since both are the same question ("what do I actually
have?") asked from different sides.

## Separate project?

**No — subcommand plus `server/` ops.** The test is dependency direction
and release cadence, not size:

- everything points one way (the vikiverse uses viki; viki does not use
  it),
- there is one hub script and no second topology proven,
- nothing yet needs an independent release cadence,
- and this repository has documented claim-rot from one fact living in two
  files **three separate times** (CLAUDE.md). A second repo multiplies
  precisely that failure.

Split when there is a second consumer or a second cadence. Not before.

## The sharing contract has an unpinned component, and N peers is where it bites

D-11 says an embedding is a deterministic function of `(content_hash,
model_id, chunk_params)`, computed once and shared. But `content_hash =
sha256(the composed extracted text)`, and **the composition recipe is not
in `viki-manifest`** — CLAUDE.md states this outright:

> two peers that compose a check-in header differently produce different
> hashes for the same check-in and both end up in `viki ask`. That is
> cache *fragmentation*, not corruption, and not an epoch bump — but the
> header formats are frozen, and changing one must be called out as
> cache-fragmenting.

On one machine that is a re-index. **Across a vikiverse it is silent
divergence between peers running different viki versions**, and nothing
detects it: the manifest pins `model_id` and the two model blob hashes,
so two peers can agree on every pinned field and still disagree on what
`content_hash` a given check-in has. Both sets of rows then coexist in a
shared cache, each looking perfectly valid.

This is not a new defect — it is a known, accepted single-node property
(RETRIEVAL_PLAN.md records the same thing) whose blast radius grows with
peer count. Two consequences for this design:

- The manifest should eventually pin a **composition-recipe version**
  alongside `model_id`, so peers can detect disagreement instead of
  merging silently. Not scoped here.
- Until it does, "frozen header formats" is a **distribution invariant**,
  not a style rule. Changing one is a vikiverse-wide event.

(Also found by `viki muse`, seeded on the model section of this file.)

## The tension repo-as-unit creates, which was already known

Making the repo the unit of caching **hardens** a boundary that
`MEMORY_DESIGN.md` already lists as something to eventually cross:

> cross-repo memory (a memory about project A retrieved while in project B)

It sits there among deferred items, in a list whose rule is that the eval
harness must show a gain before anything lands. So the vikiverse does not
create this problem, but it does make it more permanent: if a query only
ever sees one repo's cache, a memory about project A is structurally
unreachable from project B, and the "repo coverage" line above *reports*
that boundary rather than crossing it.

Two things follow. First, repo coverage is the honest minimum and should
ship regardless. Second, whatever eventually crosses repos will need a
harness that can measure it -- `MEMORY_DESIGN.md` notes the current one
"structurally cannot" -- so cross-repo retrieval is a measurement problem
before it is a distribution problem. Not scoped here; recorded so the
decision is not made by accident.

(Found by `viki muse` seeded on this document, which is the tool doing
what it is for: an undirected connection nobody asked for.)

## First task, and it is deliberately not networking

**Can a query answered from a partial view be distinguished from a
complete one?** Today, no — and that is testable without any of the above.

Build a full cache and a subset cache of the same corpus, run
`test/retrieval-eval.sh` queries against both, and check whether *anything*
in viki's output tells them apart. Expectation: nothing does.

If that confirms, the fix — a completeness signal on every answer — is
something viki wants whether or not the vikiverse ever ships, and it is a
prerequisite for `optional` and `none` being honest rather than
convenient.

This ordering is deliberate. The two things that went right today
(`libfossilsee`'s equivalence probe, the roleplay round) were proofs
first; the thing that went wrong (a vector-engine swap argued for rather
than measured) was not.

## Open questions

- **Promotion trigger for `optional`.** "A use cap" is a placeholder. Use
  count? Age? Explicit pin? Unknown, and cheap to defer until `none` and
  `required` are real.
- **Multiple keys per device.** One `FOSSIL_SEE_KEY` env var, several
  repos. Is the key per-repo in a keychain, or is a device single-tenant?
  Unresolved, and it constrains how many repos a phone can hold.
- **Tailnet identity vs Fossil accounts.** Both authenticate. Do they
  compose, or does one become decorative? `ARCHITECTURE.md` says "an agent
  is just a Fossil user account"; Tailscale would authenticate the device
  underneath that.
- **Does a custodian ever prune?** A dumb custodian cannot read what it
  stores, so it cannot know what is worth keeping. Retention has to be
  driven by a trusted reader or by policy, never by the custodian.


## The edge, and the six-platform question (2026-08-23)

Warren: *"now can dart/flutter be the portable ui for me? (mac, web, ios,
android, windows, linux)"*. Yes — and the day's work decided the shape rather
than leaving it open.

### What the edge already is

`viki edge` is the `caching = required` tier made real: viki's retrieval core
compiled to WebAssembly, reading a SQLCipher cache someone else built, offline
after one pull, holding several separately-keyed tribes at once. It is proven
to reproduce the native binary's ranking and rrf scores **exactly**, because it
IS the native code — `viki_ask.c` and friends, unmodified.

### The property that must survive any UI

`viki_ask_query()` is the single implementation. The CLI, `/api/ask` and the
wasm edge are all thin wrappers, which is why they cannot drift. **A Dart
reimplementation of retrieval would end that on day one** and no amount of
tests would hold it together afterwards. So the rule for any Flutter work is:
Dart draws, C retrieves.

### Two integration paths, not six

Flutter covers all six targets, but not by one mechanism:

| target | how Dart reaches the core | can it host a real Fossil repo? |
|---|---|---|
| macOS, iOS, Android, Windows, Linux | `dart:ffi` → the C core, per-platform build | **yes** — filesystem and sockets |
| web | JS interop → the wasm module already built | **no** — no sockets, no fork |

`dart:ffi` does not exist on Flutter Web, so the browser leg keeps the wasm
module. That is not duplicated work: it is the same C, reached differently, and
one Dart interface with two implementations behind a conditional import is an
ordinary Flutter pattern. The wasm build stops being a side quest and becomes
the web backend.

### The platform split IS the caching-tier split

This is the useful coincidence. Web cannot hold a Fossil repo — no sockets, no
fork — so it is permanently a read-only projection consumer: pull a cache, ask
questions, never be a peer. The five native targets *can* host a repo, which is
exactly what "edge viki will be fossil, so it should sync when it can" needs.

    web            → caching=required, snapshot pull, zero install, no sync
    native (5)     → caching=optional or required, a real Fossil peer, syncs

So the snapshot puller built today is not a stopgap that gets thrown away. It
is the permanent web tier, and the degenerate case for any native device that
chooses not to host a repo.

### What Flutter buys that the browser cannot

Worth being concrete, because "it works in a browser already" is a fair
objection:

- **A real Fossil repo**, hence real sync. Decisive on its own.
- **The OS keystore** — Secure Enclave, Android Keystore, TPM — which is the
  `device_secret` that `identity.db` already has a slot for (QUEUE 49). WebAuthn
  PRF gets a browser most of the way, but the native path is better and more
  universal.
- **Storage that is not evictable.** OPFS can be reclaimed under pressure; an
  app's files cannot. For a `required`-tier device that is the difference
  between offline and usually-offline.
- Background sync, notifications, and file-system access to a checkout.

### The honest cost

Five native targets means cross-compiling SQLCipher, LibreSSL and ONNX Runtime
for each. None is novel — all three ship for all five — but it is a real build
matrix, and ONNX is the heavy one. The browser edge remains *one artifact* that
runs everywhere with no store, no signing and no matrix, which is why it should
stay even after Flutter exists.

### Recommended order

1. Keep the browser edge as the zero-install surface. Done.
2. Flutter UI over `dart:ffi` on **one** native platform first — macOS or
   Android — to find the FFI shape before multiplying it by five.
3. Add the OS-keystore `device_secret`, which the identity schema already
   accommodates without a format change.
4. Only then the remaining natives, and only then a real Fossil peer.

Step 2 is where the design risk lives. Everything after it is repetition.
