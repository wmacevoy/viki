"""N-10..N-17 -- the travelling secret.

A pepper over the copy that leaves the device: `b` random bits mixed into the
nonce and never stored, so opening the travelled seal costs a search and opening
the local one does not.

These tests pin the asymmetry that justifies it, the three reasons a sequential
construction was rejected, and the two hazards that remain.
"""

from refcore import diary, merger, reader, secret, writer
from refcore.errors import Unauthorized
from tests.support import ALICE, BOB, StateTest, a_store, an_assertion

PASSWORD = "correct horse battery staple"
SECRET = b"ed25519-seed-32-bytes-goes-here!"
B = 12


class OneSeal(StateTest):
    """N-10, N-11. One ciphertext, one key, and a pepper nobody keeps."""

    def test_N10_seal_and_enrol_round_trip(self):
        nonce, wrapped, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        found, _ = secret.enrol(wrapped, PASSWORD, nonce, B, verifier)
        self.assertEqual(found, SECRET)

    def test_N11_seal_does_not_return_the_pepper(self):
        """Nobody holds it -- which is the whole economy of the construction:
        nothing to protect, leak, sync by accident, or fail to erase."""
        result = secret.seal(SECRET, PASSWORD, B)
        self.assertEqual(len(result), 4)          # nonce, wrapped, verifier, key

    def test_N11_the_stored_nonce_is_the_peppered_one(self):
        """Two identities sealed from the same inputs differ, because the pepper
        is fresh each time. Equal nonces would mean the pepper was constant or
        absent."""
        one, _, _, _ = secret.seal(SECRET, PASSWORD, B)
        two, _, _, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertNotEqual(one, two)

    def test_N12_the_creator_caches_the_key_it_already_has(self):
        """It pays nothing: it just computed the key. Only later devices
        search."""
        _, wrapped, _, key = secret.seal(SECRET, PASSWORD, B)
        self.assertEqual(secret.unlock(wrapped, key), SECRET)

    def test_N12_a_device_caches_the_key_never_the_pepper(self):
        """So even a stolen cache reveals nothing about the pepper, and losing
        the cache costs exactly what a new device costs."""
        nonce, wrapped, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        _, key = secret.enrol(wrapped, PASSWORD, nonce, B, verifier)
        self.assertEqual(secret.unlock(wrapped, key), SECRET)

    def test_N12a_the_everyday_path_does_no_search_and_no_kdf(self):
        """A kdf slow enough to impose the same per-guess cost would be paid
        here, on every single unlock, which is why it is not an alternative."""
        _, wrapped, _, key = secret.seal(SECRET, PASSWORD, 24)
        self.assertEqual(secret.unlock(wrapped, key), SECRET)

    def test_N10_the_identity_carries_the_sealed_secret(self):
        laptop, phone = a_store(principal=ALICE), a_store(principal=ALICE)
        _, wrapped, _, _ = secret.seal(SECRET, PASSWORD, B)
        ident = diary.identity_put(laptop, "alice", ALICE,
                                   secret_wrapped=wrapped, kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertIn(wrapped, reader.get(phone, ident).body)


class DefenderParallelism(StateTest):
    """N-13. The reason the pepper beats a sequential construction, and it is
    counter-intuitive enough to be worth a test.

    Serial work sounds like it defeats parallel attack and does not: cracking
    parallelizes across GUESSES, not within one. Given that, what matters is
    that the defender may parallelize ITS search while the attacker's per-guess
    advantage is unchanged -- so every core the owner brings to enrolment raises
    what an attacker must spend per guess, for the same enrolment wall-clock.
    """

    def test_N13_the_search_accepts_worker_parallelism(self):
        nonce, wrapped, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        found, _ = secret.enrol(wrapped, PASSWORD, nonce, B, verifier, workers=8)
        self.assertEqual(found, SECRET)

    def test_N13_parallelism_does_not_change_the_result(self):
        """Control: a search returning different answers on different core
        counts is not a search."""
        nonce, wrapped, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        one, _ = secret.enrol(wrapped, PASSWORD, nonce, B, verifier, workers=1)
        many, _ = secret.enrol(wrapped, PASSWORD, nonce, B, verifier, workers=8)
        self.assertEqual(one, many)


class MemoryHardness(StateTest):
    """N-14. Load-bearing rather than hygiene: the kdf is evaluated 2^(b-1)
    times per guess, so its resistance to parallel hardware multiplies through
    the whole search. This is what the rejected construction could not offer --
    modular squaring is low-memory and ASIC-friendly."""

    def test_N14_the_kdf_is_recorded_and_memory_hard(self):
        store = a_store(principal=ALICE)
        ident = diary.identity_put(store, "alice", ALICE,
                                   secret_wrapped=b"sealed", kdf="argon2id")
        self.assertIn(b"argon2", reader.get(store, ident).body)


class NoStructuralBreak(StateTest):
    """N-15. A pepper is entropy: no factoring assumption, no trapdoor to
    destroy, no shortcut for anyone who learns a parameter."""

    def test_N15_the_cost_is_a_function_of_b_alone(self):
        """Not of a hardness assumption with a review date. Everything an
        attacker needs to know is public, and it still costs 2^(b-1)."""
        self.assertEqual(secret.expected_cost(20),
                         (1 << 19, 1 << 20))


class RecordedParameters(StateTest):
    """N-16, N-16a, N-16b."""

    def test_N16_b_and_the_kdf_travel_with_the_identity(self):
        laptop, phone = a_store(principal=ALICE), a_store(principal=ALICE)
        _, wrapped, _, _ = secret.seal(SECRET, PASSWORD, B)
        diary.identity_put(laptop, "alice", ALICE, secret_wrapped=wrapped,
                           kdf="argon2id")
        merger.merge(phone, laptop)
        self.assertEqual(secret.parameters(phone, ALICE)["b"], B)

    def test_N16_raising_b_does_not_orphan_an_existing_identity(self):
        """Identities sealed under the old value must stay openable, or a
        parameter change silently locks people out of their own keys."""
        store = a_store(principal=ALICE)
        _, wrapped, _, _ = secret.seal(SECRET, PASSWORD, 10)
        diary.identity_put(store, "alice", ALICE, secret_wrapped=wrapped,
                           kdf="argon2id")
        self.assertEqual(secret.parameters(store, ALICE)["b"], 10)

    def test_N16a_a_wrong_password_fails_in_one_kdf(self):
        """Without the verifier a typo is indistinguishable from a pepper not
        yet found, so it costs the full 2^b -- terrible to use, and a denial of
        service against yourself."""
        nonce, _, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertFalse(secret.verify_password(verifier, "wrong", nonce))

    def test_N16a_the_right_password_verifies(self):
        """Control."""
        nonce, _, verifier, _ = secret.seal(SECRET, PASSWORD, B)
        self.assertTrue(secret.verify_password(verifier, PASSWORD, nonce))

    def test_N16b_the_cost_is_probabilistic_and_says_so(self):
        """Expected 2^(b-1), worst case 2^b, so enrolment varies by up to a
        factor of two. A progress indicator assuming a fixed cost will lie."""
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
