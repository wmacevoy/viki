"""Framing and hashing. No database, no clock, no I/O -- so A-3, A-4 and A-8 are
testable without a store."""

from .model import Assertion, Tombstone

# A-8. Distinct tags, so bytes framed for one purpose can never be read as the
# other. This is the domain separation whose absence let a signature over an
# assertion authorize a tombstone carrying the same id.
TAG_ASSERTION = b"viki.assertion.v2"
TAG_TOMBSTONE = b"viki.tombstone.v2"
TAG_GRANT = b"viki.grant.v2"


RANK_BYTES = 16


def normalize(text: str) -> bytes:
    """A-4, A-4a. NFC, then UTF-8 bytes. Returns BYTES, not str.

    The return type is the requirement: every value that is hashed, compared or
    ordered is bytes, so nothing downstream can accidentally hash a str under
    one engine's encoding assumptions.

    Decomposed and precomposed forms are different bytes for the same visible
    string, and macOS filesystems and input methods emit the decomposed one
    routinely. Two peers then write the same note and get two assertions.
    """
    raise NotImplementedError("A-4, A-4a")


def check_rank(rank: bytes) -> None:
    """A-2a, C-4. Refuse a rank that is not exactly RANK_BYTES bytes.

    A width rather than a convention. It makes ordering bytewise on every
    engine, removes prefix ambiguity, and converts "configure your collation
    correctly" -- a deployment constraint, invisible when wrong -- into a
    write-time refusal in one place.

    A-2b: the encoding is stated, not left to the caller. Big-endian ms since
    epoch (6), a logical counter (8), a kind-defined tiebreak (2). "Sixteen
    bytes, your problem" would reinstate C-2's original defect one level up.
    """
    raise NotImplementedError("A-2a, C-4")


def frame(tag: bytes, *fields: bytes) -> bytes:
    """A-3. Length-prefixed. No field value can forge a boundary.

    The tag is part of the frame, not a prefix bolted on: framing under one tag
    must never equal framing under another (A-8).
    """
    raise NotImplementedError("A-3")


def compute_id(a: Assertion) -> str:
    """A-1, A-2. sha256 over TAG_ASSERTION and
    (author, kind, akey, ts, rank, reference, supersedes*, body).

    `supersedes` is a length-prefixed SORTED list, so the same reconciliation
    written by two peers is the same assertion rather than two (T-1b).

    `rank` is in there. Outside it, two hosts with different rank functions
    produce identical ids carrying different ranks -- same set, different
    winner, and no integrity check fires because the ids agree.
    """
    raise NotImplementedError("A-1, A-2")


def compute_tombstone_id(t: Tombstone) -> str:
    """A-8. sha256 over TAG_TOMBSTONE and the tombstone's own fields.

    v1 gave tombstones their own table and no framing rule, so A-7 had no
    definition of `content` for the one row shape that destroys things.
    """
    raise NotImplementedError("A-8")


def verify_id(obj) -> bool:
    """A-7, A-8. Does this object's id match its content, under its own tag?

    Checked on arrival: a merge source is not trusted.
    """
    raise NotImplementedError("A-7")


def check_timestamp(ts: str, *, received_at: str | None = None) -> None:
    """C-3. Refuse a malformed or non-UTC timestamp, raising BadTimestamp.

    `received_at` bounds an author-chosen ts against the receiving peer's own
    clock. Without it an attacker sets ts far in the future and owns an akey
    forever, and no supersession is involved so R-4's authority check never runs
    (FINDINGS.md F-4).
    """
    raise NotImplementedError("C-3")
