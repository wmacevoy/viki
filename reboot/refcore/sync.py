"""Anti-entropy: how a peer learns what it lacks and gets it (S series).

MERGER IS THE ALGEBRA; THIS IS THE PROTOCOL. `merger.merge` takes a whole open
store and scans it, which is correct and costs O(N) whatever changed -- and on a
hub, O(N x peers). The sender therefore chose the receiver's CPU cost
(FINDINGS.md F-8). Everything here exists to make the cost proportional to the
DIFFERENCE rather than to the store.

THE SHAPE, and it is deliberately not a log with offsets: a log needs a leader,
and a set does not. Peers stay symmetric, and a hub is a peer that only relays.

  1. each side computes a bounded digest      (S-1, S-3)
  2. compare digests, descend into buckets that disagree
  3. ask for what is missing, by id
  4. transfer, verify what has not been verified, quarantine what fails
     (S-8, S-11, M-7)

A watermark short-circuits step 1 when both peers agree it is current. It is an
OPTIMIZATION over that diff and never a substitute (S-5).
"""

from .model import Digest, SyncReport


def digest(store, buckets: int = 256) -> Digest:
    """S-1, S-3. A bounded summary of what this store holds.

    Bounded is the requirement, not a nicety: shipping every id to discover that
    nothing changed is the cost this exists to avoid. Two peers compare
    O(buckets) hashes and descend only where they disagree, so a converged pair
    exchanges a constant amount and stops (S-2).

    The bucket count is fixed rather than derived from the store size, so a
    digest is the same size for a phone with a thousand assertions and a laptop
    with a million.
    """
    raise NotImplementedError("S-1, S-3")


def lacking(store, remote: Digest) -> tuple:
    """S-1. Returns (ids, complete). The ids this store lacks, and whether that
    list is the whole answer.

    THE FLAG IS THE FIX FOR FINDINGS T-1d, and T-1d itself was overstated. A
    fixed-size sketch CAN recover the exact missing ids -- a 256-cell invertible
    Bloom lookup table, 17 KB regardless of store size, was measured recovering
    120 of 120 -- but only while the difference stays inside its capacity. Past
    that it fails to decode, and the honest interface says so rather than
    returning a short list that looks like an answer.

    Same shape as `SyncReport.bounded`, which the spec already got right for the
    transfer half and forgot for the query half. `complete=False` means "ask
    again after taking these," not "there is nothing more."

    LEARNS WITHOUT RECEIVING. The question and the transfer are separate
    operations, which is what lets a phone decide whether a sync is worth the
    battery before spending it.

    S-5: THIS IS GROUND TRUTH. Whatever a watermark claims, this is the answer,
    and it is the falsifier for every optimization layered on top.
    """
    raise NotImplementedError("S-1, S-5, S-1a")


def watermark(store, peer: str) -> int:
    """S-4. "I have given `peer` everything up to my `seq` of N."

    SENDER-SIDE, and per peer. `seq` orders my receipts, not their writes, so my
    watermark is meaningless to anyone else -- a row that reached me at 40 may
    have reached you at 3 or not at all (C-2). One number for all peers would be
    wrong for every peer but the last one synced.
    """
    raise NotImplementedError("S-4")


def advance(store, peer: str, seq: int) -> int:
    """S-6. Move a watermark forward. ROUNDS DOWN, NEVER UP.

    The two errors are not symmetric: too low costs bandwidth and merge is
    idempotent, while too high silently skips rows and reports success. So this
    refuses to move past what was actually acknowledged rather than trusting a
    caller's arithmetic -- an API must not let the dangerous error be chosen by
    accident.

    Returns the watermark actually set, which may be lower than asked.
    """
    raise NotImplementedError("S-6")


def outstanding(store, peer: str) -> int:
    """What this store believes it still owes `peer`, from the watermark alone.

    A cheap estimate, and S-5 means it is only that: `lacking` is the answer.
    Kept separate so nobody mistakes the estimate for the truth.
    """
    raise NotImplementedError("S-4, S-5")


def export(store, ids: tuple) -> bytes:
    """G-1a, G-1b. Serialize assertions for the wire, using A-3's framing.

    THE SAME ENCODER THAT COMPUTES IDS. One encoder, no options, nothing to
    configure differently on two peers -- which is why CBOR and every other
    canonical-serialization library was refused: "deterministic" is opt-in per
    library, and its failure modes are the exact class that makes two peers
    disagree.
    """
    raise NotImplementedError("G-1a")


def ingest(store, wire: bytes) -> SyncReport:
    """G-1a, G-1b, M-7. Take wire bytes into this store.

    THIS IS THE REAL PATH. Every entry point below that takes two live `Store`
    objects is a convenience over export/ingest, and taking the other side's
    handle is the coupling FINDINGS.md T-12 condemns in the predecessor and
    B-6.6 caught this design reproducing -- a Postgres peer, a hub, or anything
    across a network cannot hold it.

    Verifies ids (A-7), quarantines what fails (M-7), and quarantines rows past
    the clock bound rather than refusing them (C-3c).
    """
    raise NotImplementedError("G-1b, M-7")


def sync(dst, src, *, budget: int = 0, cursor: str = "") -> SyncReport:
    """S-2, S-7, S-8, S-10, S-11. Incremental pull: only what dst lacks.

    S-2 IS THE HEADLINE: two peers that have already converged transfer nothing,
    and the report says so with `transferred == 0` and `complete`.

    S-8: `budget` is the RECEIVER'S bound on rows to take in one call -- zero
    means unbounded. A sender does not get to choose how much verification,
    storage and bandwidth a receiver spends, and stopping on a budget sets
    `bounded` so a truncated sync is never mistaken for a converged one.

    S-7: `cursor` resumes an interrupted sync. Progress is durable, so a phone
    that loses a connection continues rather than starting over.

    S-11: rows already in `verified` are not re-verified. Without this,
    incremental transfer still costs a full verification and "incremental" is a
    claim rather than a property. `reverified` in the report should stay zero on
    a re-sync, and a nonzero value means the optimization is not working.

    S-10: refuses a `never` diary (D-2) and refuses two different diary
    identities (M-8), before any bytes move.

    G-1b: a convenience over `export`/`ingest` for the case where both stores
    are in one process, NOT a second path. If the two ever disagree, this one is
    wrong.
    """
    raise NotImplementedError("S-2, S-7, S-8")


def push(src, dst, *, budget: int = 0, cursor: str = "") -> SyncReport:
    """M-6, S-2. The same operation with the ends swapped.

    Everything that happens on receipt happens to the destination -- the sweep
    lands there and their arrival clock ticks. The watermark, though, is the
    SENDER's: after a push, `src` records how far it has given `dst`.
    """
    raise NotImplementedError("M-6, S-4")


def relay(hub, src, *, budget: int = 0) -> SyncReport:
    """S-9. A hub carries rows it cannot read.

    IT MUST STILL SWEEP. A hub that does not apply tombstones re-serves erased
    content to every peer that pulls from it, which is the deployment failure
    REQUIREMENTS_v2 open question 3 asks about and the one thing a relay cannot
    be excused from. Holding no key does not excuse it, because a tombstone
    names its target by id and needs no plaintext to act on.
    """
    raise NotImplementedError("S-9")


def state(store, peer: str) -> dict:
    """D-6, S-4. What this store knows about its relationship with one peer:
    watermark given, last successful sync, whether a resume is pending."""
    raise NotImplementedError("D-6, S-4")
