"""Adding assertions. There is no update path."""

from .model import Assertion, PutOutcome, Reference


def put(store, assertion: Assertion) -> PutOutcome:
    """Store an assertion. A-1..A-7, C-3, D-3, E-1.

    Computes the id from the registered KindSpec's rank, checks the timestamp
    against the receiving clock, signs with purpose "assert" if a Signer is
    retained, records derivation edges (V-1), and stamps arrival.

    A-5: storing something already present is a no-op AND is reported as one.
    E-1: any other suppressed constraint means the row is missing, so verify it
    is there rather than trusting that the insert returned.
    """
    raise NotImplementedError("A-1, A-5, E-1")


def note(store, text: str, *, akey=None, reference: Reference | None = None,
         derived_from=()) -> PutOutcome:
    """The convenience. Kind "note"; rank from the registered KindSpec."""
    raise NotImplementedError("A-1")


def supersede(store, target_id: str, assertion: Assertion) -> PutOutcome:
    """Write an assertion that follows `target_id`. R-4, R-8.

    THE ONLY MUTATION VERB, and it destroys nothing: the predecessor stays and
    both are in the log view. Structuring, closing, correcting, appending to a
    conversation and replacing a file version are all this one operation --
    which is what makes R-8's two views one mechanism rather than two models.

    Refuses when the target does not exist, so a typo does not become a dangling
    edge nobody notices.
    """
    raise NotImplementedError("R-4, R-8")


def countersign(store, id_hex: str) -> None:
    """I-2, I-5. The retained identity adds its own signature row.

    A countersigner does not become the author. Signatures are keyed
    (id, signer, purpose) for exactly this reason.
    """
    raise NotImplementedError("I-2")
