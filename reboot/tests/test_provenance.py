"""I -- identity and provenance. "Who said this, and can I check it later
without asking them again?"

Content addressing gives INTEGRITY. It does not give AUTHORITY. These are the
tests for the missing half.
"""

from refcore import diary, merger, reader, writer
from refcore.errors import Unauthorized
from refcore.model import Grant, SigState, R, S, X
from tests.support import (AGENT, ALICE, BOB, CUSTODIAN, FakeSigner, StateTest,
                           a_store, an_assertion)


class AuthorIsIdentity(StateTest):
    """I-1."""

    def test_I1_author_cannot_be_rewritten_without_changing_the_assertion(self):
        store = a_store(principal=ALICE)
        mine = writer.put(store, an_assertion(author=ALICE, body=b"I said this"))
        theft = writer.put(store, an_assertion(author=BOB, body=b"I said this"))
        self.assertNotEqual(mine.id, theft.id)

    def test_I1_two_authors_saying_the_same_thing_are_two_facts(self):
        """The accepted cost of framing the author: identical content from two
        authors no longer deduplicates. For notes that is correct -- a model
        merging them has lost the more interesting half."""
        store = a_store()
        a = writer.put(store, an_assertion(author=ALICE, body=b"it rained"))
        b = writer.put(store, an_assertion(author=BOB, body=b"it rained"))
        self.assertNotEqual(a.id, b.id)
        self.assertEqual(len(reader.forks(store, "k").arms), 2)


class PerSignerState(StateTest):
    """I-2. The F-8 fix.

    v1 returned ONE verdict per assertion, so any peer could push a junk
    signature and move any assertion to BAD -- discrediting it network-wide, and
    letting any author plausibly deny their own statements via a sockpuppet.
    """

    def test_I2_a_signed_assertion_verifies_for_its_signer(self):
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        self.assertEqual(reader.sig_states(store, put.id).get(ALICE),
                         SigState.VERIFIED)

    def test_I2_a_junk_signature_does_not_discredit_a_good_one(self):
        """THE ONE THAT MATTERS. Both states coexist, keyed by signer."""
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        forger = a_store(principal=BOB, signer=FakeSigner(BOB))
        writer.countersign(forger, put.id)
        merger.merge(store, forger)
        states = reader.sig_states(store, put.id)
        self.assertEqual(states.get(ALICE), SigState.VERIFIED)
        self.assertIn(BOB, states)

    def test_I2_a_declined_signature_stores_the_assertion_unsigned(self):
        """A locked keychain or a cancelled prompt is not an error."""
        store = a_store(principal=ALICE, signer=FakeSigner(ALICE, declines=True))
        put = writer.put(store, an_assertion(author=ALICE))
        self.assertEqual(reader.sig_states(store, put.id), {})

    def test_I2_an_unheld_signer_reports_unknown(self):
        stranger = "f" * 64
        store = a_store(principal=ALICE)
        other = a_store(principal=stranger, signer=FakeSigner(stranger))
        put = writer.put(other, an_assertion(author=stranger))
        merger.merge(store, other)
        self.assertEqual(reader.sig_states(store, put.id).get(stranger),
                         SigState.UNKNOWN)


class VerificationNeedsNoSecret(StateTest):
    """I-3."""

    def test_I3_a_store_with_no_signer_can_still_verify(self):
        signed = a_store(principal=ALICE)
        put = writer.put(signed, an_assertion(author=ALICE))
        reader_only = a_store(principal=BOB, signer=None,
                              grants=(Grant(principal=BOB, diary="personal",
                                            rights=R),))
        merger.merge(reader_only, signed)
        self.assertEqual(reader.sig_states(reader_only, put.id).get(ALICE),
                         SigState.VERIFIED)


class DomainSeparation(StateTest):
    """A-8 at the authority layer. The F-2 fix.

    v1 signed a bare id, so a signature over an assertion authorized a tombstone
    carrying the same id -- W-5 defeated with no stolen key.
    """

    def test_I5_an_assertion_signature_does_not_authorize_a_tombstone(self):
        from refcore import withdrawal
        from refcore.model import Reason
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        hostile = a_store(principal=BOB,
                          grants=(Grant(principal=BOB, diary="personal",
                                        rights=R | S | X),))
        merger.merge(hostile, store)
        # A tombstone minted to carry the victim's already-signed id.
        forged = withdrawal.erase(hostile, put.id, Reason.POLICY)
        merger.merge(store, hostile)
        self.assertIsNotNone(reader.get(store, put.id))


class KeyLifecycle(StateTest):
    """I-6. The F-9 gap.

    v1 bound authorship to a raw key forever, so rotating left your entire
    history authored by a key you no longer control -- and a stolen key was
    retroactively and permanently authoritative.
    """

    def test_I6_a_grant_can_expire(self):
        store = a_store(principal=ALICE)
        diary.grant(store, AGENT, "personal", S, expires="2026-09-01T00:00:00Z")
        self.assertEqual(diary.rights(store, AGENT, "personal"), 0)

    def test_I6_an_unexpired_grant_still_holds(self):
        """Control."""
        store = a_store(principal=ALICE)
        diary.grant(store, AGENT, "personal", S, expires="2027-01-01T00:00:00Z")
        self.assertEqual(diary.rights(store, AGENT, "personal"), S)


class FourIdentities(StateTest):
    """I-5. Author, signer, custodian, principal -- never conflated."""

    def test_I5_a_custodian_is_not_automatically_a_reader(self):
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        cust = a_store(principal=CUSTODIAN, grants=())
        merger.merge(cust, store)
        with self.assertRaises(Unauthorized):
            reader.get(cust, put.id)

    def test_I5_a_countersigner_does_not_become_the_author(self):
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        other = a_store(principal=BOB,
                        grants=(Grant(principal=BOB, diary="personal",
                                      rights=R | S),))
        merger.merge(other, store)
        writer.countersign(other, put.id)
        merger.merge(store, other)
        self.assertEqual(reader.get(store, put.id).author, ALICE)
