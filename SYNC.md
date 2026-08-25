# SYNC — what a tribe may carry, and what it must refuse

**Status: DRAFT for Warren's correction.**

> *"the vikiverse can deal in raw state — agnostic sqlite blobs — but there is
> the sync question. what should be allowed?"* — Warren, 2026-08-25

If the vikiverse carries arbitrary SQLite blobs, the sync question is not "how
do we merge them" — it is **which blobs have a merge at all**. Most do not, and
the ones that do not fail *silently*, which is the reason to answer this before
anyone pushes a second blob.

---

## 0. Two measured facts that constrain everything below

### Fossil's `uv` is latest-wins, by mtime, across peers with unsynchronised clocks

There is no merge. A later push replaces the blob whole. For a *derived* blob
that is harmless — recompute it. For anything holding truth it is a **silent
lost-update generator**, and "latest" is decided by clocks that disagree
(QUEUE 38).

### Encrypted SQLite blobs cannot be diffed, deduped, or delta-synced

Measured 2026-08-25 — the same source database, encrypted twice with the **same
key**:

```
$ viki-cache-encrypt src.db e1.db "$KEY"
$ viki-cache-encrypt src.db e2.db "$KEY"
$ cmp e1.db e2.db
  DIFFERENT bytes
  20515f711114d0e0…
  ebaad70e78fbd2f1…
```

SQLCipher salts each database independently, so identical content under an
identical key produces entirely different ciphertext. Consequences, all
unavoidable:

- every sync of an encrypted blob is a **full transfer**; there is no delta
- byte-equality proves nothing about content-equality
- content-addressing a blob by its ciphertext hash is meaningless

**This is the strongest argument for keeping truth in Fossil artifacts rather
than in blobs.** Artifacts delta-compress, merge, and carry history. Blobs do
none of those, and encrypted blobs cannot even be compared.

---

## 0b. THIS IS A SAFETY POLICY, NOT A SECURITY BOUNDARY

> *"fossil artifacts seem like the defensible layer. it's why fossil seems like
> a foundation. anyone linking libfossilsee will have the sqlite api to break
> synchronization. the rest is security theater."* — Warren, 2026-08-25

Correct, and it applies to code committed an hour before he said it. Everything
below prevents **mistakes**. Against a tribe member it prevents nothing at all,
and describing it otherwise would be exactly the theater he names.

**A tribe member holds the key.** They can open the repo with SQLCipher
directly, link `libfossilsee`, or just run `fossil` — and write anything they
like into `blob`, `unversioned`, or `event`. `viki_cache_refuse_private()` stops
a careless script and a confused agent. It does not stop a person, and it was
never going to.

The distinction worth keeping:

| against | what actually protects |
|---|---|
| a **non-member** | SQLCipher. Real, and the only real one. |
| a **member** | nothing at the viki layer. They have SQL. |
| an **accident** | policy like §2. Worth having, worth naming correctly. |

### And the asymmetry that makes artifacts the defensible layer

This is the technical content of Warren's point, and it is not a preference:

- **Fossil artifacts are content-addressed and Merkle-linked.** A manifest names
  its blobs by hash; the sync protocol exchanges artifacts *by hash*. Tampering
  is therefore **detectable by any peer** — the hash simply will not match.
- **`uv` blobs are name-addressed and latest-wins.** There is no hash in the
  protocol and no chain. A receiving peer **cannot distinguish a legitimate new
  version from a substituted one.**

So the layer that is defensible is the layer whose integrity is checkable
without trusting the sender. That is artifacts, and it is why Fossil is the
foundation rather than an implementation detail.

### The circular verification this exposes in viki, today

`viki cache pull` prints `model.onnx sha256 verified against the epoch pin`.
The pin is `viki-manifest.json` — **which is itself a uv blob**
(`aModelFile[]`, `viki_cache.c:70`).

    viki-model/model.onnx    uv blob, replaceable
    viki-model/vocab.txt     uv blob, replaceable
    viki-model/viki-manifest.json   uv blob, replaceable  <-- attests the two above

An attacker who can replace the model can replace the manifest that vouches for
it, in the same push. The check catches **corruption and accident**, which is
worth having and is what D-12 needed. It is not an integrity guarantee, and the
log line reads as though it were.

**The fix follows directly from the asymmetry:** anchor the blob hashes in a
**versioned artifact**. Commit `viki-manifest.json` to the repo, and have `pull`
verify the uv bytes against the *committed* copy rather than the uv copy. Then
the Merkle chain attests the blobs, the cheap latest-wins channel still carries
the bytes, and "verified against the epoch pin" becomes true rather than
circular.

**Not done — it changes D-12's distribution contract** (the manifest stops being
purely unversioned) and that is Warren's call, not a patch to slip in.

---

## 1. The test: is the merge decidable *without understanding the data*?

The substrate must not need to know what a table means. That is only satisfiable
when rows are **immutable** and **keyed by content** — then union *is* merge:
order-independent, idempotent, commutative. A grow-only set.

viki's own cache passes by construction rather than by luck. The chunk key is
`(content_hash, model_id, chunk_ix)`, and D-11 makes an embedding a
deterministic function of exactly those — so two peers that see the same
content compute *identical rows*, and the merge is:

```sql
INSERT OR IGNORE INTO viki_chunk(content_hash, model_id, chunk_ix, chunk_text, embedding)
  SELECT … FROM inc.viki_chunk;
```

No conflict is possible, because a collision means the rows are equal.

---

## 2. The policy: four classes, and a blob must declare one

| class | merge | multi-writer | example |
|---|---|---|---|
| **derived** | none needed — rebuild it | safe | `cache.db`, the model |
| **grow-only** | union on a content key | **safe** | chunk rows, append-only notes |
| **owned** | latest-wins from one declared writer | read-only for everyone else | a device's own log |
| **private** | **never syncs** | n/a | `identity.db` |

**An undeclared blob is refused, not guessed at.** Guessing means choosing
latest-wins by default, which is the one option that loses data without saying
so. A tool that refuses is annoying; a tool that silently drops a peer's work is
not usable at all.

**All four classes are advisory** (§0b). They describe what a *cooperating* peer
should do, and a peer that ignores them is indistinguishable from one that never
heard of them. That is not a flaw to fix at this layer — it is the reason truth
belongs in artifacts.

### The escape hatch, and it is the important half

Data that is mutable *and* multi-writer — the case with no blob-level merge —
does not become unsupportable. It changes representation:

> **Express the mutations as append-only facts, and rebuild the database.**

Fossil merges text artifacts and keeps history; a binary blob has neither. This
is MEMORY_DESIGN's M-2 — *append-only converts a merge problem into a set-union
problem* — and viki already lives by it without having said so: `captures/*.md`
are versioned truth that Fossil merges, and `viki_note` is a projection rebuilt
from them. **Nobody syncs `viki_note`.** That is why editing a note is a
supersession rather than an UPDATE.

---

## 3. What viki carries today, audited against §2

| blob | class | compliant? |
|---|---|---|
| `.viki/cache.db` via `uv` | derived **and** grow-only | yes — union merge, and rebuildable if lost |
| `viki-model/*` via `uv` | derived | yes — byte-pinned and checksummed against the epoch |
| `captures/*.md` | *not a blob* — versioned artifacts | yes, and this is the escape hatch in use |
| `identity.db` | private | **not synced** — but by convention, not by enforcement |

**The gap is that last row.** Nothing stops a future `viki cache push` variant,
or a careless script, from publishing `identity.db`. It holds wrapped private
keys; the container key is public by design (QUEUE 49), so publishing it hands
every wrapped key to anyone with repo read access to attack offline at leisure.
That deserves a refusal in code, not a note in a document.

---

## 4. What "agnostic blobs" would require

To carry someone else's SQLite honestly, a tribe needs a per-blob declaration —
name, class, and for `grow-only` the key columns that make union safe:

```
viki-sync.json
{ "blobs": [
    { "name": "viki-cache.db", "class": "derived" },
    { "name": "field-log.db",  "class": "grow-only",
      "tables": { "obs": ["obs_id"] } },
    { "name": "identity.db",   "class": "private" }
] }
```

Then push can **enforce** rather than trust: refuse `private`, union-merge
`grow-only` on the declared key, replace `derived`, and refuse anything
undeclared. Roughly the shape of `cache_merge_in()` generalised, which is
encouraging — the hard case is already solved for one blob.

**Not built.** This is the design; §3's `identity.db` refusal is the piece worth
building first, because it is a live exposure rather than a future one.

---

## 5. Open questions

1. **Should `owned` exist at all?** It depends on mtime ordering across
   unsynchronised clocks, which QUEUE 38 already flagged. A Lamport counter or
   a per-writer sequence would make it defensible; without one, "owned" is
   latest-wins wearing a promise.
2. **Who declares the class — the pusher or the tribe?** Pusher is convenient;
   tribe-level is safer, because a compromised or careless peer cannot
   reclassify someone else's blob to `derived` and overwrite it.
3. **Does a `grow-only` blob need tombstones?** Today nothing is ever removed,
   which is correct for promises and wrong for a mistaken capture that should
   never have left the device. Retraction-as-supersession works inside viki;
   it does not shrink a blob.
