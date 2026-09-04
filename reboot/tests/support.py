"""Test fixtures. These ARE implemented -- they are the mocks that flatten the
chart, not part of the skeleton.

Two rules learned the expensive way (FINDINGS.md C-1, C-7):

  1. THE STORE CONSTRUCTOR MUST NOT RAISE. v1's did, so 81% of the suite died in
     setup and never reached the function it named, while a verification
     counting exception types reported success.
  2. `None` IS A VALUE, NOT A SENTINEL. v1's `a_store(signer=None)` quietly
     supplied a signer, which made five tests vacuous and one PAIR of tests
     unsatisfiable by any implementation. `_UNSET` fixes it.
"""

import hashlib
import sqlite3
import unittest

from refcore import schema
from refcore.errors import Gone
from refcore.model import (Assertion, Grant, Reference, SyncPolicy, R, S, X)
from refcore.store import Store

ALICE = "a" * 64
BOB = "b" * 64
AGENT = "e" * 64
CUSTODIAN = "c" * 64

_UNSET = object()


class FakeClock:
    """C-1. Time is an input, so a test sets it rather than waiting for it."""

    def __init__(self, start="2026-09-04T12:00:00Z"):
        self._now = start

    def now(self):
        return self._now

    def set(self, ts):
        self._now = ts


class FakeSigner:
    """Deterministic and forgeable on purpose: these tests ask whether
    signatures are CONSULTED, not whether a primitive is strong."""

    def __init__(self, principal=ALICE, declines=False):
        self._principal = principal
        self.declines = declines

    def principal(self):
        return self._principal

    def sign(self, purpose, id_hex):
        if self.declines:
            return None
        return hashlib.sha256(
            (self._principal + "|" + purpose + "|" + id_hex).encode()).digest()


class FakeVerifier:
    def verify(self, pubkey, purpose, id_hex, sig):
        return sig == hashlib.sha256(
            (pubkey + "|" + purpose + "|" + id_hex).encode()).digest()


class FakeFetcher:
    """X-2's three-way contract: bytes, None for AWAY, Gone for gone."""

    def __init__(self, have=None, gone=(), versions=None):
        self.have = dict(have or {})
        self.gone = set(gone)
        self.versions = dict(versions or {})

    def _k(self, ref):
        return (ref.system, ref.ident)

    def fetch(self, ref):
        if self._k(ref) in self.gone:
            raise Gone("deleted at source")
        return self.have.get(self._k(ref))

    def version(self, ref):
        return self.versions.get(self._k(ref))


class FakeCustodian:
    def __init__(self, holder=CUSTODIAN):
        self.holder = holder

    def wrap(self, content_key, recipient):
        return hashlib.sha256(recipient.encode()).digest() + content_key

    def unwrap(self, wrapped):
        head = hashlib.sha256(self.holder.encode()).digest()
        return wrapped[len(head):] if wrapped.startswith(head) else None


class NoteKind:
    """R-6, C-4. A registered KindSpec -- wired to the Store, unlike v1's,
    which was referenced by zero modules and zero tests."""

    name = "note"

    def akey(self, body, reference):
        return reference.ident if reference else "k"

    def rank(self, ts, body):
        return rank_of(ts)

    def canon(self, body):
        return body


class CountedKind(NoteKind):
    """C-4. A kind that ranks by a logical counter, so skew moves timing rather
    than outcome. Body is b"seq=N"."""

    name = "counted"

    def akey(self, body, reference):
        return "c"

    def rank(self, ts, body):
        return pack_rank(counter=int(body.split(b"=")[1]))


def pack_rank(ms=0, counter=0, tie=0):
    """A-2a, A-2b. The stated encoding: big-endian ms since epoch (6 bytes),
    a logical counter (8), a kind-defined tiebreak (2). Exactly 16."""
    return (ms.to_bytes(6, "big") + counter.to_bytes(8, "big")
            + tie.to_bytes(2, "big"))


def rank_of(ts):
    """A rank derived from an ISO timestamp, for fixtures that only care that
    later sorts after earlier.

    A malformed ts yields a zero rank rather than raising: the fixture must not
    decide what C-3 refuses. Getting this wrong made a timestamp test fail with
    a ValueError from the fixture instead of a BadTimestamp from the code, which
    check.py caught -- the failure-for-the-wrong-reason case it exists to find.
    """
    digits = "".join(c for c in ts if c.isdigit())[:14]
    return pack_rank(ms=int(digits) % (1 << 48) if digits else 0)


def a_ref(system="gmail", ident="msg-1", version="v1"):
    return Reference(system=system, ident=ident, version=version)


def an_assertion(*, author=ALICE, kind="note", akey="k",
                 ts="2026-09-04T12:00:00Z", rank=None, body=b"hello",
                 reference=None, supersedes=(), derived_from=()):
    return Assertion(author=author, kind=kind, akey=akey, ts=ts,
                     rank=rank if rank is not None else rank_of(ts), body=body,
                     reference=reference, supersedes=supersedes,
                     derived_from=derived_from)


def a_store(principal=ALICE, *, diary="personal", clock=None, signer=_UNSET,
            verifier=None, custodian=None, fetcher=None, kinds=None,
            grants=_UNSET, policy=SyncPolicy.DOMAIN, endpoint=None):
    """The constructor is inert, so a test fails inside the function it names."""
    if signer is _UNSET:
        signer = FakeSigner(principal)
    if grants is _UNSET:
        grants = (Grant(principal=principal, diary=diary, rights=R | S | X),)
    return Store(sqlite3.connect(":memory:"), diary=diary, principal=principal,
                 clock=clock or FakeClock(), signer=signer,
                 verifier=verifier or FakeVerifier(), custodian=custodian,
                 fetcher=fetcher, kinds=kinds or (NoteKind(), CountedKind()),
                 grants=grants, policy=policy, endpoint=endpoint)


def plant(store, assertion, forged_id):
    """Write a row a trusted `put` would refuse, straight into the table.

    THE FIX FOR FINDINGS T-1a. Two tests were mutually unsatisfiable because
    both used `writer.put`: one required it to REFUSE a mismatched id (A-7) and
    the other required it to ACCEPT one, so that merge had a row to quarantine
    (M-7). Only one of those can be the write path's behaviour.

    A merge source is untrusted BY DEFINITION, so a corrupt row should never
    have to be reachable through the trusted door. Planting it directly is what
    a corrupt peer, a bad encoder or a truncated transfer actually produces --
    and it lets A-7 refuse at the write path and quarantine at the merge path
    without contradiction.
    """
    schema.install(store.conn)          # planting needs the tables to exist
    store.conn.execute(
        "INSERT INTO assertion(id, author, kind, akey, arank, ts, body)"
        " VALUES (?,?,?,?,?,?,?)",
        (forged_id, assertion.author, assertion.kind, assertion.akey,
         assertion.rank, assertion.ts, assertion.body))


class StateTest(unittest.TestCase):
    pass
