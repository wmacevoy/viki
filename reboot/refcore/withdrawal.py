"""The exception to grow-only, bought at a stated price.

The store is a 2P-Set: merge stays associative, commutative and idempotent, but
a withdrawn id can never be re-added.

THE IMMUNE CLASS IS LARGER THAN TOMBSTONES. It is everything authority is
derived from -- tombstones AND grants. v1 immunized only tombstones, so erasing
a grant made the sweep order-dependent: the same set reaching two terminal
states depending on sweep order (FINDINGS.md C-2). Nothing in this module
deletes from `tombstone` or `grant`.

WHAT IT CANNOT PROMISE (W-11): a peer that never merges again keeps its copy,
and a peer that merges can decline to sweep. Best-effort by construction, and
any stronger claim would be false.
"""

from .model import Reason, SweepReport


def withdraw(store, target_id: str, reason: Reason) -> str:
    """W-1. Hide it, keep it, allow it back. What most deletes actually mean."""
    raise NotImplementedError("W-1")


def restore(store, tombstone_id: str) -> str:
    """W-1. The verb v1 advertised and never had.

    Its test passed only when nothing was restored: the view consulted the
    tombstone table rather than whether the tombstone was superseded, so the
    target stayed hidden and the broken implementation went green while the
    correct one failed (FINDINGS.md C-4).
    """
    raise NotImplementedError("W-1")


def seal(store, target_id: str, reason: Reason, custodian: str,
         deadline: str = "") -> str:
    """W-4, W-7, W-8. Move the content to the heap under a wrapped key.

    W-4: SEALED CONTENT LEAVES `assertion`. v1 sealed IN PLACE, which
    contradicted A-6 and broke A-7 -- the row's id no longer hashed its content,
    so one sealed row poisoned every merge that peer sourced (FINDINGS.md C-3).

    `deadline` is absolute, from this tombstone's ts (C-5), so a peer receiving
    an already-expired seal erases immediately and one offline for a year erases
    on first wake. No agreement on time is needed; skew moves timing, never
    outcome.

    W-8: this is the default tier.
    """
    raise NotImplementedError("W-4, W-7, W-8")


def unseal(store, target_id: str, custodian) -> bytes:
    """W-1. The custodian's key opens the heap entry. Anyone else: Unauthorized."""
    raise NotImplementedError("W-1")


def erase(store, target_id: str, reason: Reason) -> str:
    """W-3, W-5. Destroy the assertion, its heap entry, its cache entries and
    its projection.

    W-3 is easy to get wrong in the direction that matters: erasing the
    assertion and leaving the payload destroys the DESCRIPTION and keeps the
    content -- inverted for the purpose the feature exists to serve.

    W-8: reachable only on explicit request. The default is a seal with a
    deadline, which destroys nothing a real erase cannot finish and provides an
    undo. Immediate erasure is for the case where the window IS the risk.
    """
    raise NotImplementedError("W-3, W-5")


def erase_reference(store, ref, reason: Reason) -> str:
    """W-12. A tombstone may target a REFERENCE, and everything naming it is
    findable through the derivation edges.

    This is how erasing a source email reaches the summary derived from it. A
    content-addressed tombstone cannot: the derivative holds different bytes.
    """
    raise NotImplementedError("W-12")


def sweep(store) -> SweepReport:
    """Apply every stored tombstone whose target is present. W-2, W-5, W-6, W-13.

    W-5 IS THE POINT. A tombstone takes effect only on a signature verified over
    (purpose || id) from a principal holding `x`. Without it, erasure is not a
    privacy mechanism but a censorship primitive: anyone who can write to any
    store that ever merges into yours destroys anything whose id they know. An
    unauthorized tombstone is STORED and INERT and counted -- kept, because an
    attempted censorship is evidence.

    W-13: erasing a source FLAGS its derivatives rather than destroying them. A
    summary covering ten emails must not vanish because one was deleted; that
    decision belongs to a person.

    W-10: no arrival sequence number may survive its assertion, or the store
    advertises something it cannot produce.
    """
    raise NotImplementedError("W-2, W-5, W-13")


def promote_expired(store, now: str) -> int:
    """W-7. Seals past their deadline become erasures, on this peer's own clock.

    Destroys the wrapped key -- 32 bytes -- rather than rewriting the payload,
    so a large item expires in constant time and the shredding reaches backups.
    """
    raise NotImplementedError("W-7")


def residue(store, target_id: str) -> tuple:
    """What of `target_id` still remains anywhere. Empty after a complete erase.

    "Show me that it is gone" is the question an auditor actually asks, and a
    claim of erasure that cannot be demonstrated is the anxiety-reducing
    artifact with the risk left in place.
    """
    raise NotImplementedError("W-3")


def tombstones(store, target: str) -> tuple:
    """Every tombstone naming `target`, in arrival order.

    Unfiltered by necessity -- tombstones must reach peers that cannot read the
    target. FINDINGS.md F-7 records what that publishes permanently, and W-9's
    closed vocabulary is only half an answer: the code, the actor and the timing
    still leak. Open in REQUIREMENTS_v2.
    """
    raise NotImplementedError("W-9")


def local_note(store, tombstone_id: str, text: str) -> None:
    """W-9. The free-text explanation. Stored here, NEVER merged.

    A required propagating reason is mandatory, grow-only, forever, and the
    natural thing to write in it describes the very thing being erased. The code
    travels; the sentence stays home.
    """
    raise NotImplementedError("W-9")
