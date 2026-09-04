"""What the host supplies. The layer links no crypto, opens no file, and calls
no clock (C-1).

Small protocols rather than one context object: a caller that only reads must
never be asked for a Signer, and one that never seals must never be asked for a
Custodian.
"""

from typing import Protocol, runtime_checkable


@runtime_checkable
class Clock(Protocol):
    """C-1. The only source of time, so a test drives it rather than waits."""

    def now(self) -> str:
        """ISO-8601 UTC. Lexical order is time order."""


@runtime_checkable
class Signer(Protocol):
    """I-1, I-2."""

    def principal(self) -> str:
        """This identity's public key. What lands in the frame as author."""

    def sign(self, purpose: str, id_hex: str) -> bytes | None:
        """Signs (purpose || id), NEVER a bare id.

        Without domain separation a signature made over an assertion authorizes
        a tombstone carrying the same id (FINDINGS.md F-2). `purpose` is
        "assert", "tombstone", "grant" or "countersign".

        None means declined -- a locked keychain, a cancelled prompt. The
        assertion is stored unsigned, and under I-7 an unsigned assertion does
        not resolve.
        """


@runtime_checkable
class Verifier(Protocol):
    """I-3. Verification needs no secret, so it is a separate port."""

    def verify(self, pubkey: str, purpose: str, id_hex: str, sig: bytes) -> bool: ...


@runtime_checkable
class Custodian(Protocol):
    """W-4. Seals move content to the heap under a key wrapped to a custodian;
    expiry destroys the wrap, not the payload, so a large item expires in
    constant time."""

    def wrap(self, content_key: bytes, recipient: str) -> bytes: ...

    def unwrap(self, wrapped: bytes) -> bytes | None: ...


@runtime_checkable
class Fetcher(Protocol):
    """X-2. Resolves a reference against its external system.

    Returns bytes when content is available, None when it is AWAY (offline,
    rate-limited, not yet fetched), and raises Gone when the source says the
    thing is deleted or access is revoked. That three-way split is the whole
    contract: an adapter returning None for a deleted message makes a purge look
    like a network hiccup forever (X-3).
    """

    def fetch(self, reference) -> bytes | None: ...

    def version(self, reference) -> str | None:
        """The current etag, or None if this system offers none. Inventing one
        is worse than reporting its absence (X-7)."""


@runtime_checkable
class KindSpec(Protocol):
    """R-6, C-4. One per kind. The single extension point.

    Unlike v1's, this one is registered on the Store and consulted by `put`, so
    it is wired rather than declared -- v1's was referenced by zero modules and
    zero tests (FINDINGS.md C-8).
    """

    name: str

    def akey(self, body: bytes, reference) -> str: ...

    def rank(self, ts: str, body: bytes) -> str:
        """Declared per kind, not implicitly the timestamp. A kind that can
        carry a logical counter should, so skew moves timing not outcome."""

    def canon(self, body: bytes) -> bytes: ...
