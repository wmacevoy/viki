"""References and the cache (X series). None of this existed in v1.

THE CACHE BREAKS THE OLD PROJECTION RULE. "Derived, rebuildable, disposable" was
true of chunks, which regenerate from stored text. A cache of an email does NOT
regenerate once the message is deleted, so it is a projection that can become
permanently unrebuildable -- a category the design did not have, and the reason
`pin` exists (X-5, X-6).
"""

from .model import Reference, RefState


def state(store, ref: Reference) -> RefState:
    """X-2. CACHED, AWAY or GONE.

    X-3: AWAY and GONE are never reported as each other. A phone with an empty
    cache must not read as a mailbox that was emptied. An absent cache row means
    AWAY; GONE is a fact the source told us and is recorded.
    """
    raise NotImplementedError("X-2, X-3")


def content(store, ref: Reference) -> bytes:
    """Cached bytes, fetching if a Fetcher is retained.

    Raises Gone when the source says so. Returns nothing and reports AWAY when
    it simply is not here -- degraded operation is a required path, not a
    failure (X-8).
    """
    raise NotImplementedError("X-2")


def cache_put(store, ref: Reference, data: bytes) -> None:
    """X-4. Bounded, evictable, and NEVER merged. Each device caches
    independently, which is what lets the phone hold every assertion and almost
    no content."""
    raise NotImplementedError("X-4")


def evict(store, budget: int) -> int:
    """X-4, X-5. Evict to fit the stated bound. Returns how many went.

    X-5: EVICTION MAY BE IRREVERSIBLE, and this says so at the point of
    eviction. Evicting the last copy of content whose source is GONE raises
    Unrebuildable rather than proceeding quietly -- the caller is destroying
    something, and it should have to know.
    """
    raise NotImplementedError("X-4, X-5")


def pin(store, ref: Reference) -> None:
    """X-6. Exempt from eviction: the deliberate act of deciding to carry
    something. A pinned item is truth rather than cache."""
    raise NotImplementedError("X-6")


def stale(store, ref: Reference) -> bool | None:
    """X-7. Has the source changed since we wrote about it?

    None means the system offers no version, which is a FACT to report rather
    than a value to invent. Without this a summary silently describes a document
    that no longer says that.
    """
    raise NotImplementedError("X-7")


def mark_gone(store, ref: Reference) -> None:
    """X-2, W-14. The source deleted it or revoked access.

    NOT an erasure: withdrawal is a decision, unavailability is a fact. The same
    distinction the parking material draws between a sensor's self-report and a
    consumer's conclusion about reachability.
    """
    raise NotImplementedError("W-14")
