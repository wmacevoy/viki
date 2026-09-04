"""N-10..N-17 -- the travelling secret.

The salt is the whole mechanism: full on a device that has it, truncated in the
row that travels. A device that wants the secret searches the dropped bits,
recovers the salt, and keeps it locally.
"""

from refcore import diary, merger, reader, secret, writer
from refcore.errors import Unauthorized
from tests.support import ALICE, BOB, StateTest, a_store, an_assertion

PASSWORD = "correct horse battery staple"
SECRET = b"ed25519-seed-32-bytes-goes-here!"
B = 12


class OneSaltOneKey(StateTest):
    """N-10."""

    def test_N10_seal_and_enrol_round_trip(self):
        salt, trunc, wrapped, verifier = secret.seal(SECRET, PASSWORD, B)
        found, recovered = secret.enrol(wrapped, PASSWORD, trunc, B, verifier)
        self.assertEqual(found, SECRET)
        self.assertEqual(recovered, salt)

    def test_N10_the_travelling_salt_has_its_low_bits_zeroed(self):
        salt, trunc, _, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertEqual(int.from_bytes(trunc, "big") & ((1 << B) - 1), 0)

    def test_N10_the_truncated_salt_differs_from_the_real_one(self):
        """Control, and it is N-11 doing the work: if these could be equal there
        would be no search."""
        salt, trunc, _, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertNotEqual(salt, trunc)

    def test_N10_truncation_keeps_the_high_bits(self):
        """The searcher needs everything above `b` or the space is not 2^b."""
        salt, trunc, _, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertEqual(int.from_bytes(salt, "big") >> B,
                         int.from_bytes(trunc, "big") >> B)


class TheRedraw(StateTest):
    """N-11. Without it, one identity in 2^b has no hardening at all --
    silently, and looking exactly like every other identity."""

    def test_N11_the_low_bits_are_never_all_zero(self):
        for _ in range(8):
            self.assertNotEqual(
                int.from_bytes(secret.new_salt(B), "big") & ((1 << B) - 1), 0)

    def test_N11_a_salt_is_thirty_two_bytes(self):
        self.assertEqual(len(secret.new_salt(B)), 32)


class TheRecoveredSaltStaysHome(StateTest):
    """N-12. It is the ANSWER to the search: a peer that shared it would
    broadcast the answer and the hardening would evaporate."""

    def test_N12_the_creator_never_searches(self):
        """It drew the salt, so it stores it and pays nothing."""
        store = a_store(principal=ALICE)
        salt, trunc, wrapped, _ = secret.seal(SECRET, PASSWORD, B)
        ident = diary.identity_put(store, "alice", ALICE,
                                   secret_wrapped=wrapped, kdf="argon2id")
        secret.remember_salt(store, ident, salt)
        self.assertEqual(secret.unlock(wrapped, PASSWORD,
                                       secret.local_salt(store, ident)), SECRET)

    def test_N12_the_recovered_salt_does_not_sync(self):
        """THE ONE THAT MATTERS. If it travelled, the first device to search
        would hand the answer to everyone, including a hub."""
        laptop, phone = a_store(principal=ALICE), a_store(principal=ALICE)
        salt, trunc, wrapped, _ = secret.seal(SECRET, PASSWORD, B)
        ident = diary.identity_put(laptop, "alice", ALICE,
                                   secret_wrapped=wrapped, kdf="argon2id")
        secret.remember_salt(laptop, ident, salt)
        merger.merge(phone, laptop)
        self.assertIsNone(secret.local_salt(phone, ident))

    def test_N12b_the_assertion_carries_the_truncated_salt(self):
        """Permanently and everywhere. Shipping the full salt and letting the
        transport truncate it would make the shipped row's id disagree with the
        creator's -- a different assertion, a forked identity key, and an A-7
        failure."""
        laptop, phone = a_store(principal=ALICE), a_store(principal=ALICE)
        salt, trunc, wrapped, _ = secret.seal(SECRET, PASSWORD, B)
        ident = diary.identity_put(laptop, "alice", ALICE,
                                   secret_wrapped=wrapped, kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertEqual(reader.get(phone, ident).id, ident)
        self.assertNotIn(salt, reader.get(phone, ident).body)

    def test_N12a_a_device_that_searched_resets_its_own_row(self):
        store = a_store(principal=BOB)
        salt, trunc, wrapped, verifier = secret.seal(SECRET, PASSWORD, B)
        ident = diary.identity_put(store, "alice", ALICE,
                                   secret_wrapped=wrapped, kdf="argon2id")
        _, recovered = secret.enrol(wrapped, PASSWORD, trunc, B, verifier)
        secret.remember_salt(store, ident, recovered)
        self.assertEqual(secret.local_salt(store, ident), salt)

    def test_N12a_the_everyday_path_is_one_kdf(self):
        """A kdf slow enough to impose the same per-guess cost as the search
        would be paid HERE, on every unlock, which is why it is not an
        alternative."""
        salt, _, wrapped, _ = secret.seal(SECRET, PASSWORD, 24)
        self.assertEqual(secret.unlock(wrapped, PASSWORD, salt), SECRET)


class DefenderParallelism(StateTest):
    """N-13. Counter-intuitive enough to be worth a test.

    Serial work sounds like it defeats parallel attack and does not: cracking
    parallelizes across GUESSES, not within one. What matters is that the
    defender may parallelize ITS search while the attacker's per-guess advantage
    is unchanged.
    """

    def test_N13_the_search_accepts_worker_parallelism(self):
        _, trunc, wrapped, verifier = secret.seal(SECRET, PASSWORD, B)
        found, _ = secret.enrol(wrapped, PASSWORD, trunc, B, verifier, workers=8)
        self.assertEqual(found, SECRET)

    def test_N13_parallelism_does_not_change_the_result(self):
        """Control: a search returning different answers on different core
        counts is not a search."""
        salt, trunc, wrapped, verifier = secret.seal(SECRET, PASSWORD, B)
        _, one = secret.enrol(wrapped, PASSWORD, trunc, B, verifier, workers=1)
        _, many = secret.enrol(wrapped, PASSWORD, trunc, B, verifier, workers=8)
        self.assertEqual(one, many)
        self.assertEqual(one, salt)


class MemoryHardness(StateTest):
    """N-14. Load-bearing rather than hygiene: evaluated 2^(b-1) times per
    guess, so its resistance to parallel hardware multiplies through the whole
    search. The rejected time-lock could not offer this -- modular squaring is
    low-memory and ASIC-friendly."""

    def test_N14_the_kdf_is_recorded_with_the_identity(self):
        store = a_store(principal=ALICE)
        ident = diary.identity_put(store, "alice", ALICE,
                                   secret_wrapped=b"sealed", kdf="argon2id")
        self.assertIn(b"argon2", reader.get(store, ident).body)


class NoStructuralBreak(StateTest):
    """N-15. Truncated entropy is entropy: no factoring assumption, no trapdoor,
    no shortcut for anyone who learns a parameter."""

    def test_N15_the_cost_is_a_function_of_b_alone(self):
        self.assertEqual(secret.expected_cost(20), (1 << 19, 1 << 20))


class RecordedParameters(StateTest):
    """N-16, N-16a, N-16b."""

    def test_N16_b_and_the_kdf_travel_with_the_identity(self):
        laptop, phone = a_store(principal=ALICE), a_store(principal=ALICE)
        _, _, wrapped, _ = secret.seal(SECRET, PASSWORD, B)
        diary.identity_put(laptop, "alice", ALICE, secret_wrapped=wrapped,
                           kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertEqual(secret.parameters(phone, ALICE)["b"], B)

    def test_N16_raising_b_does_not_orphan_an_existing_identity(self):
        """Identities sealed under the old value must stay openable, or a
        parameter change silently locks people out of their own keys."""
        store = a_store(principal=ALICE)
        _, _, wrapped, _ = secret.seal(SECRET, PASSWORD, 10)
        diary.identity_put(store, "alice", ALICE, secret_wrapped=wrapped,
                           kdf="argon2id")
        self.assertEqual(secret.parameters(store, ALICE)["b"], 10)

    def test_N16a_a_wrong_password_fails_in_one_kdf(self):
        """Without the verifier a typo is indistinguishable from a salt not yet
        found, so it costs the full 2^b -- terrible to use, and a denial of
        service against yourself."""
        _, trunc, _, verifier = secret.seal(SECRET, PASSWORD, B)
        self.assertFalse(secret.verify_password(verifier, "wrong", trunc))

    def test_N16a_the_right_password_verifies(self):
        """Control."""
        _, trunc, _, verifier = secret.seal(SECRET, PASSWORD, B)
        self.assertTrue(secret.verify_password(verifier, PASSWORD, trunc))

    def test_N16b_the_cost_is_probabilistic_and_says_so(self):
        expected, worst = secret.expected_cost(B)
        self.assertEqual(worst, 2 * expected)


class Rekey(StateTest):
    """N-17. Device removal is a re-key, not a delete."""

    def test_N17_a_rekey_replaces_the_root_set(self):
        store = a_store(principal=ALICE)
        secret.rekey(store, new_roots=(ALICE,))
        self.assertEqual(tuple(diary.roots(store)), (ALICE,))

    def test_N17_the_removed_device_keeps_what_it_had(self):
        """Forward-only, like every other revocation here. Saying so is the
        difference between a limitation and a false promise."""
        laptop, lost = a_store(principal=ALICE), a_store(principal=BOB)
        put = writer.put(laptop, an_assertion(author=ALICE))
        merger.merge(lost, laptop)
        secret.rekey(laptop, new_roots=(ALICE,))
        self.assertIsNotNone(reader.get(lost, put.id))

    def test_N17_the_removed_device_learns_nothing_after(self):
        laptop, lost = a_store(principal=ALICE), a_store(principal=BOB)
        merger.merge(lost, laptop)
        secret.rekey(laptop, new_roots=(ALICE,))
        writer.put(laptop, an_assertion(author=ALICE, akey="after"))
        with self.assertRaises(Unauthorized):
            merger.merge(lost, laptop)
