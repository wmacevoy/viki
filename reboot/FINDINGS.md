# FINDINGS

What we learned the expensive way. Newest first. Each entry: the claim, the repro, and the wrong
assumption it replaces.

Source for entries F-1..F-12: an independent security review (2026-09-04) of this directory only,
with no other context, answering "does the security model survive being deployed as a multi-tenant
service?" Verdict: **no**. Items marked *measured* were probed against SQLite 3.50.3 rather than
reasoned about.

---

Source for entries B-1..B-9: an independent buildability and dependency review (2026-09-04). The
reviewer **implemented ~660 lines and got 92 of 217 tests green**, then verified the contested
findings by execution rather than by reading. Every measurement below is theirs unless noted.

---

## B-1 The spec is implementable, and that is measured rather than argued

660 lines produced 92/217. Full single-engine SQLite: ~1,800–2,000 lines across the 15 modules
already laid out, **5–7 weeks** for one engineer; **9–11** with a real wire format and a Postgres
peer. Plus **2–3 weeks of decisions up front**, which are not coding weeks and need the spec owner.

C, anchored on `core/`'s measured 2,486 lines of code: **~10–12 KLOC across ~18 files, 14–18 weeks**,
because the reboot spec is a strict superset — grants, three withdrawal tiers with heap and
custodian, references and cache, derivation and publication, the whole S protocol, per-signer
signature state, fork detection, 16-byte rank, NFC, framing, and a second engine.

**Recommended sequencing: Python first as the reference and conformance oracle, C second against
it.** Python does not ship to U-3's phone or to the wasm tier, so C is not optional — but a
differential oracle (same inputs, same ids, same resolution, same digest roots) is worth more than
any dependency on the list.

## B-2 T-1d was wrong, and the correction is a single missing return value

The claim that S-1 and S-3 are information-theoretically incompatible **does not hold**. A fixed
256-cell invertible Bloom lookup table — 17 KB, constant, independent of store size — recovers the
exact missing ids and reports when it cannot:

```
store=   5  diff=  3  ->  recovered   3   decoded_cleanly=True    exact=True
store= 500  diff=  3  ->  recovered   3   decoded_cleanly=True    exact=True
store=1120  diff=120  ->  recovered 120   decoded_cleanly=True    exact=True
store=1200  diff=200  ->  recovered  41   decoded_cleanly=False   exact=False
```

Every S test as written is satisfiable. What is missing is **one return value**: `lacking()` must be
able to say "the sketch overflowed, this is partial" — the same shape as `SyncReport.bounded`, which
the spec already got right for the transfer half and forgot for the query half. Downgrade T-1d from
"the API shape is wrong" to "the API is missing a completeness flag."

## B-3 T-1a is confirmed but not blocking; T-1b is blocking and cheap

**T-1a** reproduces exactly: with the sane rule, `test_lying_a_forged_id_does_not_enter_the_store`
passes and both M-7 tests die with `BadId` inside `writer.put` before reaching their assertions. **The
fix is one line of test, not a requirement change:** plant the bad row with a raw
`theirs.conn.execute("INSERT INTO assertion …")`. **A merge source is untrusted by definition and
should never have to be reachable through the trusted write path.**

**T-1b** reproduces: both healing assertions differ only in `supersedes`, so both become new
unsuperseded heads and the fork survives. Blocking, and about a day's work — make `supersedes` a
tuple, frame it as a length-prefixed sorted list, move the edge to a `supersedes(id, parent)` table.
That is T-4's multi-parent recovery, and it is the right model regardless: "one node reconciles two
lines" is a real thing a single parent cannot say.

## B-4 T-1c is the one real hole, and it is a missing requirement

Rules (1) and (2) of the triangle *are* jointly satisfiable — "the receiving store's grant view must
name the tombstone's author as holding `x`." Rule (3), the hub with `grants=()` that must still apply
a tombstone, then breaks. The only escape passing all three is "`relay` sweeps without judging
authority, `merge` judges," **which makes every hub a censorship amplifier for the whole fleet.**

Confirmed by grep: `REQUIREMENTS_v2.md` contains **no rule about who may issue a grant.** No root of
authority, no delegation constraint. This is not a test bug.

## B-5 Two live bugs introduced by the A-4a edit itself *(measured, now fixed)*

**The DDL type split.** `assertion.id` was BLOB while `arrival.id` and `verified.id` were TEXT, and
`assertion.ref_*` was BLOB while `cache.ref_*` was TEXT. **In SQLite a BLOB never equals a TEXT** —
measured: `SELECT count(*) FROM assertion JOIN verified ON assertion.id = verified.id` returns **0**
for identical content. W-10, S-11 and X-8 all depend on exactly those joins. This is the T-2/T-3
class of defect reproduced *inside the requirement written to close it*, because the edit converted
the truth tables and stopped there.

**Invalid timestamps in the sync fixture.** `"2026-09-04T12:%02d:00Z" % i` yields `12:60:00Z` at
i=60 and worse beyond, so `test_S3_a_digest_does_not_grow_with_the_store` — which fills 500 rows —
**could never pass any C-3-compliant implementation.** `check.py` could not see it: the test dies in
`writer.py` one frame earlier, which is T-10's "C-1 displaced one frame" arriving with a concrete
instance.

Both fixed. Neither was catchable by the gate as it stands.

## B-6 Six further defects, unfixed

1. **A-4 vs A-4a: you cannot NFC-normalize invalid UTF-8**, and one test requires normalization while
   another requires `b"\xff\xfe…"` to round-trip. One rule satisfies both, verified: decode
   `surrogateescape` → NFC → encode `surrogateescape`. **It must be stated**, because it is a hash
   rule and two peers guessing differently fragment the store silently.
2. **D-3a is false as tested.** `test_D3c` grants an agent `S` alone and then reads full bodies out
   of `reader.log`, while `test_D3a` asserts the same agent gets `Unauthorized` from `reader.get`. An
   `s`-only agent reads everything through the log view. **D-3a's headline — "a fully compromised
   agent still cannot read" — does not survive its own suite.**
3. **`derived_from` is outside the frame and outside the merge.** Not among the eight hashed fields
   and not a step in the merge order, so V-1's edges are unauthenticated, two assertions with
   different sources collide on one id, and after a merge `derivatives_of()` is empty — silently
   disabling W-13's flagging on exactly the peer that received the summary.
4. **R-4 needs two enforcement points with two behaviours** — refuse in `supersede`, ignore-at-fold in
   `current` — and nothing says so. The fold-time half costs an author-rights lookup on every arm of
   every read, **a real cost nobody has costed.**
5. **C-3's skew tolerance is unstated and load-bearing.** Measured: at a 5-minute bound, 6 of 25 sync
   tests are refused at write time by their own fixture; at 60 minutes, 1.
6. **G-1 has no implementation surface.** Nothing in `refcore` takes or returns wire bytes;
   `merger.merge` and all three `sync` entry points bind two live `Store` objects — structurally the
   same coupling T-12 condemns in the predecessor.

## B-7 D-4 and S-9 are enforced by a Python `if`

`test_D4` checks that `reader.get` raises `Unauthorized` on a store whose SQLite file the relay
operator owns. The K4 table claims "a relay learns only ciphertext"; **no requirement anywhere says
anything is encrypted.** v1's permission filter was deleted for being a non-boundary, and two D/S
requirements whose only possible implementation is that same non-boundary were kept.

## B-8 Dependencies: the honest answer is that almost none help

**Refuse:** every CRDT library (the data model *is* a 2P-Set; union is ten lines, and any library
brings its own identity model and fights A-1); **CBOR and every canonical-serialization library**
("deterministic CBOR" is opt-in per library and its failure modes — map ordering, indefinite lengths,
float canonicalization — are the exact class T-12 names for PG's json builder); LMDB/RocksDB (the SQL
is not where the difficulty is, and SQLCipher gives at-rest encryption free); git/IPFS/Fossil; any
migration tool; **`oopc` as a link** (86 lines of reusable code, unlicensed, 8 commits, no external
adopters, ~50 lines of boilerplate per class — copy `Type.h`/`Type.c` and adopt the convention, which
is what `core/` already does).

**minisketch** would make S-1/S-3 exactly true in one round and is production-proven in Erlay, but
it is C++ with BCH algebra nobody here can audit, and a 60-line IBLT already satisfies every S test.
**Read `negentropy`'s protocol spec** — range-based set reconciliation, the same problem, well
documented — before designing the multi-round fallback. **Copy the protocol, not the code.**

**Take:** `hypothesis` in Python, test-only. The top-line claim of the system is a semilattice law and
the suite checks it at three hand-written points; generated operation sequences with shrinking are
the difference between believing merge is confluent and having a minimal counterexample. It attacks
the hardest correctness risk (sweep confluence under merge, G-6) at zero runtime obligation. For C,
hand-roll ~150 lines that double as the differential oracle against the Python reference.

**LibreSSL's `libcrypto` is already paid for** by SQLCipher-LibreSSL, so use its SHA-256 and
**delete `core/src/sha256.c`** — its header still claims "not a security boundary" while the function
now computes the id that Ed25519 signs, and **there is no known-answer test for it anywhere in the
153-assertion probe**, so a different-but-consistent hash would pass the entire suite.

**`retain-recall` with eyes open:** MIT, real CI across three OSes and three TLS backends with
sanitizers — but **every invariant is an `assert()`, so `NDEBUG` deletes all of them** and `RECALL` on
an empty stack is a null dereference in any Release build. And ambient thread-local context is a poor
match for a layer whose whole job is holding two stores at once; an explicit `Store*` parameter fits
better.

**C's one forcing dependency is NFC**: there is no libc normalization, so it is ICU (unacceptable) or
utf8proc (one .c plus a table, MIT). **Better: narrow A-4** so `akey`/`kind`/`author` are opaque bytes
chosen by the `KindSpec` and normalization is the caller's problem, *verified* rather than performed.
That is Fossil's answer (T-7), costs nothing, and removes the dependency entirely.

Also flagged: **`retain.h` is an unvendored dependency of `core/`'s public header**, the comment
claiming it is vendored is false, and `core/build.sh` fails today without `VIKI_RETAIN_DIR`.

## B-9 Two recommendations, ranked

**First — write down the root of authority:** who may issue a grant, and what a receiving store does
with a grant it cannot trace to that root. It is the only genuinely unsatisfiable thing in the
specification (B-4). Everything downstream depends on it: W-5's sweep, D-3a's confinement claim,
S-9's hub, I-4, I-6's rotation, and whether merging grants is safe or is a self-promotion vector.

**Second, cheap and same week — promote A-3's framing to be the wire format**, and add `derived_from`
to it. That turns G-1 from a sentence into code, gives the S protocol something to ship, kills the
two-live-handles coupling the spec criticises in its predecessor and then reproduces, and closes B-6.3
— with one encoder, no options, and no new dependency.

**And a subtraction: cut the G series to G-1 and G-2 for v1.** Nobody has answered T-12's strategic
question — if the hub cannot read rows, Postgres buys nothing a blob store would not, and there is no
PG analogue of SQLCipher; if it can, D-4 and S-9 are false. Keep A-4a's bytes rule and A-2a's rank
width, which are correct on their own merits. Drop "a peer may be Postgres" until the hub-trust
question is answered: it deletes four requirements, a class of dialect work, and the
`WITHOUT ROWID`/`AUTOINCREMENT` problem.

---

Source for entries T-1..T-12: an independent three-way comparison (2026-09-04) of `reboot/`,
`core/` and `vendor/fossil-see`, stopped early and delivered as a partial. Verdict: **treat
`REQUIREMENTS_v2.md` as a patch list against `core/`, not a replacement.** *(Verified)* = the
reviewer read the code or ran a probe; *(inferred)* = reasoned from documents.

---

## T-1 v2 reproduced the exact defect C-7 named: four mutually unsatisfiable pairs *(verified)*

C-7 flagged "one PAIR of tests unsatisfiable by any implementation" as a v1 defect. v2 has four.

**T-1a `put` must both refuse and store a forged id.** `test_failure.py:106-110` requires `BadId`
for `id = "0"*64`. `test_merge.py:81-84` requires `put(theirs, bad)` to SUCCEED for
`id = "not-a-hash"`, so that merge has a row to quarantine. The only escape is to reject a
shape-valid hex mismatch and accept a shape-invalid one — perverse, and it makes M-7 exercisable
only against malformed strings, never a genuine hash mismatch.

**T-1b R-7 fork healing is impossible with single-valued `supersedes`.**
`test_resolution.py:160-170` heals a two-arm fork with two superseding assertions. Both carry
`supersedes` in the frame (A-2), so both get distinct ids, both are unsuperseded, both sit on akey
`k` — still a fork by R-5. The test asserts `assertIsNone`. **See T-4 for the fix.**

**T-1c The tombstone-authority triangle — any two of three.**
(1) `test_withdrawal.py:154-160`: ALICE's tombstone bites in ALICE's store, her only evidence of `x`
being the constructor tuple at `support.py:148`. (2) `test_provenance.py:97-109`: BOB holds `R|S|X`
in *his* store, erases ALICE's row, merges in, and the row must survive — so a tombstone bites only
if the RECEIVING store's grant view names its author. (3) `test_sync.py:217-228`: the hub has
`grants=()` and must still apply ALICE's tombstone, which rule (2) forbids.

The escape is worse. `merger.py:23` merges grants, but **no test ever writes a grant into a store
that is then merged** — every rights decision comes from the constructor tuple. Fix that and (2)
flips, because the merge delivers BOB's **self-issued** `R|S|X`.

**REQUIREMENTS_v2 HAS NO RULE THAT A GRANT MUST BE ISSUED BY SOMEONE WHO ALREADY HOLDS THE RIGHT.
There is no root of authority and no delegation constraint.** This is F-9 and C-9 — both named as v1
defects, both reproduced in the v2 fixture, while `diary.py:30-38` documents the fix in prose.

**T-1d S-1 and S-3 are information-theoretically incompatible. — OVERSTATED, see B-2.** `Digest` is fixed at 256 buckets
and `test_sync.py:86-91` pins that count equal for a 5-row and a 500-row store. But
`lacking(store, remote: Digest)` is a pure local function of one store and one static digest, and
`test_sync.py:30-34` requires it to return three exact sha256 ids. You cannot recover d arbitrary
256-bit ids from a constant-size digest for unbounded d. Real anti-entropy needs **multiple rounds**
— which the single-`Digest` signature forecloses — or a sketch sized in d, which S-3's test forbids.
**The S-series API shape is wrong, not just its tests.**

## T-2 A LIVE BUG in `core/` today: `substr()` counts characters, `range_at` counts bytes *(verified by probe)*

`range_at` computes byte offsets (`viki_core.c:945-946`); the chunk view slices with
`substr(a.atext, c.lo+1, c.hi-c.lo)` (`:249-251`), and SQLite's `substr` on TEXT counts
**characters**.

Measured: `atext = 'café is a word\nsecond line'` is 27 bytes and 26 characters. `range_at` gives
`lo=0, hi=16` for line one; `substr(atext,1,16)` returns `'café is a word\ns'` — one character of
line two bled in.

Every chunk boundary in non-ASCII text is off by the count of multibyte characters before it,
cumulatively, compounding through the overlap. FTS and embeddings are computed over the wrong
window — and the wrong window is baked into a key the schema describes as immutable and
union-mergeable. PG has the identical `substring(text …)` vs `substring(bytea …)` split.

**This is the highest-value finding in the review for shipping code.**

## T-3 The `arank` fix is endorsed, with three cautions *(verified)*

Called "the single highest-value change available." But:

1. **It fixes the ordering that usually decides and leaves the one that decides ties.** R-2's
   tiebreak is `arank DESC, id DESC`, and `id` is still a 64-char hex TEXT column. Lowercase hex
   happens to sort the same under C and en_US — luck, not a guarantee, and it breaks the moment
   anything uppercases an id. Make `id` bytes too, or at minimum `COLLATE "C"`.
2. **SQLite sorts ALL BLOBs after ALL TEXT regardless of content** (verified: `'zzz'` TEXT sorts
   before `CAST('aaa' AS BLOB)`). A store with mixed text and blob ranks orders **catastrophically**
   wrong, not subtly. Migrate every row in one transaction; never allow a mixed state to exist even
   transiently.
3. It moves the work to `KindSpec.rank`, which must now produce 16 bytes deterministically across
   hosts. Frame membership (A-2) makes divergence *detectable*, not impossible. **State the encoding**
   — big-endian ts-millis ‖ counter ‖ tiebreak — rather than "16 bytes, your problem."

And from the addendum: `core/`'s current tiebreak is a **query-plan artifact**. Drop
`viki_assert_key` and the planner uses a temp b-tree and returns a different winner on the same
rows. **Ship `, id DESC` with the width change or one silent nondeterminism replaces another.**

## T-4 Multi-parent: the mechanism Fossil has, both successors dropped, and R-7 needs *(verified)*

A Fossil check-in's `P` card carries a primary parent **plus N merge parents**; `nParent` /
`azParent[]` grows (`vendor/fossil/src/manifest.c:95-96, 892-897`). That is a first-class "one node
reconciles two lines" primitive.

Neither `core/` (`viki_core.h:157`) nor `reboot/` (`model.py:110`) has it — `supersedes` is
single-valued. **This is exactly what R-7 needs and cannot express (T-1b).** Named the
highest-value recovery available.

## T-5 Fossil's shun is out-of-band on purpose, and both successors inverted it *(verified)*

`shun` is a repo-local table consulted on receive and on send (`src/schema.c:155`;
`src/xfer.c:162, 281, 461, 499`), travelling only via an explicit privileged
`fossil configuration push shun` (`src/configure.c:69, 166, 352`) — never in ordinary sync.
**Shunning is safe because it is out-of-band, opt-in and privileged.**

`core/` inverted this: redaction is an ordinary in-band assertion that propagates automatically and
bites everywhere, with no signature check (`apply_redactions`, `viki_core.c:1179-1204`). `reboot/`
correctly diagnoses that (W-2, W-5) and keeps the in-band design **without ever considering the
out-of-band alternative the ancestor uses.**

Related: Fossil's **private branches** (`xfer.c:53, 168, 276`) are the ancestor of D-1 `never`, and
they are enforced by **a capability check at the receiver, not a policy flag at the sender.** Given
T-1c, that is the shape worth copying.

## T-6 Four mechanisms `reboot/` lost from `core/` *(verified)*

1. **The `canon()` / `text()` split** (`viki_core.h:143-146`) — identity bytes vs searchable bytes.
   `reboot`'s `KindSpec` has no `text`, and `ids.compute_id(a)` takes only an `Assertion` so it
   **cannot reach `KindSpec.canon` at all** (confirmed independently). A-4's NFC normalization is
   therefore forced unconditionally over arbitrary binary bodies.
2. **Payload outside the assertion row** (`viki_blob`, `:238-243`) — *"so a 23 MB model does not sit
   in the row every resolve, count and merge scans."* `reboot` puts `body BLOB NOT NULL` **inside**
   `assertion` **and declares it WITHOUT ROWID** (`schema.py:13-25`), under a requirement (U-4) that
   says sources are gigantic. SQLite's guidance is that WITHOUT ROWID is a pessimisation once rows
   exceed ~1/20 of a page.
3. **Chunk ranges keyed `(id, lo, hi, model)`** (`:212-217`) — the extent is in the key, so two
   chunkings coexist, overlap is free, chunk rows are grow-only and union-mergeable, and a device
   with no model can store an assertion a device with a model later adds ranges over. `reboot/` has
   no projection schema at all.
4. **`viki why` — the chain walked by ID, both directions** (`viki_trace.c:190-215`). `reboot`'s
   only chain read is `reader.log(store, akey)`, keyed by **akey, not id**, so an agent holding an id
   from an earlier session cannot ask what replaced it. **There is no reverse walk anywhere in
   `refcore/`**, while R-8 claims the two views are one mechanism.

## T-7 Fossil hashes with NO framing at all, and that is a live alternative *(verified)*

`www/fileformat.wiki:36-38`: *"No prefixes, suffixes, or other information is added to an artifact
before the hash is computed."* Fossil still gets positional identity because a manifest's bytes
**contain** its D and P cards — its `canon()` is self-describing.

So `core/`'s `\x1f` framing and `reboot/`'s length-prefixed A-3 are both **compensating for a
`canon()` that isn't**. Fossil's version has a property neither successor has: the hash function
contains no schema, so **adding a field later cannot silently change every id** — which A-2 just did
by adding `rank`.

## T-8 The security boundary has zero tests that cross it *(verified)*

Every publication test calls `derive.publish(assistant, assistant, ...)` — **source and destination
are the same store** (`test_derive.py:61, 71, 86`). Worse, `test_V2`/`test_V3` publish `summary.id`,
an id written into **gmail** that `assistant` does not hold, so `publish` records a crossing that
never happened. V-3 says *"This is the security boundary, so it must be visible."* **Nothing in the
suite moves one byte across one.**

Also `test_derive.py:46-53` asks `assistant` to refuse a write because `source.id` came from a
`never` diary — but `assistant` holds no row for that id and no knowledge of gmail's policy. It can
only pass by refusing *any* unknown `derived_from`, a different rule under a misleading error class.

## T-9 Four requirements with no schema, and three vacuous tests *(verified)*

**No schema:** `restore` works by the tombstone being superseded, and `tombstone` has **no
`supersedes` column** while `grant` does (`schema.py:34-43` vs `:55`). W-9's local free text has no
table. **D-3b's vocabulary has no input at all** — `Store.__init__` takes no vocabulary and
`diary_policy` has no column, so `test_diary.py:147` smuggles it through `endpoint="enum:ok,spam"`,
overloading the hub-endpoint column; `test_D3b` is unsatisfiable. M-5's `unprojected` is uncomputable
with no projection table. **All four are verbatim C-8, reproduced.**

**Vacuous:** `test_sync.py:121-127` (S-5's falsifier) plants a lie that S-6 clamps to zero, in a
different store from the one it then queries — the docstring says "an implementation that trusts the
watermark fails here" and it cannot. `test_coverage_gaps.py:162-163` (W-11) iterates `vars(report)`,
which yields **field names**, so it cannot fail — and C-7 flagged the v1 version of this exact test.
`test_withdrawal.py:258-269` (C-5) checks residue on a store that never held the target.

## T-10 The gate understates its own limit *(verified — the reviewer ran it)*

200 red, 0 green, exit 0 — but 142/200 raise in `writer.py`, and `schema.py` and `withdrawal.py` are
reached by **zero** tests. The whole W-series, which `GAPS.md` calls "the three that matter," pins no
behaviour at all. **"This is C-1 displaced one frame, not eliminated."** Fair: it was reported as an
acceptable limit rather than as the same defect moved.

## T-11 Six of twelve findings dropped silently *(verified by grep)*

F-3, F-5, F-6, F-7, F-11 and F-12 appear nowhere in `REQUIREMENTS_v2.md`, `GAPS.md` or `README.md`,
in a repository whose thesis is *"IDs are the thread."* `withdrawal.py:129` claims F-7 is "Open in
REQUIREMENTS_v2" and it is not in the Open list.

**F-5 matters most:** no operator or moderator class, no admission control, no in-model remedy for a
store polluted with illegal content. D-3's friendly-access K4 makes it worse — v1's per-assertion
filter was correctly killed and **the replacement is nothing**.

`GAPS.md` is also stale against v2 and unmarked: G-14 cites `P-1..P-9`, a series v2 deleted.

## T-12 Postgres: the question underneath, which no document asks *(verified reasoning)*

Ranked leaks in `core/`: identity built by **engine-specific SQL functions** (PG's
`jsonb_build_object` reorders keys, so the same logical task yields a different sha256 — the third
instance of this class in this project); **no wire format at all** — merge binds two live handles and
`host/viki-httpd.py` ships an entire checkpointed SQLite file, so **the wire format IS the SQLite
file format** and a PG peer cannot participate at any layer; sweep atomicity assumed from
single-writer; `AUTOINCREMENT` as the watermark basis (PG sequences allocate before commit and are
not gap-free, so a committed-later row is skipped and reported delivered — **exactly S-6's dangerous
direction, caused by the engine rather than a caller**); `TEXT` columns holding arbitrary user bytes
(PG rejects `\x00`); T-2's `substr`; `WITHOUT ROWID` carrying a blob; locale-dependent `lower()`;
and FTS5/`bm25()`/`viki_cos`, **least dangerous precisely because retrieval is a declared projection
that never merges** — worth stating as the explicit licence rather than leaving implicit.

**But the strategic question is the one nobody asked:** if the PG hub **cannot** read rows (S-9,
D-4), PG buys nothing a blob store would not — and there is no PG analogue of SQLCipher, so the
"a diary IS the encrypted container" story vanishes. If it **can** read them, D-4 and S-9 are false
and the hub is inside the trust domain. **"PostgreSQL on the hub" is under-specified until that is
settled, and no amount of dialect work resolves it.**

`core/` is nonetheless dramatically more portable than its ancestor — Fossil to PG would be a
redesign, not a port (ATTACHed cross-database joins, `blob.rid` as a wire cursor, single-writer
read-modify-write as concurrency control). That is a direct payoff of "the contract is SQLite, not
Fossil."

## What the review credits

`reboot/`'s **R-2 total order is better than both ancestors** — a grep of all of Fossil found no
resolution path anywhere that breaks a tie by content hash; every one is `ORDER BY mtime` with no
secondary key, and the single deterministic tiebreak (`unversioned`, `src/unversioned.c:173-197`)
destroys the loser. Fossil survives its own nondeterminism only because `tagxref` is derived and
`rebuild` regenerates it; `core/` and `reboot/` resolve in-query with no rebuild, so a
nondeterministic tiebreak is not repairable.

Also credited: `reboot/`'s **W-1 `withdraw` tier** is Fossil's anti-tag (`T -name`, tagtype=0,
`manifest.c:2459`) — a non-destructive cancel that does not claim to be a newer version — which
`core/` lacks entirely. And **M-7's quarantine is a genuine fix `core/` should take**: `merge_into`
currently aborts the whole savepoint on any bad row (`viki_core.c:869-874`), so one planted
immutable row is a permanent denial of sync.

And `core/` **correctly discarded** Fossil's phantom/orphan machinery, which exists only because
`tag_insert` folds destructively at write time. An unrecorded win worth writing down so nobody
reintroduces it.

## One unmarked false claim in `core/` *(verified)*

`core/README.md` §2 lists **"field-level merge for the cases that need it, which is what
`ticketchng` does"** as inherited. It does not exist: a task's fields live in one JSON `body`
(`viki_task.c:86`) and superseding replaces the whole body. Nothing in `core/` merges fields.

## Not examined

The reviewer did not build or run `core/`, did not review `src/`, `edge/`, `assistant/` or
`cli/viki_cli.c` beyond greps, gave `test_provenance.py` and `test_derive.py` a lighter pass, and
measured no performance. It cannot speak to what Fossil does about clock skew, hash collisions, or
sync-during-commit.

---

Source for entries C-1..C-9: an independent conceptual-integrity review (2026-09-04) of this
directory only, no other context, asking "is this one coherent design?" Verdict: **no — two good
designs that were not introduced to each other.** Items marked *measured* were run.

---

## C-1 The verification in README.md was itself the K3 it warns against *(measured)*

**81% of the suite (99/122) dies in `Store.__init__` at the first `a_store()` call and never reaches
the function it names.** The README claimed "every failure is a `NotImplementedError` raised by the
function under test," and offered `grep -c '^NotImplementedError'` as proof — which prints 122
whether the rest of each test body is a specification or gibberish.

**Wrong assumption:** that checking the exception *type* checks the *target*. It does not. The
categorizer run before publishing confirmed all 122 were `NotImplementedError` and that result was
reported as though it confirmed each test reached its own subject. Necessary, not sufficient, and
presented as sufficient.

Actual distribution: `store.py` 99, `ids.py` 17, `acl.py` 6.

**Fix:** a real gate — assert the raising frame is the module the test names — and a `Store` stub
inert enough that tests proceed past construction.

---

## C-2 Resolution is not a pure function of the set, so R-2 and R-3 are false as written

Two independent causes.

**Rank is outside the frame.** A-2 frames seven fields; `arank` is not among them. Rank is produced
by a host-supplied `KindSpec` at write time and stored. So two peers whose hosts implement
`KindSpec.rank` differently for one kind produce assertions with **identical ids and different
ranks** — same set, different winner, permanently, and *undetectable*, because the ids agree so no
integrity check fires. Fix: put `arank` in the frame, or pin the kind spec into the epoch the way
the parent project pins `(model, chunking)`.

**Authority is derived from content that erasure destroys.** Grants are ordinary assertions:

1. Grant `G` puts Bob in village V.
2. Alice writes `X` with `village=rsx`.
3. Bob writes tombstone `T1` erasing `X` — valid while `G` stands.
4. Alice writes `T2` erasing `G` — valid, she authored it.

Every peer holds `{G, X, T1, T2}`. Sweep `T2` then `T1`: `G` dies, Bob leaves V, `T1` is inert, `X`
survives. Sweep `T1` then `T2`: `X` dies. **Same set, two terminal states, chosen by sweep order.**

No `ORDER BY` fixes this. Authority-bearing assertions must join the immune class — which means the
immune class is larger than "tombstones," and W-2's structural-by-table-split argument needs a second
mechanism after all.

**Third cause:** with P-5 filtering before the fold, two stores holding the same set and different
principals *must* return different winners. R-2 as phrased cannot hold.

---

## C-3 The `seal` tier cannot be built as specified

`DESIGN.md` §6 says each peer encrypts its copy **in place**. A-6 forbids changing a stored
assertion; A-7 refuses an assertion whose id does not match its content.

1. Bob seals `X`; `assertion.body` is now ciphertext and the row stays (it must — `VISIBLE_VIEW`
   filters `tier IN ('withdraw','seal')`, which is only needed if the row is still there).
2. Bob pushes to Carol, who verifies arriving ids. `sha256(frame(…ciphertext…)) != X`.
3. Per `test_E4`, one bad row raises `IncompleteMerge` for the whole union.

**One sealed row permanently poisons every merge that peer is a source of.** The seal tier as
designed cannot coexist with content-addressed identity. Either sealed content leaves `assertion`
for a `sealed` table — making seal a recoverable erase — or A-7 is scoped to exclude sealed rows,
which puts an unauthenticated hole at the merge boundary.

---

## C-4 W-1's reversibility has no mechanism, and its test passes only when nothing is restored

`withdrawal.py` has no un-withdraw verb, and its module docstring says "a withdrawn id can never be
re-added. That is the requirement rather than a defect" — the direct negation of W-1's "restorable"
and of the reversibility column in the W table.

`test_W1_a_withdrawal_is_reversible` supersedes the tombstone and asserts one visible row. The view
consults `tombstone`, not whether the tombstone was superseded, so the target stays hidden and the
test goes green on the restore row alone. **Restore the target correctly and the test fails.** The
broken implementation passes; the correct one does not.

---

## C-5 M-7 versus P-8: the filter's stated adversary walks through the merge door

M-7 requires a relay to carry what it cannot read, and `test_M7` has Bob merge Alice's private row
and push it onward. Reading that row for the push reaches past Bob's own filter — which
`test_P8_reading_a_base_table_fails` asserts is structurally impossible. Both tests are in the suite.

The only reconciliation is a privileged internal path, at which point P-8 is a convention about
`store.query` and not a structural boundary.

**And `DESIGN.md` §5's agent-confinement argument collapses:** the confined agent calls
`merger.push(store, attacker_store)` and ships every row, readable or not, in one call. Nothing in
`REQUIREMENTS.md` gates who may merge or push, and the non-goals explicitly disclaim write
prevention.

*(The security review reached the same conclusion by a different route. Two independent readings
converging is the strongest signal in either report.)*

---

## C-6 The `visible_assertion` view cannot carry a per-principal predicate *(measured)*

`acl.visibility_predicate` returns SQL **and parameters**. SQLite: `OperationalError: parameters are
not allowed in views`. So the predicate must be inlined as a literal.

But a view is persistent schema in the file, and `CREATE VIEW IF NOT EXISTS` on an existing view is
a silent no-op — verified. **Whichever principal opens the database file first bakes their ACL into
it, and every later principal reads through it.** P-7's rationale — a different principal gets a
different connection — is exactly what makes this fire.

The design's one executable claim is wrong in the direction of granting access. Fix: `CREATE TEMP
VIEW` per connection, or a predicate over a session table.

---

## C-7 Five pairs of tests are mutually unsatisfiable, and a fixture defeats five more

**`support.py:70`** — `signer=signer if signer is not None else FakeSigner(principal)` means
`a_store(signer=None)` **has a signer**. Five tests pass `signer=None`. Consequence:
`test_W5_an_unsigned_tombstone_does_not_bite` becomes byte-for-byte the same setup as
`test_W5_an_author_can_erase_their_own` with the opposite assertion — **no implementation can pass
both.** `test_I3`, `test_E1_a_missing_signer_is_not_an_error`, `test_I4_an_unsigned_grant…` and
`test_M4_a_signed_copy_is_not_shadowed…` are vacuous. Fix: `FakeSigner(declines=True)`, or an
`_UNSET` sentinel.

**`test_R2` and `test_R5`** both build two unsuperseded assertions on key `k` with equal `ts`. No
predicate can call one a fork and the other resolvable. Under R-5's fork definition,
`test_R1_highest_rank_wins` and `test_R3` fail too. **R-1 and R-5 are inconsistent requirements**: if
any second unsuperseded arm is a fork, `current` never uses rank and R-2's tiebreak is unreachable.

**Asserting nothing:** `test_A3_embedded_nul_and_newline_survive` (passes for `b"".join`);
`test_R1_unknown_key_is_not_found` (`assertRaises(Exception)` — a typo satisfies it);
`test_M6_a_push_does_not_advance_my_own_clock` (asserts a dataclass default, and contradicts
`test_E2_a_partially_projected_store_says_so`); `test_W11` (tests for the absence of an attribute
name); `test_P5_the_filter_runs_before_ranking` (no `k` parameter exists, so before and after are
indistinguishable); `test_E3` (samples `BadTimestamp`, the one class with a static requirement —
`Refused`, the most-raised class, inherits `requirement = ""` and violates E-3 outright).

**Naming:** `test_W18_…` names gap G-18; there is no requirement W-18. `grep W-18` walks nowhere, in
the repository whose README says IDs are the thread.

---

## C-8 Requirements with no mechanism, and one mechanism with no requirement

**No mechanism:** P-5's `k` — **no read function takes a limit**, so filter-before-ranking is
unobservable, untestable and unviolatable. M-5/W-4 projections — no table, no flag, so
`reader.unprojected()` is uncomputable and `residue()` cannot see a leaked vector. W-9's local free
text — no table in the DDL. I-4 signed grants — no grant table, no kind, and `assertion` has no
village column at all, so "is this in one of my villages" is unanswerable and §5's "an `IN` over
constants" has no column to run against. W-1's restore. `KindSpec` — referenced by **zero** modules
and **zero** tests; nothing registers a kind, `Store` takes none, `Assertion` has no rank field. The
single declared extension point has no wiring. E-2's third case — `Result` has no channel for
*unreadable*, and five of six read paths return bare values with no blindness channel at all.

**No requirement:** `arrival`/`seq` exists only so W-10 can clean it up. By `DESIGN.md`'s own rule it
should be deleted. `errors.BadFrame` can never be raised. `payload`'s primary key contradicts §3's
"large payloads key on a caller-supplied content hash on a separate path."

---

## C-9 The glossary polices words, not concepts

Ten words for a design with at least fifteen. **`key` has five meanings**: what assertions compete
on, the SQLCipher key, a seal's content key, a public key (`schema.py:19`, `Signer.principal`), and
the wrapped key — and `tests/test_merge.py:14` defines `keys_of()`, which returns **ids**.

**`rank`** means a sortable string and, in P-5, retrieval relevance — which the non-goals put out of
scope entirely. **`withdraw`** is both the genus and one species, the exact error the glossary bans
*delete* and *write* for. **`tombstone`**: the glossary says "an assertion"; the schema says it is not
one. **`seal`** is a confidentiality partition in §5 and an escrow tier in §6.

**`principal` and `author` are conflated in the API built to keep them apart.**
`acl.can(store, assertion, right)` takes only `store.principal` as subject, but R-4 and W-5 are
questions about the author *of the superseding assertion or tombstone*. As specified, whether Bob's
hostile tombstone bites depends on who opened the store — Alice opening her own store applies it,
since she holds `x` on her own row. **The signature is missing a parameter, and that omission is the
difference between the two headline security requirements working and being decorative.**

Also: `DESIGN.md` §7 says "nine modules" over a table of eleven, and that table omits `acl` from
`writer`'s and `withdrawal`'s dependencies — which matters, because the table *is* the SOLID
argument.

---

## F-1 The P-8 authorizer MECHANISM works; the POLICY built on it does not *(measured)*

The recalled behavior is correct: a read through the view produces `SQLITE_READ` on `assertion` with
the innermost-view argument set to `visible_assertion`, and base-table reads, subqueries, differently
named views and `ATTACH`ed databases are all denied. **The README's UNVERIFIED note resolves in
favor of the mechanism.**

The stated policy — "deny the base tables, permit only the views" — is a *read-only* doorframe, and
these all succeed under it:

| Attack | Result |
| --- | --- |
| `CREATE TEMP VIEW visible_assertion AS SELECT * FROM main.assertion` | every row returned; temp shadows main and the read carries the sanctioned view name |
| `DROP VIEW visible_assertion; CREATE VIEW visible_assertion AS SELECT * FROM assertion` | every row returned, persistently, for every later connection |
| `DELETE FROM tombstone` | succeeds — **retires W-2** |
| `UPDATE assertion SET body='rewritten'` | succeeds — **retires A-6 and A-1 together** |
| `INSERT`, `DROP TABLE`, `CREATE TRIGGER`, `PRAGMA`, a 3M-row recursive CTE | all succeed |

**Wrong assumption:** that denying `SQLITE_READ` on base tables is what "structural, not remembered"
means. Nothing in `DESIGN.md` §5 or `schema.py` denies `SQLITE_UPDATE`, `SQLITE_DELETE`,
`SQLITE_INSERT`, `SQLITE_CREATE_TEMP_VIEW`, `SQLITE_DROP_VIEW`, `SQLITE_CREATE_TRIGGER`,
`SQLITE_ATTACH`, `SQLITE_PRAGMA` or `SQLITE_FUNCTION`.

**Worse:** `store.query` was added *deliberately*, with a docstring arguing the door has to exist for
the filter to be structural rather than a property of today's queries. It opens directly onto
`DELETE FROM tombstone`. The door reintroduces G-1, the top finding this repository exists to fix.

**Fix:** default-DENY allowlist authorizer; assert the temp schema is empty per connection;
`sqlite3_limit` and a progress handler; do not expose raw SQL to tenants.

---

## F-2 A tombstone has no framing rule, so erase authority can be forged with no stolen key

`ids.compute_id` takes an `Assertion`. `tombstone` has its own `id TEXT PRIMARY KEY` and **no framing
rule appears anywhere** — not in `ids.py`, not in `DESIGN.md` §3, not in A-2. So A-7's "an arriving
row's id is checked against its content" has no definition of *content* for the one row shape that
destroys things.

Compounding it: `signature` is keyed `(id, signer)` with no purpose field, and `Signer.sign(id_hex)`
covers a bare hex string with **no domain separation**.

**Repro:** take any id Alice ever signed — ids are public and travel by design. Craft a tombstone with
`id = <that id>`, `target = <anything>`, `tier='erase'`, `author=ALICE`. The sweep looks for a verified
signature on the tombstone's id from a principal holding `x`, finds Alice's genuine signature, and the
tombstone bites.

**Wrong assumption:** that giving tombstones their own table (the structural fix for W-2) was free.
Splitting the table split them off from the framing rule too, and nobody wrote the second one.

**Fix:** content-address tombstones under the same length-prefixed framing with a distinct domain tag;
sign `(purpose ‖ id)`, not `id`.

---

## F-3 M-7 plus full replication makes every permission advisory

M-7 mandates that a peer merges and relays what it cannot read. `DESIGN.md` §9 defers partial
replication with "every peer holds everything… when that stops being affordable."

Composed: every peer's SQLite file holds every other participant's plaintext — author, key, ts, mode,
supersedes, body — regardless of mode bits. The attack is `sqlite3 store.db`. The nine bits are a
property of a *connection*, and no adversary is obliged to use ours.

**Wrong assumption:** §9 files full replication under **affordability**. Under multi-tenancy it is a
total confidentiality failure on day one, and it decides the architecture: either tenants are not
peers, or per-scope encryption is v1 for everything rather than for "a small sealed class."

`DESIGN.md` §5's deliberate reversal — filter first, encryption later — is correct for a local library
and **inverted for a hosted service**.

---

## F-4 One row permanently breaks any key, and R-4's authority check is not on the path

**Fork-DoS.** R-5 plus `reader.current` raising `Forked`: one hostile assertion on `warren/salary`
breaks `current()` for that key forever. The victim cannot heal it — superseding the hostile arm needs
`s` on it, erasing it needs `x`, and the attacker's mode grants neither. Grow-only makes it permanent;
merge propagates it. Cost: one row.

**Rank hijack**, if a non-raising resolution path is used instead: `ts` is author-chosen, C-3 validates
only format and UTC, nothing bounds `ts` against arrival, and C-2 makes `seq` local and never merged —
so no trustworthy arrival evidence travels. Set `ts` far in the future and own the key.

**Wrong assumption:** that R-4 (authority to supersede) covers hostile writes. Neither attack touches
`supersedes`, so the check never runs. `akey` is a free-form string with no namespace and no binding to
an author.

---

## F-5 Fixing G-3 is what makes abuse unremovable — the same mechanism, two directions

Once a tombstone requires `x` on its target, a peer has no authority to erase what was pushed at it:
the spammer is the author, holds `x`, and declines. A store polluted with illegal content, malware, a
planted credential or doxxing has **no in-model remedy**, and M-7 relays it onward.

There is no operator or moderator class in the nine bits. `Reason.LEGAL` exists in the vocabulary; the
authority to act on it does not.

**Wrong assumption:** the non-goal "No write prevention" reads as physics — "in a union-merge store you
cannot stop a peer writing." That is true of what a peer *authors* and false of what a service
*accepts*. It is a decision to have no admission control, and admission control is the one thing a
hosted service cannot do without.

The abuse adversary appears in none of the four documents.

---

## F-6 The injection argument in DESIGN.md §5 does not hold

§5 argues an agent "that speaks only this API and never sees the key is confined by it."

The filter confines the agent to **the principal's own scope**, which is exactly what an injection
wants — the principal is the human. And the default `Mode(author=R|S|X, …)` gives the author `x`, so
an injected agent can erase its user's entire history: one call, irreversible (W-2), propagating,
with no confirmation, no rate ceiling and no undo. W-8's "delay is the default" is a default, not a
constraint — and a seal with a past deadline erases immediately on arrival (W-7), so any policy gating
`erase` behind approval is bypassed by sealing.

**Structural cause:** the nine bits cannot express a delegated capability subset, because the classes
are author/village/world and **the agent is the author**. There is no way to say "may write and
supersede, never redact", and no way to tell an agent subkey's signature from the human's.

For a system whose premise is automated agents, that is a missing primitive, not a hardening item.

---

## F-7 The erasure mechanism manufactures permanent personal data about erasure requests

`withdrawal.tombstones(target)` has no permission filter and necessarily cannot — tombstones must reach
peers that cannot read the target. Each row carries `target, tier, reason, author, ts`. So the world
learns, permanently and un-erasably (W-2): something existed at this id, it was destroyed, by whom,
when, and why.

The vocabulary makes it worse. `SECRET` is a hunting signal — go find a peer that has not swept.
`SUBJECT_REQUEST` names a person who asked to be forgotten. `LEGAL` discloses that a legal process
exists, which is what a gag order forbids.

**Wrong assumption:** W-9's reasoning about free text is right and **incomplete**. Removing the sentence
does not remove the channel; the code, the actor and the timing leak unconditionally. That is a GDPR
conflict inside the GDPR feature.

Related: erasure has no scope. Ids are content-addressed, so the same assertion in two tenants' stores
is the same id, and a tombstone names only `target`. **"Erase my copy" is inexpressible**, and so is
"decline this erasure" — including under legal hold.

---

## F-8 Two requirements are denial-of-service primitives, and the tests enshrine them

**Poison-pill merge wedge.** `test_E4_a_merge_that_fails_midway_is_reported_incomplete` requires
`IncompleteMerge` when a source holds one id-mismatched row. That row is immutable and
content-addressed, so it never goes away, and skipping it would be a partial union that M-3 forbids
reporting as whole. **One planted row permanently wedges a victim's anti-entropy.** Merge needs
quarantine-and-continue with a reported count.

**Signature discredit.** `test_I2_a_forged_signature_reports_bad_not_unsigned` specifies that any peer
can push `(id, ANYONE, junk)` and move an assertion to `BAD` — which the docstring calls "an incident."
So anyone can discredit any assertion in the network at will, and any author can plausibly deny their
own statements via a sockpuppet. State must be **per-signer** (`verified_by = {…}`), never one verdict
a stranger can pull down. Unbounded, too: N junk signers is storage amplification plus O(N)
verification per read.

---

## F-9 Key rotation and revocation do not exist, so compromise is retroactive and permanent

I-1 puts a raw public key in the frame, so authorship binds to a key forever. Rotating leaves your
entire history authored by a key you no longer control — and that key still holds `author=R|S|X` over
all of it. No key→identity indirection, no expiry, no validity window. And because `ts` is
author-chosen with no trusted timestamp, "signatures after T are invalid" is inexpressible: a stolen
key mints history dated before the compromise.

Falsehood #3 ("an author's key is uncompromised") is present but **understates**: it omits
*permanent, network-wide, irreversible*.

Grants have no issuer authority, expiry or revocation either. And `test_I4_a_signed_grant_confers_
membership` bypasses grants entirely by passing `villages=("home",)` to the constructor — **the host
asserts membership and the state layer trusts it.** I-4 says "signed grants, not an unsigned field";
that is not what the test tests.

---

## F-10 Open question 5 is load-bearing and unanswered

Nothing requires `assertion.author == store.principal`, and the tests do the opposite routinely. Only
a signature separates a claim from a fact — and until "does an unsigned assertion resolve at all?" is
answered *no*, **author-spoofing is a supported operation that wins by rank**.
`test_lying_an_impersonated_author_does_not_verify` asserts only that `sig_state != verified`. It does
not assert the forgery loses resolution or is refused.

---

## F-11 Several existence oracles are requirements, not bugs

- **A-5**: `put` reports `stored=False` when already present. Since the id is a hash of guessable
  fields, "did Alice write X on key K" is a cheap confirmation attack through the ordinary write API
  — `ts` at second resolution costs ~86,400 guesses per day of window.
- **`reader.get`**: `NotFound` vs `Unauthorized` must be distinguishable (E-1, P-6) — a direct
  existence oracle for any id, no guessing needed.
- **P-6 `withheld`**: a per-key census of what you may not see. **M-5 `unprojected`**: activity volume.
  **`forks()`**: arm counts on keys you cannot read.

**Wrong assumption:** these were derived for a single-user local library where the caller already holds
everything. Under multi-tenancy each is a cross-tenant channel, and P-6 and A-5 need re-deriving
against a hostile reader — or the leak needs stating as accepted.

---

## F-12 Connection pooling silently cross-contaminates tenants

P-7 binds the principal at open and the authorizer runs at prepare. `store.py` says so — "a statement
compiled under one principal must never be reused under another" — and leaves the deployment
consequence unstated: every request needs its own connection, its own authorizer installation, and no
shared statement cache. Every mainstream service framework pools by default, and the failure is
silent: tenant A's compiled, authorized statements serving tenant B.

Related: the umask default is `Mode(author=R|S|X, village=R, world=0)` and
`test_P9_the_default_is_not_world_readable` checks only `world == 0`. If "village" maps to a shared hub
population, **the default publishes every note to every tenant in it.** The test checks the wrong half.

---

## What the review credited

Recorded so the fix list is not mistaken for a verdict on the whole design. Content addressing over set
union genuinely buys what `DESIGN.md` §2 claims: idempotent, order-independent, replay-immune merge
with no exactly-once delivery requirement, and tampering that yields a different row rather than a
corrupted one. Author-in-frame, length-prefixed framing, the two-table add/remove split, four
signature states presented as facts rather than judgments, reads that report their own blindness, and
a refusal taxonomy that names its requirement — all correct, several routinely gotten wrong.

W-11 and falsehood #2 were called **exemplary**: stated, bounded, and tested. The README's UNVERIFIED
note was correct to exist and correct to demand a probe.

## The recommended next artifact

Not a patch list — a **threat model for the hosted deployment**, because open questions 3 and 5 are
both security-critical, both unanswered, and both directly on the path of the proposed architecture.
