"""N -- the root of authority.

The gap every prior review converged on. v2 said grants confer rights and never
said who may issue one, so the only implementation passing the whole suite was
"a relay applies every tombstone it receives" (FINDINGS.md B-4, T-1c).

The anchor is the CONTAINER, not the content: a row can claim anything, but a row
cannot let you open a database.
"""

from refcore import diary, merger, reader, withdrawal, writer
from refcore.errors import Refused, Unauthorized
from refcore.model import Grant, Reason, R, S, X
from tests.support import (AGENT, ALICE, BOB, StateTest, a_store, an_assertion)

ROOT = "r" * 64


def proof_for(recipient):
    """N-3. Stands in for a MAC over the recipient's public key under the
    database key -- verifiable by ANY key-holder, not only the recipient, who is
    the one party nobody else can check for."""
    return b"mac:" + recipient.encode()


class TheAnchor(StateTest):
    """N-3. Read from the key-wrap table, which is structure rather than merged
    content."""

    def test_N3_a_root_claim_with_a_valid_proof_is_a_root(self):
        store = a_store(principal=ALICE)
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        self.assertIn(ROOT, diary.roots(store))

    def test_N3_a_root_claim_with_a_bad_proof_is_not(self):
        """THE POINT, and it is cryptographic rather than structural: a peer
        cannot promote itself by writing a row, because it cannot produce the
        proof without the database key."""
        store = a_store(principal=ALICE)
        diary.add_root(store, BOB, wrapped=b"sealed", proof=b"forged")
        self.assertNotIn(BOB, diary.roots(store))

    def test_N3_a_merged_self_issued_root_claim_confers_nothing(self):
        """The self-promotion vector across a merge."""
        mine = a_store(principal=ALICE)
        hostile = a_store(principal=BOB)
        diary.add_root(hostile, BOB, wrapped=b"sealed", proof=b"forged")
        merger.merge(mine, hostile)
        self.assertNotIn(BOB, diary.roots(mine))

    def test_N3a_the_root_set_travels(self):
        """The reason a root claim is an assertion and not container metadata: a
        newly enrolled device learns who the roots are by merging, rather than
        being told out of band."""
        laptop = a_store(principal=ALICE)
        phone = a_store(principal=ALICE)
        diary.add_root(laptop, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        merger.merge(phone, laptop)
        self.assertIn(ROOT, diary.roots(phone))

    def test_N9_anyone_holding_the_key_can_add_a_root(self):
        """Stated, not defended: root authority is exactly as strong as
        database-key custody and no stronger -- which is the same bound the
        verification has, deliberately."""
        store = a_store(principal=BOB)
        diary.add_root(store, BOB, wrapped=b"sealed", proof=proof_for(BOB))
        self.assertIn(BOB, diary.roots(store))


class AuthoritativeIdentity(StateTest):
    """N-1, N-2."""

    def test_N2_an_identity_signed_by_a_root_is_authoritative(self):
        store = a_store(principal=ROOT)
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        diary.identity_put(store, "alice", ALICE)
        self.assertTrue(diary.authoritative(store, ALICE))

    def test_N2_an_identity_signed_by_a_non_root_is_not(self):
        """Control. A checker that says yes to everything passes the test
        above."""
        store = a_store(principal=BOB)
        diary.identity_put(store, "alice", ALICE)
        self.assertFalse(diary.authoritative(store, ALICE))

    def test_N2_an_unsigned_identity_confers_nothing_but_is_kept(self):
        """Stored and readable — refusing it would lose the evidence that the
        claim was made."""
        store = a_store(principal=ALICE, signer=None)
        ident = diary.identity_put(store, "alice", ALICE)
        self.assertFalse(diary.authoritative(store, ALICE))
        self.assertIsNotNone(reader.get(store, ident))


class IdentityAssertions(StateTest):
    """N-1. An assertion, not a table -- so it merges, is searchable, obeys the
    diary's rights, and is revoked by supersession."""

    def test_N1_an_identity_carries_a_name_and_a_public_key(self):
        store = a_store(principal=ALICE)
        ident = diary.identity_put(store, "alice", ALICE)
        row = reader.get(store, ident)
        self.assertEqual(row.akey, ALICE)

    def test_N1_identities_merge_like_everything_else(self):
        """No separate distribution channel: it travels by the same union."""
        mine, theirs = a_store(principal=ALICE), a_store(principal=ALICE)
        ident = diary.identity_put(theirs, "alice", ALICE)
        merger.merge(mine, theirs)
        self.assertIsNotNone(reader.get(mine, ident))

    def test_N1_an_identity_obeys_the_diary_rights(self):
        """A table would have had no permissions at all. This is what being an
        assertion buys."""
        owner = a_store(principal=ALICE)
        ident = diary.identity_put(owner, "alice", ALICE)
        blind = a_store(principal=BOB, grants=())
        merger.merge(blind, owner)
        with self.assertRaises(Unauthorized):
            reader.get(blind, ident)

    def test_N1_revocation_is_supersession_not_erasure(self):
        """W-2. "Unsuperseded" is a pure function of the set, so revoking is
        order-independent; erasing an authority-bearing assertion would make the
        sweep depend on sweep order, which is FINDINGS.md C-2's defect."""
        store = a_store(principal=ROOT)
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        ident = diary.identity_put(store, "alice", ALICE)
        diary.revoke_identity(store, ALICE)
        self.assertFalse(diary.authoritative(store, ALICE))
        self.assertIsNotNone(reader.get(store, ident))

    def test_W2_an_identity_cannot_be_erased(self):
        store = a_store(principal=ROOT)
        ident = diary.identity_put(store, "alice", ALICE)
        with self.assertRaises(Refused):
            withdrawal.erase(store, ident, Reason.POLICY)


class Delegation(StateTest):
    """N-4, N-5. Delegation narrows; it never escalates."""

    def test_N4_an_issuer_cannot_grant_a_right_it_does_not_hold(self):
        store = a_store(principal=BOB,
                        grants=(Grant(principal=BOB, diary="personal", rights=R | S),))
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        self.assertFalse(diary.may_grant(store, BOB, "personal", X))

    def test_N4_an_issuer_may_grant_a_right_it_holds(self):
        """Control."""
        store = a_store(principal=BOB,
                        grants=(Grant(principal=BOB, diary="personal", rights=R | S),))
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        diary.identity_put(store, "bob", BOB)
        self.assertTrue(diary.may_grant(store, BOB, "personal", R))

    def test_N5_a_root_holds_every_right(self):
        """What terminates the delegation chain rather than leaving it
        circular."""
        store = a_store(principal=ROOT)
        diary.add_root(store, ROOT, wrapped=b"sealed", proof=proof_for(ROOT))
        self.assertTrue(diary.may_grant(store, ROOT, "personal", R | S | X))

    def test_N6_an_unrootable_grant_is_stored_and_inert(self):
        """The self-promotion vector, closed: merging a peer's self-issued grant
        delivers it, and it confers nothing."""
        mine = a_store(principal=ALICE)
        hostile = a_store(principal=BOB)
        diary.grant(hostile, BOB, "personal", R | S | X)
        merger.merge(mine, hostile)
        self.assertEqual(diary.rights(mine, BOB, "personal"), 0)
        self.assertEqual(len(diary.inert_grants(mine)), 1)


class KeyDerivation(StateTest):
    """N-7, N-8. The two must never share a code path."""

    def test_N7_the_database_key_is_used_raw(self):
        """A uniformly random 256-bit key is already at full entropy, so
        stretching buys nothing against brute force — and the cost is not
        academic: 5.99 ms to open raw versus 345.93 ms with a passphrase."""
        store = a_store(principal=ALICE)
        diary.add_root(store, ALICE, wrapped=b"sealed", proof=proof_for(ALICE))
        self.assertEqual(diary.describe(store).get("kdf"), None)

    def test_N8_a_password_wrapped_secret_key_declares_a_slow_kdf(self):
        """Its input IS low-entropy, so the opposite rule applies. One `unlock`
        function treating both alike is the likeliest way to get this wrong."""
        store = a_store(principal=ALICE)
        ident = diary.identity_put(store, "alice", ALICE,
                                   secret_wrapped=b"ciphertext", kdf="argon2id")
        self.assertEqual(reader.get(store, ident).body, b"argon2id")


class RelayCannotJudge(StateTest):
    """S-9, S-9a. A relay and a peer are different roles, and only one can
    sweep."""

    def test_S9_a_relay_holds_no_key_and_applies_nothing(self):
        """It cannot judge a tombstone's authority because it cannot read the
        identity that signed it. Demanding that it sweep anyway was the third
        leg of the triangle that made the whole authority model unsatisfiable."""
        relay = a_store(principal=BOB, grants=())
        laptop = a_store(principal=ALICE)
        target = writer.put(laptop, an_assertion(author=ALICE))
        withdrawal.erase(laptop, target.id, Reason.SECRET)
        merger.merge(relay, laptop)
        self.assertEqual(withdrawal.sweep(relay).inert, 1)

    def test_S9a_a_relay_re_serves_withdrawn_content_and_says_so(self):
        """Inherent, not a defect to patch: a relay carrying an opaque blob
        cannot know what is inside it. The mitigation is the container -- what a
        relay carries is superseded wholesale by a later snapshot -- so the
        honest requirement is that it must not CLAIM to have swept."""
        relay = a_store(principal=BOB, grants=())
        laptop = a_store(principal=ALICE)
        target = writer.put(laptop, an_assertion(author=ALICE))
        withdrawal.erase(laptop, target.id, Reason.SECRET)
        merger.merge(relay, laptop)
        self.assertNotEqual(withdrawal.residue(relay, target.id), ())

    def test_S9_a_peer_holding_the_key_does_sweep(self):
        """Control: the sweep must still work somewhere, or withdrawal is
        decorative."""
        laptop = a_store(principal=ALICE)
        phone = a_store(principal=ALICE)
        target = writer.put(laptop, an_assertion(author=ALICE))
        withdrawal.erase(laptop, target.id, Reason.SECRET)
        merger.merge(phone, laptop)
        self.assertEqual(withdrawal.residue(phone, target.id), ())
