"""The refusal taxonomy.

E-3: every refusal names the requirement that refused it, INCLUDING the base
class. v1's `Refused` carried an empty string and was the most-raised class in
the suite, so the requirement it was meant to satisfy was violated by the
mechanism meant to satisfy it (FINDINGS.md C-7).
"""


class StateError(Exception):
    requirement = "E-3"

    def __init__(self, message="", requirement=None):
        super().__init__(message)
        if requirement:
            self.requirement = requirement
        if not self.requirement:
            raise AssertionError("E-3: a refusal must name its requirement")


class Refused(StateError):
    """A write refused at write time. Nothing was stored. Subclass or pass a
    requirement -- the generic form is not allowed to be anonymous."""

    requirement = "E-1"


class NotFound(StateError):
    requirement = "E-1"


class Immutable(StateError):
    requirement = "A-6"


class BadFrame(StateError):
    requirement = "A-3"


class BadId(StateError):
    requirement = "A-7"


class BadTombstone(StateError):
    """A tombstone whose id does not match its own framing (A-8)."""

    requirement = "A-8"


class BadTimestamp(StateError):
    requirement = "C-3"


class Unauthorized(StateError):
    """The principal lacks r, s or x on this diary."""

    requirement = "D-3"


class NoSyncPath(StateError):
    """A diary declared `never` was offered as a sync endpoint."""

    requirement = "D-2"


class NotPublication(StateError):
    """Content would cross to a wider sync policy without being named a
    publication."""

    requirement = "V-4"


class Forked(StateError):
    requirement = "R-5"


class IncompleteMerge(StateError):
    requirement = "M-3"


class NotAStore(StateError):
    requirement = "E-4"


class Gone(StateError):
    """The referent was deleted or access revoked at the source. Distinct from
    Away, which is a state and not an error (X-2, X-3)."""

    requirement = "X-2"


class Unrebuildable(StateError):
    """An eviction that cannot be undone because the source is gone (X-5)."""

    requirement = "X-5"
