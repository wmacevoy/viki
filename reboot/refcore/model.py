"""Data, and no behavior.

Everything here is constructible. Nothing here decides anything.
"""

from dataclasses import dataclass, field
from enum import Enum

# D-3. Three rights, per principal, per diary. There is no `w`: nothing is ever
# written to. There is no `a` either -- adding and superseding are one right,
# because a superseding assertion DESTROYS NOTHING and the predecessor stays in
# the log view (R-8). Only `x` removes.
R = 4
S = 2
X = 1


class SyncPolicy(Enum):
    """D-1. Declared per diary and enforced by the code that syncs."""

    NEVER = "never"           # no sync path exists at all (D-2)
    DOMAIN = "domain"         # replicated among named principals, one trust domain
    PUBLISHED = "published"   # pushed to a named hub


class RefState(Enum):
    """X-2. Three states, never conflated.

    AWAY and GONE are the pair that matters: a phone with an empty cache must
    not read as a mailbox that was emptied (X-3).
    """

    CACHED = "cached"   # the bytes are here
    AWAY = "away"       # the reference is known, the bytes are not here
    GONE = "gone"       # deleted at the source, or access revoked


class Tier(Enum):
    """W-1."""

    WITHDRAW = "withdraw"   # hidden, reversible, a real restore verb exists
    SEAL = "seal"           # moved to the heap, custodian-recoverable
    ERASE = "erase"         # destroyed


class Reason(Enum):
    """W-9. A closed vocabulary that propagates. Free text stays local.

    PROVISIONAL -- REQUIREMENTS_v2 does not yet settle this list.
    """

    MISTAKE = "mistake"
    SECRET = "secret"
    SUBJECT_REQUEST = "subject"
    LEGAL = "legal"
    POLICY = "policy"


class SigState(Enum):
    """I-2. Reported PER SIGNER, never as one verdict for an assertion: a single
    value let any peer push a junk signature and move any assertion to BAD,
    discrediting it network-wide (FINDINGS.md F-8)."""

    VERIFIED = "verified"
    BAD = "bad"
    UNKNOWN = "unknown"     # signed by an identity this diary does not hold


@dataclass(frozen=True)
class Reference:
    """X-1. A stable name for content held elsewhere. Inside the frame; the
    content is not."""

    system: str            # "gmail", "graph", "drive"
    ident: str             # stable id within that system
    version: str = ""      # etag. "" means the system offers none -- which is a
                           # FACT to report, not a value to invent (X-7)


@dataclass(frozen=True)
class Grant:
    """D-3, D-5, I-4. An assertion in a diary the granting human controls, so an
    agent's authority is traceable to something a person signed. W-2: grants are
    immune to withdrawal, or authority is derived from erasable content and the
    sweep becomes order-dependent (FINDINGS.md C-2)."""

    principal: str
    diary: str
    rights: int
    ts: str = ""
    expires: str = ""      # I-6. "" means no stated expiry


@dataclass(frozen=True)
class Assertion:
    """A-2 frames author, kind, akey, ts, rank, reference, supersedes and body.

    `rank` is IN the frame. Left outside, a host-supplied rank function produces
    identical ids with different ranks -- same set, different winner, and
    undetectable because the ids agree (FINDINGS.md C-2).

    `supersedes` IS A TUPLE, and that is the fix for FINDINGS T-1b. With a single
    parent, healing a two-arm fork means writing two assertions that differ only
    in which arm they supersede -- so they get distinct ids, both are
    unsuperseded, and the fork survives. One node reconciling two lines is a real
    thing a single parent cannot say. Fossil has carried it since 1996 in a
    check-in's P card (primary parent plus N merge parents); both successors
    dropped it (FINDINGS T-4).

    Framed as a length-prefixed SORTED list (A-2), so the same reconciliation
    written by two peers is the same assertion.

    `id` is carried here as lowercase hex because that is the natural handle,
    and stored as its UTF-8 bytes (A-4a). Hex is ASCII, so byte order over the
    stored form and lexical order over this string agree -- which is what makes
    R-2's `id DESC` tiebreak mean the same thing in both places.
    """

    author: str
    kind: str
    akey: str
    ts: str
    rank: bytes            # A-2a: EXACTLY 16 bytes, refused otherwise
    body: bytes
    reference: Reference | None = None
    supersedes: tuple = ()      # T-1b: MULTI-PARENT. See the note below.
    derived_from: tuple = ()      # V-1. ids and references this was computed from
    id: str = ""


@dataclass(frozen=True)
class Tombstone:
    """A-8. Its own table AND its own framing rule.

    v1 split the table for W-2 and never wrote the second framing rule, which
    let any signature the victim had ever produced authorize an erasure
    (FINDINGS.md F-2). Signing covers (purpose || id), not a bare id.
    """

    author: str
    target: str
    target_kind: str      # "assertion" or "reference" (W-12)
    tier: Tier
    reason: Reason
    ts: str
    deadline: str = ""    # W-7, C-5. Absolute, from this tombstone's ts
    id: str = ""


@dataclass(frozen=True)
class Result:
    """E-2. A read that knows its own blindness -- in the RESULT, not the docs.

    Four channels because there are four ways to be partly blind, and v1 had two.
    """

    rows: tuple = ()
    unprojected: int = 0    # M-5. written but not indexed
    unreadable: int = 0     # sealed, or in the heap
    uncached: int = 0       # X-8. references whose content is away or gone


@dataclass(frozen=True)
class PutOutcome:
    """A-5. `stored` is False when the assertion was already present -- which
    must be distinguishable from a dropped write (E-1)."""

    id: str
    stored: bool


@dataclass(frozen=True)
class MergeReport:
    """M-3, M-5, M-7."""

    added: int = 0
    signatures_added: int = 0
    quarantined: int = 0    # M-7. bad rows set aside, NOT an abort
    swept: int = 0
    unprojected: int = 0
    complete: bool = False


@dataclass(frozen=True)
class SweepReport:
    """W-5. `inert` counts tombstones stored but not honored for want of
    authority -- kept, because an attempted censorship is evidence."""

    withdrawn: int = 0
    sealed: int = 0
    erased: int = 0
    flagged: int = 0        # W-13. derivatives of an erased source
    inert: int = 0


@dataclass(frozen=True)
class Publication:
    """V-3. The reviewable record of what crossed the boundary."""

    source_diary: str
    dest_diary: str
    assertions: tuple = ()
    derived_from: tuple = ()
    ts: str = ""


@dataclass(frozen=True)
class Digest:
    """S-1, S-3. A BOUNDED summary of what a store holds.

    Bounded is the whole point: shipping a list of every id to discover that
    nothing changed is exactly the cost this exists to avoid. `ranges` are
    bucket hashes, so two peers compare O(buckets) values and descend only into
    buckets that disagree.
    """

    diary: str
    root: str = ""          # one hash over the whole set
    ranges: tuple = ()      # ((lo, hi, hash), ...) -- bucket digests
    count: int = 0


@dataclass(frozen=True)
class SyncReport:
    """S-2, S-7, S-8.

    `cursor` is the resume token, empty when finished. `bounded` says the
    receiver stopped on its own budget rather than at the end -- reported,
    because a truncated sync is otherwise indistinguishable from a converged
    one.
    """

    transferred: int = 0
    already_held: int = 0
    quarantined: int = 0
    reverified: int = 0     # S-11. should stay 0 on a re-sync
    complete: bool = False
    cursor: str = ""
    bounded: bool = False


@dataclass(frozen=True)
class Fork:
    """R-5. Two or more unsuperseded arms on one akey. In a log (R-8) this means
    two writers raced, which is worth surfacing rather than preventing."""

    akey: str
    arms: tuple = field(default_factory=tuple)
