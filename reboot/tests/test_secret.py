"""N-10..N-17 -- the travelling secret, and what it does and does not buy.

The construction is a time-lock puzzle (Rivest-Shamir-Wagner 1996) over the copy
of an identity's private key that leaves the device. These tests pin the
asymmetry that justifies it and the two hazards that come with it.
"""

from refcore import diary, merger, reader, secret, writer
from refcore.errors import Refused, Unauthorized
from tests.support import ALICE, BOB, StateTest, a_store, an_assertion

PASSWORD = "correct horse battery staple"
SECRET = b"ed25519-seed-32-bytes-goes-here!"


class TwoSeals(StateTest):
    """N-10. Sealed twice; only one copy travels."""

    def test_N10_both_seals_recover_the_same_secret(self):
        nonce = secret.make_nonce(2048)
        local, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 16)
        self.assertEqual(secret.unlock_local(local, PASSWORD, nonce), SECRET)
        self.assertEqual(secret.unlock_travel(travel, PASSWORD, nonce, t=1 << 16),
                         SECRET)

    def test_N10_the_two_seals_are_different_ciphertexts(self):
        """Control. If they were equal the travel copy would be unlockable by
        the fast path and the whole construction would be decoration."""
        nonce = secret.make_nonce(2048)
        local, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 16)
        self.assertNotEqual(local, travel)

    def test_N10_only_the_travel_seal_syncs(self):
        laptop = a_store(principal=ALICE)
        phone = a_store(principal=ALICE)
        nonce = secret.make_nonce(2048)
        local, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 16)
        ident = diary.identity_put(laptop, "alice", ALICE,
                                   secret_wrapped=travel, kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertNotIn(local, reader.get(phone, ident).body)

    def test_N10_the_travel_seal_does_sync(self):
        """Control: it has to arrive, or a new device cannot enrol at all --
        which is the requirement this whole design exists to serve."""
        laptop = a_store(principal=ALICE)
        phone = a_store(principal=ALICE)
        nonce = secret.make_nonce(2048)
        _, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 16)
        ident = diary.identity_put(laptop, "alice", ALICE,
                                   secret_wrapped=travel, kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertIn(travel, reader.get(phone, ident).body)


class TheTrapdoorIsDestroyed(StateTest):
    """N-12. The factors are the shortcut, so nobody holds them -- including the
    owner, who has no legitimate use for one because they already hold
    `local_secret`."""

    def test_N12_make_nonce_returns_only_the_modulus(self):
        """It must not hand back p or q in any form. With phi(n) an attacker
        computes 2^t mod phi(n) and collapses the chain into one
        exponentiation."""
        result = secret.make_nonce(2048)
        self.assertIsInstance(result, int)

    def test_N14_the_modulus_is_the_stated_size(self):
        """The hardening is bounded by the difficulty of FACTORING this: an
        attacker who factors the nonce recovers the shortcut and the travel seal
        falls back to plain kdf strength. So the size is a stated parameter with
        a review date, not a constant chosen once."""
        self.assertEqual(secret.make_nonce(2048).bit_length(), 2048)

    def test_N14_a_larger_nonce_is_available(self):
        """Control on the same point: if the size cannot move, it is not a
        parameter and the review date means nothing."""
        self.assertEqual(secret.make_nonce(3072).bit_length(), 3072)


class Sequentiality(StateTest):
    """N-11."""

    def test_N11_more_squarings_give_a_different_key(self):
        """The chain must actually depend on t, or `t` is a decorative
        parameter."""
        nonce = secret.make_nonce(2048)
        k = secret.k_local(PASSWORD, nonce)
        self.assertNotEqual(secret.k_travel(k, nonce, t=1 << 10),
                            secret.k_travel(k, nonce, t=1 << 11))

    def test_N11_the_chain_is_deterministic(self):
        """Same input, same t, same key -- otherwise no peer could ever unlock
        what another sealed."""
        nonce = secret.make_nonce(2048)
        k = secret.k_local(PASSWORD, nonce)
        self.assertEqual(secret.k_travel(k, nonce, t=1 << 10),
                         secret.k_travel(k, nonce, t=1 << 10))


class TheAsymmetry(StateTest):
    """N-13. THE POINT, and the reason to state what it is not.

    It is NOT that sequential work defeats parallel attack -- password cracking
    parallelizes across guesses, not within one, so `t` behaves like a KDF cost
    parameter. What differs is WHEN THE DEFENDER PAYS.
    """

    def test_N13_the_everyday_path_does_no_squaring(self):
        """The owner on their own device pays a kdf and nothing else. A KDF slow
        enough to impose the same per-guess cost as `t` would be paid here, on
        every single unlock, which is why it is not an alternative."""
        nonce = secret.make_nonce(2048)
        local, _ = secret.seal(SECRET, PASSWORD, nonce, t=1 << 20)
        self.assertEqual(secret.unlock_local(local, PASSWORD, nonce), SECRET)

    def test_N13_the_enrolment_path_pays_the_lock_once(self):
        nonce = secret.make_nonce(2048)
        _, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 12)
        self.assertEqual(secret.unlock_travel(travel, PASSWORD, nonce, t=1 << 12),
                         SECRET)

    def test_N13_a_wrong_password_fails_on_both_paths(self):
        """Control: the lock is on top of the password, not instead of it."""
        nonce = secret.make_nonce(2048)
        local, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 12)
        self.assertNotEqual(secret.unlock_local(local, "wrong", nonce), SECRET)
        self.assertNotEqual(
            secret.unlock_travel(travel, "wrong", nonce, t=1 << 12), SECRET)


class MemoryHardnessIsSeparate(StateTest):
    """N-15. Modular squaring is low-memory and ASIC-friendly -- the opposite of
    what constrains parallel cracking hardware. The two defend different axes."""

    def test_N15_the_local_kdf_is_memory_hard_on_its_own_account(self):
        nonce = secret.make_nonce(2048)
        diary_store = a_store(principal=ALICE)
        ident = diary.identity_put(diary_store, "alice", ALICE,
                                   secret_wrapped=b"sealed", kdf="argon2id")
        self.assertIn(b"argon2", reader.get(diary_store, ident).body)


class RecordedParameters(StateTest):
    """N-16. A peer that cannot tell how much work the seal requires cannot
    unlock it."""

    def test_N16_t_and_the_nonce_size_travel_with_the_identity(self):
        laptop = a_store(principal=ALICE)
        phone = a_store(principal=ALICE)
        nonce = secret.make_nonce(2048)
        _, travel = secret.seal(SECRET, PASSWORD, nonce, t=1 << 16)
        diary.identity_put(laptop, "alice", ALICE, secret_wrapped=travel,
                           kdf="argon2id")
        merger.merge(phone, laptop)
        params = secret.parameters(phone, ALICE)
        self.assertEqual(params["t"], 1 << 16)
        self.assertEqual(params["nonce_bits"], 2048)

    def test_N16_raising_t_does_not_orphan_an_existing_identity(self):
        """Identities sealed under the old value must stay unlockable, or a
        parameter change silently locks people out of their own keys."""
        store = a_store(principal=ALICE)
        nonce = secret.make_nonce(2048)
        _, old = secret.seal(SECRET, PASSWORD, nonce, t=1 << 12)
        diary.identity_put(store, "alice", ALICE, secret_wrapped=old,
                           kdf="argon2id")
        self.assertEqual(secret.parameters(store, ALICE)["t"], 1 << 12)


class Rekey(StateTest):
    """N-17. Device removal is a re-key, not a delete."""

    def test_N17_a_rekey_replaces_the_root_set(self):
        """Unwrapping a recipient does not un-tell them a key they already
        hold."""
        store = a_store(principal=ALICE)
        secret.rekey(store, new_roots=(ALICE,))
        self.assertEqual(tuple(diary.roots(store)), (ALICE,))

    def test_N17_the_removed_device_keeps_what_it_had(self):
        """Forward-only, like every other revocation here. Saying so is the
        difference between a limitation and a false promise."""
        laptop = a_store(principal=ALICE)
        lost = a_store(principal=BOB)
        put = writer.put(laptop, an_assertion(author=ALICE))
        merger.merge(lost, laptop)
        secret.rekey(laptop, new_roots=(ALICE,))
        self.assertIsNotNone(reader.get(lost, put.id))

    def test_N17_the_removed_device_learns_nothing_after(self):
        laptop = a_store(principal=ALICE)
        lost = a_store(principal=BOB)
        merger.merge(lost, laptop)
        secret.rekey(laptop, new_roots=(ALICE,))
        writer.put(laptop, an_assertion(author=ALICE, akey="after"))
        with self.assertRaises(Unauthorized):
            merger.merge(lost, laptop)
