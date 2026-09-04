"""Union. The whole of sync that belongs in a state layer.

How the other store arrived -- HTTP, a cable, a hub -- is the host's business.
"""

from .model import MergeReport


def merge(dst, src) -> MergeReport:
    """M-1..M-8. Pull: their rows into mine.

    Identity is a content hash, so this is insert-or-ignore and there is no
    conflict resolution to get wrong.

    Order within a merge is load-bearing:
      1. refuse if either end declares `never` (D-2) or they are different
         diaries (M-8)
      2. assertions, ids verified against content (A-7)
      3. tombstones, ids verified under their own tag (A-8)
      4. grants -- immune, and needed before the sweep can judge authority
      5. signatures -- BEFORE the sweep, so a tombstone arriving in this merge
         is judged against the authority that arrived with it
      6. sweep (W-5, W-6)
      7. promote expired seals (W-7)
      8. stamp arrivals (W-10)

    M-7: A BAD ROW IS QUARANTINED AND COUNTED, NOT AN ABORT. v1 required abort,
    which made one planted row a permanent denial of sync: the row is immutable
    and content-addressed so it never goes away, and skipping it would be a
    partial union that M-3 forbids reporting as whole (FINDINGS.md F-8). A
    quarantine count is the third answer -- honest about what did not land, and
    not a wedge.

    M-3: a source scan ending in error still reports `complete=False`.
    """
    raise NotImplementedError("M-1, M-7")


def push(src, dst) -> MergeReport:
    """M-6. Mine into theirs -- one operation with the ends swapped.

    Everything that happens on receipt happens to the DESTINATION: the sweep
    lands there, and their arrival clock ticks for what they received. Mine does
    not move; I learned nothing.

    NOTE what is NOT here: v1's M-7, "a peer relays what it cannot read." It was
    written for a network the use cases do not describe, and composed with full
    replication it was the top finding in both reviews -- every peer holding
    every other participant's plaintext. Diaries are the replication unit now,
    and most of them never leave (D-1, D-2).
    """
    raise NotImplementedError("M-6")
