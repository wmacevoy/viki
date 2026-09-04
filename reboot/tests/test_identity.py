"""A -- assertions and identity."""

import unicodedata

from refcore import ids, writer
from refcore.errors import BadTimestamp, Immutable
from refcore.model import Reference, Tier, Tombstone, Reason
from tests.support import pack_rank, ALICE, BOB, StateTest, a_ref, a_store, an_assertion


class ContentAddressing(StateTest):

    def test_A1_same_content_same_id(self):
        self.assertEqual(ids.compute_id(an_assertion()),
                         ids.compute_id(an_assertion()))

    def test_A1_different_body_different_id(self):
        """Control: without it the test above passes for a constant."""
        self.assertNotEqual(ids.compute_id(an_assertion(body=b"hello")),
                            ids.compute_id(an_assertion(body=b"goodbye")))


class FramedPosition(StateTest):
    """A-2. One test per framed field: a frame omitting one fails only that."""

    def test_A2_author_changes_the_id(self):
        self.assertNotEqual(ids.compute_id(an_assertion(author=ALICE)),
                            ids.compute_id(an_assertion(author=BOB)))

    def test_A2_akey_changes_the_id(self):
        self.assertNotEqual(ids.compute_id(an_assertion(akey="k1")),
                            ids.compute_id(an_assertion(akey="k2")))

    def test_A2_timestamp_changes_the_id(self):
        self.assertNotEqual(ids.compute_id(an_assertion(ts="2026-09-04T12:00:00Z")),
                            ids.compute_id(an_assertion(ts="2026-09-04T12:00:01Z")))

    def test_A2_rank_changes_the_id(self):
        """THE C-2 FIX. Left outside the frame, two hosts with different rank
        functions produce identical ids carrying different ranks -- same set,
        different winner, and no integrity check fires because the ids agree."""
        self.assertNotEqual(ids.compute_id(an_assertion(rank=pack_rank(counter=1))),
                            ids.compute_id(an_assertion(rank=pack_rank(counter=2))))

    def test_A2_reference_changes_the_id(self):
        """X-1. The reference is framed; the content is not."""
        self.assertNotEqual(ids.compute_id(an_assertion(reference=a_ref(ident="m1"))),
                            ids.compute_id(an_assertion(reference=a_ref(ident="m2"))))

    def test_A2_supersedes_changes_the_id(self):
        self.assertNotEqual(ids.compute_id(an_assertion(supersedes=None)),
                            ids.compute_id(an_assertion(supersedes="f" * 64)))


class FrameBoundaries(StateTest):
    """A-3."""

    def test_A3_a_field_cannot_forge_a_boundary(self):
        """The concrete failure of separator framing: one field containing the
        separator frames identically to two fields split at it."""
        self.assertNotEqual(ids.frame(ids.TAG_ASSERTION, b"a\x1fb"),
                            ids.frame(ids.TAG_ASSERTION, b"a", b"b"))

    def test_A3_shifting_a_boundary_changes_the_frame(self):
        self.assertNotEqual(ids.frame(ids.TAG_ASSERTION, b"a", b"bc"),
                            ids.frame(ids.TAG_ASSERTION, b"ab", b"c"))

    def test_A3_an_empty_field_is_a_field(self):
        """Collapsing it is how a framing loses a column and two different
        tuples start hashing alike."""
        self.assertNotEqual(ids.frame(ids.TAG_ASSERTION, b"", b"a"),
                            ids.frame(ids.TAG_ASSERTION, b"a"))

    def test_A3_framing_is_not_concatenation(self):
        """v1's version of this test passed for a plain join. This one cannot:
        the frame must be longer than its payload, because the lengths are in
        it."""
        self.assertGreater(len(ids.frame(ids.TAG_ASSERTION, b"ab", b"cd")),
                           len(ids.TAG_ASSERTION) + 4)


class DomainSeparation(StateTest):
    """A-8. The F-2 fix.

    v1 gave tombstones their own table and no framing rule, so A-7 had no
    definition of `content` for the one row shape that destroys things -- and
    any signature the victim had ever produced authorized an erasure.
    """

    def test_A8_the_same_bytes_frame_differently_under_different_tags(self):
        self.assertNotEqual(ids.frame(ids.TAG_ASSERTION, b"x"),
                            ids.frame(ids.TAG_TOMBSTONE, b"x"))

    def test_A8_a_tombstone_has_its_own_id(self):
        t = Tombstone(author=ALICE, target="f" * 64, target_kind="assertion",
                      tier=Tier.ERASE, reason=Reason.SECRET,
                      ts="2026-09-04T12:00:00Z")
        self.assertEqual(len(ids.compute_tombstone_id(t)), 64)

    def test_A8_a_tombstone_id_is_not_its_target(self):
        """The forgery this closes: mint a tombstone whose id equals an id the
        victim already signed, and their genuine signature authorizes it."""
        target = "f" * 64
        t = Tombstone(author=ALICE, target=target, target_kind="assertion",
                      tier=Tier.ERASE, reason=Reason.SECRET,
                      ts="2026-09-04T12:00:00Z")
        self.assertNotEqual(ids.compute_tombstone_id(t), target)

    def test_A8_changing_the_tier_changes_the_tombstone_id(self):
        def tomb(tier):
            return Tombstone(author=ALICE, target="f" * 64,
                             target_kind="assertion", tier=tier,
                             reason=Reason.SECRET, ts="2026-09-04T12:00:00Z")
        self.assertNotEqual(ids.compute_tombstone_id(tomb(Tier.WITHDRAW)),
                            ids.compute_tombstone_id(tomb(Tier.ERASE)))


class Normalization(StateTest):
    """A-4."""

    def test_A4_nfc_and_nfd_produce_one_id(self):
        nfc = unicodedata.normalize("NFC", "café")
        nfd = unicodedata.normalize("NFD", "café")
        self.assertNotEqual(nfc, nfd)          # the premise, not the claim
        self.assertEqual(ids.compute_id(an_assertion(body=nfc.encode())),
                         ids.compute_id(an_assertion(body=nfd.encode())))

    def test_A4_normalization_keeps_different_strings_different(self):
        """Control: normalization must not be so aggressive it loses meaning."""
        self.assertNotEqual(ids.normalize("cafe"), ids.normalize("café"))


class WriteOnce(StateTest):

    def test_A5_a_second_put_is_reported_not_silent(self):
        store = a_store()
        first = writer.put(store, an_assertion())
        second = writer.put(store, an_assertion())
        self.assertTrue(first.stored)
        self.assertFalse(second.stored)
        self.assertEqual(first.id, second.id)

    def test_A6_the_same_id_with_other_bytes_is_refused(self):
        """A-6. There is no update path, so the only way to appear to change a
        stored assertion is to present its id with other content. Refused, not
        ignored -- ignoring keeps the old bytes while the caller believes
        otherwise."""
        store = a_store()
        original = an_assertion()
        writer.put(store, original)
        forged = an_assertion(body=b"edited")
        object.__setattr__(forged, "id", ids.compute_id(original))
        with self.assertRaises(Immutable):
            writer.put(store, forged)

    def test_A7_an_id_that_does_not_match_its_content_fails_verification(self):
        forged = an_assertion()
        object.__setattr__(forged, "id", "0" * 64)
        self.assertFalse(ids.verify_id(forged))

    def test_A7_a_correct_id_verifies(self):
        """Control: a verifier that rejects everything passes the test above."""
        a = an_assertion()
        object.__setattr__(a, "id", ids.compute_id(a))
        self.assertTrue(ids.verify_id(a))


class Timestamps(StateTest):

    def test_C3_a_malformed_timestamp_is_refused(self):
        with self.assertRaises(BadTimestamp):
            ids.check_timestamp("next Tuesday")

    def test_C3_a_valid_timestamp_is_accepted(self):
        """Control: a checker refusing everything passes the test above."""
        self.assertIsNone(ids.check_timestamp("2026-09-04T12:00:00Z"))
