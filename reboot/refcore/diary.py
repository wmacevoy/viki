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
