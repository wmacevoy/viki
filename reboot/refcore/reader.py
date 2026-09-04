"""Folds over the set.

R-8 IS THE SHAPE OF THIS MODULE. A chain of successors carries two views over
one mechanism: `log` returns every entry in order and hides nothing; `current`
returns the head alone. A conversation and a file's version history are the
same edges read two ways, not two data models -- which is what lets a reader
who has learned supersession guess how a log works and be right.
"""

from .model import Assertion, Fork, Result, SigState


def get(store, id_hex: str) -> Assertion:
    """One assertion by id. NotFound, or Unauthorized without `r` on the diary."""
    raise NotImplementedError("D-3")


def current(store, akey: str) -> Assertion:
    """R-1. THE REPLACEMENT VIEW: the head alone.

    The single unsuperseded arm of the chain; raises Forked when there is more
    than one. RANK IS NOT CONSULTED HERE -- a chain has one head. Deterministic
    fork resolution is `resolve`, and separating them is what makes R-1 and R-5
    satisfiable together (FINDINGS.md C-7).

    I-7: an unsigned assertion never becomes current. It is stored, readable and
    reported as a claim -- otherwise author-spoofing is a supported operation
    that wins by rank (FINDINGS.md F-10).
    """
    raise NotImplementedError("R-1, R-5, I-7")


def resolve(store, akey: str) -> tuple:
    """R-2, R-3. Deterministically pick among the arms of a fork.

    Returns (assertion, was_forked). Ordered `arank DESC, id DESC` -- an
    explicit total order, because ties are not exotic (two writes in one clock
    tick, two offline peers whose hosts emit the same ISO string) and an
    implicit tiebreak is determinism by accident of index shape.

    This exists separately from `current` because a summary on a phone must
    produce an answer without prompting anyone, while an audit must be told the
    disagreement existed. v1 put the tiebreak on `current` AND required a unique
    arm, which made it unreachable code (FINDINGS.md C-7).
    """
    raise NotImplementedError("R-2, R-3")


def log(store, akey: str) -> Result:
    """R-8. THE LOG VIEW: every entry on the chain, in order, nothing hidden.

    R-9: A READ WHOSE CORRECTNESS DEPENDS ON HISTORY MUST USE THIS. Anyone
    holding `s` can change what `current` returns without destroying anything,
    so an audit or security decision reading only the head is flippable while
    the tampering sits visible one view away. Which view a consumer reads is
    part of that consumer's contract, not a display preference.
    """
    raise NotImplementedError("R-8, R-9")


def forks(store, akey: str) -> Fork | None:
    """R-5, R-7. The arms of a disagreement, or None.

    In a log this means two writers raced, which is worth surfacing rather than
    preventing. Healable by any domain member holding `s` -- v1 made a fork
    permanent and unhealable, a denial of service costing one row (F-4).
    """
    raise NotImplementedError("R-5, R-7")


def sig_states(store, id_hex: str) -> dict:
    """I-2. PER SIGNER: {signer: SigState}.

    Never one verdict for an assertion. A single value let any peer push a junk
    signature and move any assertion to BAD, discrediting it network-wide, and
    let any author plausibly deny their own statements via a sockpuppet
    (FINDINGS.md F-8). An empty dict means unsigned, which under I-7 means it
    does not resolve.
    """
    raise NotImplementedError("I-2")


def unprojected(store) -> int:
    """M-5. Assertions written but not indexed.

    A partially projected store is not distinguishable by any TOTAL: both counts
    are nonzero and a written-but-unindexed assertion reads exactly like one
    that was never written. A merge produces that state by construction.
    """
    raise NotImplementedError("M-5")
