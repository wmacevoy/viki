"""Derivation and publication (V series).

THE PUBLICATION BOUNDARY IS THE SECURITY BOUNDARY. Both use cases are the same
shape: a sensitive store that never moves, and a smaller derived artifact that
does. What crosses is therefore the only thing an adversary downstream can ever
see, and it must be visible rather than a side effect.
"""

from .model import Publication


def record(store, derived_id: str, sources: tuple) -> None:
    """V-1, V-1a. Recompute the edge rows from the assertion's FRAMED sources.

    Never merged as data: `derived_from` is inside the frame (A-2), so a peer
    re-derives the edges from the assertion it received rather than trusting a
    table. Unmerged and unframed -- which is how this started -- the edges are
    unauthenticated AND `derivatives_of()` comes back empty after a merge,
    silently disabling W-13's flagging on exactly the peer that received the
    summary (FINDINGS.md B-6.3).

    Needed three ways: explaining why the calendar says this, reaching a summary
    when its source is erased (W-12), and auditing what an agent published.
    """
    raise NotImplementedError("V-1")


def sources_of(store, derived_id: str) -> tuple:
    raise NotImplementedError("V-1")


def derivatives_of(store, source: str) -> tuple:
    """The reverse edge. This is how erasing a source email reaches the summary
    computed from it -- a content-addressed tombstone cannot, because a
    derivative holds different bytes (W-12)."""
    raise NotImplementedError("V-1, W-12")


def publish(store, dest, assertion_ids: tuple) -> Publication:
    """V-2, V-3, V-4. Move assertions into a diary with a wider sync policy.

    An EXPLICIT operation, never a side effect of an ordinary write. Refuses
    with NotPublication when content would cross from a `never` diary without
    being named a publication -- nothing crosses by accident.

    Returns the reviewable record: which assertions, derived from what, into
    which diary. V-6: the blast radius of a prompt injection is what reached a
    syncing diary, and this makes that enumerable rather than argued about.
    """
    raise NotImplementedError("V-2, V-3, V-4")


def published(store, since: str = "") -> tuple:
    """V-3. What has crossed. The audit read, and it uses the log view (R-9)."""
    raise NotImplementedError("V-3")
