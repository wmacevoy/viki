"""Sync policy and access. The primary security mechanism (D series).

v1 made per-assertion permissions the boundary. Both reviews showed a filter
inside the process cannot confine anything that holds the bytes, and the use
cases never asked for it: the sensitive diaries do not sync, and the ones that
do are replicated inside one trust domain where everyone holds the key.
"""

from .model import Grant, SyncPolicy


def policy_of(store, diary: str) -> SyncPolicy:
    """D-1. A diary declares `never`, `domain` or `published`."""
    raise NotImplementedError("D-1")


def declare(store, diary: str, policy: SyncPolicy, endpoint: str = "") -> None:
    """D-1. The declaration is enforced by the code that syncs, not documented."""
    raise NotImplementedError("D-1")


def refuse_sync(store, other) -> None:
    """D-2. Raise NoSyncPath if either end is declared `never`.

    Not a flag and not an override: a `never` diary has no sync path at all.
    """
    raise NotImplementedError("D-2")


def rights(store, principal: str, diary: str) -> int:
    """D-3. The r|s|x this principal holds on this diary, from signed grants.

    I-4: from grants, not from an unsigned constructor argument. v1's positive
    control passed membership in as a tuple, so the host asserted membership and
    the state layer trusted it -- which is precisely the unsigned field I-4
    forbids (FINDINGS.md F-9).
    """
    raise NotImplementedError("D-3, I-4")


def may(store, right: int, diary: str | None = None) -> bool:
    """D-3. Whether the store's principal holds `right`.

    D-3c: `s` is safe to grant because it destroys nothing -- a superseding
    assertion adds an entry and the predecessor stays in the log view (R-8).
    Only `x` removes.
    """
    raise NotImplementedError("D-3")


def grant(store, principal: str, diary: str, r: int, expires: str = "") -> str:
    """D-5. A grant is an assertion in a diary the granting human controls, so
    an agent's authority is traceable to something a person signed, and revoking
    it is a superseding grant.

    W-2: grants are IMMUNE to withdrawal. Authority derived from erasable
    content makes the sweep order-dependent -- the same set reaching two
    terminal states depending on sweep order (FINDINGS.md C-2).
    """
    raise NotImplementedError("D-5, W-2")


def describe(store) -> dict:
    """D-6. Policy, domain, and last successful sync per peer.

    A store that cannot say where it sends its contents is not usable.
    """
    raise NotImplementedError("D-6")


def vocabulary(store, diary: str) -> tuple:
    """D-3b. The stated vocabulary of a diary an agent may write.

    Free text is unbounded bandwidth; an enum and a score are close to none. The
    residual covert channel is this vocabulary's entropy times the number of
    items processed -- stated, not assumed away.
    """
    raise NotImplementedError("D-3b")


# ---- N series: the root of authority ---------------------------------------
#
# The anchor is the CONTAINER, not the content. A row can claim anything; a row
# cannot let you open a database. That asymmetry is the whole mechanism.

def roots(store) -> tuple:
    """N-3. The root-authority public keys: the recipients the database key is
    wrapped to.

    Read from `dbkey_wrap`, which is container structure and never merged — so a
    peer cannot promote itself by writing a row, because it would have to wrap a
    key it does not hold.
    """
    raise NotImplementedError("N-3")


def add_root(store, recipient: str, wrapped: bytes) -> None:
    """N-3, N-9. Wrap the database key to another recipient.

    Requires the database key, which means **anyone in the domain can do this.**
    Stated rather than defended: root authority is exactly as strong as
    database-key custody and no stronger, which is the same guardrail-not-boundary
    line the project already draws.
    """
    raise NotImplementedError("N-3, N-9")


def identity_put(store, name: str, pubkey: str,
                 secret_wrapped: bytes | None = None, kdf: str = "") -> str:
    """N-1, N-8. Record an identity.

    `secret_wrapped` is this identity's private key under a PASSWORD, so N-8's
    slow KDF applies — and must never share a code path with N-7's raw database
    key. One "unlock" function treating both alike is the single most likely way
    to get this wrong.
    """
    raise NotImplementedError("N-1")


def authoritative(store, pubkey: str) -> bool:
    """N-2. Is this identity's row signed by a root-authority key?

    An unsigned or unrootable identity is stored, readable, and confers nothing.
    """
    raise NotImplementedError("N-2")


def may_grant(store, issuer: str, diary: str, rights: int) -> bool:
    """N-4, N-5. May `issuer` confer exactly these rights?

    Only when the issuer's identity is authoritative (N-2) **and the issuer
    already holds every right being granted**. Delegation narrows; it never
    escalates. Root authorities hold everything, which is what terminates the
    chain rather than leaving it circular.
    """
    raise NotImplementedError("N-4, N-5")


def inert_grants(store) -> tuple:
    """N-6. Grants stored but not honored for want of a path to a root.

    Kept and counted, not discarded — the same shape as an unauthorized
    tombstone, and for the same reason: an attempted escalation is evidence.
    """
    raise NotImplementedError("N-6")
