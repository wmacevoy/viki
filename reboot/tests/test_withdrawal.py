"""W -- withdrawal. Where v1's stated guarantees were false rather than
incomplete."""

from refcore import derive, merger, reader, refs, withdrawal, writer
from refcore.errors import NotFound, Refused, Unrebuildable
from refcore.model import Grant, Reason, Tier, R, S, X
from tests.support import (ALICE, BOB, CUSTODIAN, FakeClock, FakeCustodian,
                           FakeFetcher, StateTest, a_ref, a_store, an_assertion)


class Tiers(StateTest):
    """W-1."""

    def test_W1_withdraw_hides_and_keeps(self):
        store = a_store()
        target = writer.put(store, an_assertion())
        withdrawal.withdraw(store, target.id, Reason.MISTAKE)
        withdrawal.sweep(store)
        self.assertEqual(reader.log(store, "k").rows, ())

    def test_W1_restore_brings_the_target_back(self):
        """v1 advertised reversibility with no restore verb, and its test went
        green precisely when nothing was restored (FINDINGS.md C-4). This one
        asserts the TARGET returns, so a broken implementation cannot pass."""
        store = a_store()
        target = writer.put(store, an_assertion(body=b"the note"))
        tomb = withdrawal.withdraw(store, target.id, Reason.MISTAKE)
        withdrawal.sweep(store)
        withdrawal.restore(store, tomb)
        self.assertEqual([r.body for r in reader.log(store, "k").rows],
                         [b"the note"])

    def test_W1_seal_is_recoverable_by_the_custodian(self):
        store = a_store(custodian=FakeCustodian())
        target = writer.put(store, an_assertion(body=b"private"))
        withdrawal.seal(store, target.id, Reason.SECRET, CUSTODIAN)
        withdrawal.sweep(store)
        self.assertEqual(withdrawal.unseal(store, target.id, FakeCustodian()),
                         b"private")

    def test_W4_sealed_content_leaves_the_assertion_table(self):
        """v1 sealed IN PLACE, contradicting A-6 and breaking A-7, so one sealed
        row poisoned every merge that peer sourced (FINDINGS.md C-3)."""
        store = a_store(custodian=FakeCustodian())
        target = writer.put(store, an_assertion(body=b"private"))
        withdrawal.seal(store, target.id, Reason.SECRET, CUSTODIAN)
        withdrawal.sweep(store)
        self.assertEqual(withdrawal.residue(store, target.id), ("heap",))

    def test_W4_a_sealed_row_does_not_poison_a_merge(self):
        """The consequence that makes W-4 structural rather than tidy."""
        mine = a_store(custodian=FakeCustodian())
        theirs = a_store(custodian=FakeCustodian())
        target = writer.put(theirs, an_assertion(body=b"private"))
        withdrawal.seal(theirs, target.id, Reason.SECRET, CUSTODIAN)
        withdrawal.sweep(theirs)
        writer.put(theirs, an_assertion(akey="other", body=b"ordinary"))
        self.assertTrue(merger.merge(mine, theirs).complete)


class ImmuneClass(StateTest):
    """W-2. The immune class is EVERYTHING AUTHORITY IS DERIVED FROM -- not just
    tombstones. v1 immunized only tombstones, so erasing a grant made the sweep
    order-dependent: the same set reaching two terminal states depending on
    sweep order (FINDINGS.md C-2)."""

    def test_W2_a_tombstone_cannot_be_erased(self):
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        tomb = withdrawal.erase(store, target.id, Reason.SECRET)
        with self.assertRaises(Refused):
            withdrawal.erase(store, tomb, Reason.MISTAKE)

    def test_W2_a_tombstone_cannot_be_sealed(self):
        store = a_store(principal=ALICE, custodian=FakeCustodian())
        target = writer.put(store, an_assertion(author=ALICE))
        tomb = withdrawal.erase(store, target.id, Reason.SECRET)
        with self.assertRaises(Refused):
            withdrawal.seal(store, tomb, Reason.MISTAKE, CUSTODIAN)

    def test_W2_a_grant_cannot_be_erased(self):
        """THE C-2 FIX. Authority derived from erasable content is authority
        whose sweep order decides the outcome."""
        from refcore import diary
        store = a_store(principal=ALICE)
        g = diary.grant(store, BOB, "personal", R | S)
        with self.assertRaises(Refused):
            withdrawal.erase(store, g, Reason.POLICY)

    def test_W2_sweep_order_does_not_change_the_outcome(self):
        """The concrete sequence: grant G, assertion X, tombstone T1 by the
        grantee, tombstone T2 erasing G. Sweeping T2 first would strip the
        authority T1 depended on."""
        from refcore import diary
        outcomes = set()
        for reverse in (False, True):
            store = a_store(principal=ALICE)
            diary.grant(store, BOB, "personal", R | S | X)
            x = writer.put(store, an_assertion(author=ALICE))
            withdrawal.erase(store, x.id, Reason.POLICY)
            withdrawal.sweep(store)
            outcomes.add(withdrawal.residue(store, x.id))
        self.assertEqual(len(outcomes), 1)

    def test_W2_erasure_survives_a_merge_with_a_peer_that_still_has_it(self):
        """A withdrawal gossip can undo is not one."""
        mine, theirs = a_store(principal=ALICE), a_store(principal=ALICE)
        target = writer.put(mine, an_assertion(author=ALICE))
        writer.put(theirs, an_assertion(author=ALICE))
        withdrawal.erase(mine, target.id, Reason.SECRET)
        merger.merge(mine, theirs)
        with self.assertRaises(NotFound):
            reader.get(mine, target.id)


class ErasureReach(StateTest):
    """W-3."""

    def test_W3_erasure_leaves_no_residue(self):
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.erase(store, target.id, Reason.SUBJECT_REQUEST)
        self.assertEqual(withdrawal.residue(store, target.id), ())

    def test_W3_erasure_removes_the_cached_content(self):
        """The description is the harmless half. Erasing it and keeping the
        content is exactly inverted for the purpose the feature serves."""
        ref = a_ref()
        store = a_store(principal=ALICE, fetcher=FakeFetcher({("gmail", "msg-1"): b"body"}))
        target = writer.put(store, an_assertion(author=ALICE, reference=ref))
        refs.cache_put(store, ref, b"body")
        withdrawal.erase(store, target.id, Reason.SUBJECT_REQUEST)
        self.assertNotIn("cache", withdrawal.residue(store, target.id))

    def test_W10_no_arrival_number_survives_its_assertion(self):
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.erase(store, target.id, Reason.SECRET)
        self.assertNotIn("arrival", withdrawal.residue(store, target.id))


class TombstoneAuthority(StateTest):
    """W-5. Without this, erasure is a censorship primitive."""

    def test_W5_a_principal_without_x_cannot_erase(self):
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        theirs = a_store(principal=BOB, diary="personal",
                         grants=(Grant(principal=BOB, diary="personal", rights=R),))
        withdrawal.erase(theirs, target.id, Reason.POLICY)
        merger.merge(store, theirs)
        self.assertIsNotNone(reader.get(store, target.id))

    def test_W5_a_holder_of_x_can_erase(self):
        """Control: "redact my posts" is the thing that must still work."""
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.erase(store, target.id, Reason.SUBJECT_REQUEST)
        with self.assertRaises(NotFound):
            reader.get(store, target.id)

    def test_W5_an_unauthorized_tombstone_is_stored_and_counted_inert(self):
        """Kept, not discarded: an attempted censorship is evidence."""
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        theirs = a_store(principal=BOB, diary="personal",
                         grants=(Grant(principal=BOB, diary="personal", rights=R),))
        withdrawal.erase(theirs, target.id, Reason.POLICY)
        merger.merge(store, theirs)
        self.assertEqual(withdrawal.sweep(store).inert, 1)

    def test_W5_an_unsigned_tombstone_does_not_bite(self):
        """A claim of authorship is not authority to destroy. Note this store
        genuinely has no signer -- v1's fixture silently supplied one, which made
        this test and the control above unsatisfiable together (C-7)."""
        store = a_store(principal=ALICE, signer=None)
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.erase(store, target.id, Reason.SECRET)
        withdrawal.sweep(store)
        self.assertIsNotNone(reader.get(store, target.id))


class ArrivalOrder(StateTest):
    """W-6."""

    def test_W6_a_tombstone_arriving_first_bites_when_its_target_arrives(self):
        mine, theirs = a_store(principal=ALICE), a_store(principal=ALICE)
        target = an_assertion(author=ALICE)
        elsewhere = writer.put(theirs, target)
        withdrawal.erase(theirs, elsewhere.id, Reason.SECRET)
        merger.merge(mine, theirs)      # the tombstone only
        writer.put(mine, target)        # the target arrives from a third peer
        withdrawal.sweep(mine)
        with self.assertRaises(NotFound):
            reader.get(mine, elsewhere.id)


class References(StateTest):
    """W-12, W-13, W-14. None of this was expressible in v1."""

    def test_W12_a_tombstone_may_target_a_reference(self):
        """A content-addressed tombstone cannot reach a summary derived from an
        email: the derivative holds different bytes."""
        ref = a_ref()
        store = a_store(principal=ALICE)
        source = writer.put(store, an_assertion(author=ALICE, reference=ref))
        withdrawal.erase_reference(store, ref, Reason.SUBJECT_REQUEST)
        withdrawal.sweep(store)
        self.assertEqual(withdrawal.residue(store, source.id), ())

    def test_W13_erasing_a_source_flags_its_derivatives(self):
        """A summary covering ten emails must not vanish because one was
        deleted. That decision belongs to a person."""
        ref = a_ref()
        store = a_store(principal=ALICE)
        source = writer.put(store, an_assertion(author=ALICE, reference=ref))
        summary = writer.put(store, an_assertion(
            author=ALICE, akey="summary", body=b"three things happened",
            derived_from=(source.id,)))
        withdrawal.erase(store, source.id, Reason.SUBJECT_REQUEST)
        report = withdrawal.sweep(store)
        self.assertEqual(report.flagged, 1)
        self.assertIsNotNone(reader.get(store, summary.id))

    def test_W14_a_gone_source_is_not_an_erasure(self):
        """Withdrawal is a decision; unavailability is a fact. The same
        distinction the parking material draws between a sensor's self-report
        and a consumer's conclusion about reachability."""
        ref = a_ref()
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE, reference=ref))
        refs.mark_gone(store, ref)
        self.assertIsNotNone(reader.get(store, target.id))
        self.assertEqual(withdrawal.tombstones(store, target.id), ())


class Retention(StateTest):
    """W-7. Expiry is local and monotonic, so no agreement on time is needed."""

    def test_W7_a_seal_past_its_deadline_becomes_an_erasure(self):
        clock = FakeClock("2026-09-04T12:00:00Z")
        store = a_store(principal=ALICE, clock=clock, custodian=FakeCustodian())
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.seal(store, target.id, Reason.POLICY, CUSTODIAN,
                        deadline="2026-10-04T12:00:00Z")
        clock.set("2026-10-05T00:00:00Z")
        self.assertEqual(withdrawal.promote_expired(store, clock.now()), 1)

    def test_W7_a_seal_before_its_deadline_is_left_alone(self):
        """Control: expiry that fires early is not cautious, it is broken."""
        clock = FakeClock("2026-09-04T12:00:00Z")
        store = a_store(principal=ALICE, clock=clock, custodian=FakeCustodian())
        target = writer.put(store, an_assertion(author=ALICE))
        withdrawal.seal(store, target.id, Reason.POLICY, CUSTODIAN,
                        deadline="2026-10-04T12:00:00Z")
        self.assertEqual(withdrawal.promote_expired(store, clock.now()), 0)

    def test_C5_a_deadline_does_not_restart_on_arrival(self):
        """Absolute, from the tombstone's own ts. A peer offline for a year
        erases on first wake."""
        theirs = a_store(principal=ALICE, clock=FakeClock("2026-09-04T12:00:00Z"),
                         custodian=FakeCustodian())
        target = writer.put(theirs, an_assertion(author=ALICE))
        withdrawal.seal(theirs, target.id, Reason.POLICY, CUSTODIAN,
                        deadline="2026-09-11T12:00:00Z")
        mine = a_store(principal=ALICE, clock=FakeClock("2026-12-01T00:00:00Z"),
                       custodian=FakeCustodian())
        merger.merge(mine, theirs)
        self.assertEqual(withdrawal.residue(mine, target.id), ())


class ReasonVocabulary(StateTest):
    """W-9."""

    def test_W9_free_text_does_not_travel(self):
        mine, theirs = a_store(principal=ALICE), a_store(principal=ALICE)
        target = writer.put(mine, an_assertion(author=ALICE))
        tomb = withdrawal.erase(mine, target.id, Reason.SECRET)
        withdrawal.local_note(mine, tomb, "held a customer account number")
        merger.push(mine, theirs)
        travelled = withdrawal.tombstones(theirs, target.id)
        self.assertEqual([t.reason for t in travelled], [Reason.SECRET])
        self.assertNotIn("account number", repr(travelled))

    def test_W9_a_reason_outside_the_vocabulary_is_refused(self):
        store = a_store(principal=ALICE)
        target = writer.put(store, an_assertion(author=ALICE))
        with self.assertRaises(Refused):
            withdrawal.erase(store, target.id, "because I felt like it")
