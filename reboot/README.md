# reboot — distributed state, specified before it is built

A design and a **failing** test suite for the state layer: what is true, who may see it, and how it
goes away. No retrieval, no network, no filesystem.

## State of this repository

**276 tests. 276 red. Zero green.** That is the finished condition of this round, not a problem to
fix. The tests are the requirements in executable form, written before the code so the first
implementation has something to be wrong against.

```sh
python3 check.py                 # the gate
python3 -m unittest discover     # the suite
python3 -m unittest tests.test_withdrawal -v
```

No dependencies. `unittest` is in the standard library, for the same reason the course repository
uses `node:test`: a dependency is something every reader must learn, trust, audit and update.

### The gate, and why counting is not enough

`check.py` asserts three things a counting grep cannot: nothing passes, nothing dies in setup, and
every failure is a `NotImplementedError`. v1 offered `grep -c NotImplementedError` as proof and it
printed the test count whether or not a test reached its own assertions — 81% died constructing a
`Store` (`FINDINGS.md` C-1). A check that passes by construction spends the alarm and leaves the
danger in place.

It also reports which modules **no test currently reaches**. While the whole skeleton is
unimplemented a test that must write before it reads legitimately stops at `writer.py`, so this is
the honest limit of what the gate proves — and it becomes a real signal as modules land.

## The documents

| File | Answers | Changes when |
| --- | --- | --- |
| `REQUIREMENTS_v2.md` | what must observably be true | the goal changes |
| `REQUIREMENTS.md` | v1, superseded and kept | never — the diff is the artifact |
| `DESIGN.md` | v1 mechanism. **Stale against v2**; kept as the before-picture | — |
| `GAPS.md` | where the current `../core/` stands | the code changes |
| `FINDINGS.md` | what two independent reviews found the expensive way | reality writes it |

IDs are the thread. `grep W-2` walks from a red test to the sentence that demanded it to the finding
that forced it.

## What changed from v1

v1 was written from theory. v2 is written from two stated use cases — a personal assistant over
Gmail and Outlook, and project management — and the difference is structural:

- **Deleted: the entire permission apparatus** (nine mode bits, umask, filtered view, authorizer,
  `store.query`). No stated requirement needed per-assertion permission, both reviews showed the
  filter was not a boundary, and the measured SQLite defects made it worse than useless. Access is
  now per principal per diary: `r`, `s`, `x`.
- **Deleted: "every peer holds everything"** and the relay-what-you-cannot-read rule. The sensitive
  diaries never sync at all; what syncs is smaller and deliberately authored.
- **New: references and a bounded cache** (`X`). Sources are gigantic and impermanent, so an
  assertion holds a reference and the cache holds bytes. `cached` / `away` / `gone` are three
  distinguishable states, and eviction can be permanently destructive — a projection category the
  old design did not have.
- **New: derivation and publication** (`V`). Publication is the security boundary, so it produces a
  reviewable record rather than happening as a side effect.
- **New: incremental synchronization** (`S`). `merger` is the algebra of union — correct, and O(N)
  per sync however little changed, so the sender chose the receiver's CPU cost (`FINDINGS.md` F-8).
  `sync` makes the cost proportional to the difference: bounded digests, a peer that can learn what
  it lacks without receiving it, sender-side watermarks that round down, resumable transfers, and a
  budget the *receiver* sets. It is also what finally justifies the `arrival` table, which until now
  was a mechanism whose only requirement was to clean itself up.
- **New: engine portability as a requirement series** (`G`). A peer may be SQLite or Postgres; they
  are peers, not tiers, reconciling through the S protocol. Backed by two rules that make it true
  rather than aspirational: **rank is exactly 16 bytes in a stated encoding** (`A-2a`, `A-2b`), and
  **everything hashed, compared or ordered is bytes — no TEXT columns in truth** (`A-4a`). That
  second rule closes four leaks at once: no collation exists for bytes, Postgres `text` rejects NUL
  and invalid UTF-8, SQLite's blob-after-text sort order cannot bite a store with no text, and
  `substr()` counting characters instead of bytes is a live off-by-multibyte bug in the predecessor
  (`FINDINGS.md` T-2).
- **New: two views over one mechanism** (`R-8`). A conversation and a file's version history are the
  same successor edges read two ways. An audit read must use the log view (`R-9`).

## Layout

```
refcore/          the skeleton. Every behavior raises NotImplementedError.
  ports.py        Clock, Signer, Verifier, Custodian, Fetcher, KindSpec
  errors.py       the refusal taxonomy; every class names its requirement
  model.py        data, no behavior
  ids.py          framing and hashing, with domain-separated tags
  schema.py       the DDL, which is real
  store.py        INERT constructor -- the C-1 fix
  diary.py        sync policy, grants, rights
  writer.py       put, note, supersede, countersign
  reader.py       get, current, resolve, log, forks, sig_states
  refs.py         reference state, cache, pin, evict, staleness
  derive.py       derivation edges, publication
  secret.py       the travelling secret: time-lock sealing, rekey
  merger.py       merge, push -- the ALGEBRA of union
  sync.py         digest, lacking, watermark, sync, push, relay -- the PROTOCOL
  withdrawal.py   withdraw, restore, seal, erase, sweep, residue
tests/
  support.py      fixtures. Implemented; they are the mocks.
check.py          the gate
```

## Dependencies

Almost none, by choice, and a fourth review confirmed that almost nothing would help: "the design is
already near-minimal, and the two dependencies that would earn their place — a canonical encoder and
a set-reconciliation sketch — are each ~50 and ~300 lines you should own."

| Where | What | Why |
| --- | --- | --- |
| Python, test-only | `hypothesis` | The top-line claim is a semilattice law currently checked at a handful of hand-written points |
| C | SQLCipher-LibreSSL, and the `libcrypto` it already links | SHA-256 and Ed25519 already paid for |
| C, conditional | utf8proc | Only if A-4 is not narrowed; there is no libc NFC |

**Refused:** **libbf / bignum** — a time-lock puzzle was the first choice for the travelling secret
and was rejected after analysis (N-13): serial work does not defeat parallel attack, because cracking
parallelizes across guesses rather than within one. A pepper costs the attacker the same per guess,
lets the *defender* parallelize its one search, keeps the whole cost inside a memory-hard KDF, and
adds no dependency. Also refused: every CRDT library, CBOR and all canonical-serialization libraries, LMDB/RocksDB,
git/IPFS/Fossil, migration tools, minisketch, and `oopc` as a *link* — copy the convention, which is
what `core/` already does.

## Known limits

- **`DESIGN.md` is v1 and is now stale.** Kept deliberately as the before-picture; the mechanism
  rationale for v2 lives in `REQUIREMENTS_v2.md` and in the module docstrings.
- **`schema.install_authorizer` is gone** along with the filter it enforced. The measured findings
  that killed it are `FINDINGS.md` F-1 and C-6.
- **Four questions are open** at the bottom of `REQUIREMENTS_v2.md`. The reference-format one — do
  Gmail, Graph and Drive all expose a usable etag? — is a probe rather than a decision, and it is
  the only one that can still move a requirement.
