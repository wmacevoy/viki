"""S -- anti-entropy. How a peer learns what it lacks and gets it.

The M series tests the ALGEBRA of union. These test the PROTOCOL, which did not
exist before: every sync was a full scan that re-hashed every body and
re-verified every signature, so the sender chose the receiver's CPU cost
(FINDINGS.md F-8).

U-3 is what makes this a requirement rather than an optimization: a phone syncing
the assistant diary on battery, over a connection that drops, against a store
that only ever grows.
"""

from datetime import datetime, timedelta

from refcore import merger, sync, withdrawal, writer
from refcore.errors import NoSyncPath, Refused
from refcore.model import Reason, SyncPolicy
from tests.support import (ALICE, BOB, StateTest, a_ref, a_store, an_assertion)


def a_ts(i):
    """Valid ISO for any i. The first version of this was
    `"2026-09-04T12:%02d:00Z" % i`, which yields '12:60:00Z' at i=60 and worse
    beyond -- so the 500-row digest test could never pass a C-3-compliant
    implementation, and check.py could not see it because the test died one
    frame earlier in writer.py."""
    return (datetime(2026, 9, 4, 12, 0, 0)
            + timedelta(seconds=i)).strftime("%Y-%m-%dT%H:%M:%SZ")


def fill(store, n, prefix=b"row"):
    return [writer.put(store, an_assertion(akey="k%d" % i,
                                           body=prefix + b"-%d" % i,
                                           ts=a_ts(i))).id
            for i in range(n)]


class LearnWithoutReceiving(StateTest):
    """S-1. The question and the transfer are separate operations, which is what
    lets a phone decide whether a sync is worth the battery before spending it."""

    def test_S1_a_peer_can_ask_what_it_lacks(self):
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        wanted = fill(theirs, 3)
        ids, _ = sync.lacking(mine, sync.digest(theirs))
        self.assertEqual(sorted(ids), sorted(wanted))

    def test_S1_asking_transfers_nothing(self):
        """The whole point of separating them. After asking, the store still
        holds nothing."""
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        fill(theirs, 3)
        sync.lacking(mine, sync.digest(theirs))
        self.assertEqual(sync.digest(mine).count, 0)

    def test_S1_a_converged_peer_lacks_nothing(self):
        """Control: an answer that always lists rows is not an answer."""
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        fill(theirs, 3)
        merger.merge(mine, theirs)
        ids, complete = sync.lacking(mine, sync.digest(theirs))
        self.assertEqual(ids, ())
        self.assertTrue(complete)


class ConvergedPairsTransferNothing(StateTest):
    """S-2. THE HEADLINE. The property the full-scan design could not express at
    all, and the one the phone case hits first."""

    def test_S2_a_second_sync_transfers_nothing(self):
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 5)
        first = sync.sync(phone, laptop)
        second = sync.sync(phone, laptop)
        self.assertEqual(first.transferred, 5)
        self.assertEqual(second.transferred, 0)
        self.assertTrue(second.complete)

    def test_S2_one_new_row_transfers_one_row(self):
        """Cost proportional to the DIFFERENCE, not to the store."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 20)
        sync.sync(phone, laptop)
        writer.put(laptop, an_assertion(akey="new", body=b"one more"))
        self.assertEqual(sync.sync(phone, laptop).transferred, 1)

    def test_S11_a_resync_reverifies_nothing(self):
        """Without this, incremental TRANSFER still costs a full VERIFICATION
        and "incremental" is a claim rather than a property."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 5)
        sync.sync(phone, laptop)
        self.assertEqual(sync.sync(phone, laptop).reverified, 0)


class CompletenessOfTheAnswer(StateTest):
    """S-1a. THE FIX FOR FINDINGS T-1d -- and T-1d itself was overstated.

    A fixed-size sketch does recover the exact missing ids while the difference
    stays inside its capacity; a 256-cell invertible Bloom lookup table, 17 KB
    regardless of store size, was measured recovering 120 of 120. Past capacity
    it fails to decode, and the honest interface says so rather than returning a
    short list that looks like an answer.
    """

    def test_S1a_a_small_difference_is_answered_completely(self):
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        fill(theirs, 3)
        ids, complete = sync.lacking(mine, sync.digest(theirs))
        self.assertEqual(len(ids), 3)
        self.assertTrue(complete)

    def test_S1a_a_difference_past_capacity_says_it_is_partial(self):
        """Otherwise this is S-8's failure mode moved one function earlier: a
        truncated answer indistinguishable from a converged one."""
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        fill(theirs, 5000)
        ids, complete = sync.lacking(mine, sync.digest(theirs))
        self.assertFalse(complete)
        self.assertGreater(len(ids), 0)

    def test_S1a_partial_means_ask_again_not_nothing_more(self):
        """Taking what it recovered and asking again must converge, or a large
        first sync can never finish."""
        mine, theirs = a_store(diary="assistant"), a_store(diary="assistant")
        fill(theirs, 5000)
        sync.sync(mine, theirs)
        _, complete = sync.lacking(mine, sync.digest(theirs))
        self.assertTrue(complete)


class BoundedExchange(StateTest):
    """S-3. Shipping a list of every id to discover nothing changed is the
    failure this forbids."""

    def test_S3_a_digest_does_not_grow_with_the_store(self):
        small, large = a_store(diary="assistant"), a_store(diary="assistant")
        fill(small, 5)
        fill(large, 500)
        self.assertEqual(len(sync.digest(small).ranges),
                         len(sync.digest(large).ranges))

    def test_S3_a_digest_still_distinguishes_different_stores(self):
        """Control: a constant digest is bounded and useless."""
        one, two = a_store(diary="assistant"), a_store(diary="assistant")
        fill(one, 5)
        fill(two, 5, prefix=b"other")
        self.assertNotEqual(sync.digest(one).root, sync.digest(two).root)

    def test_S3_identical_stores_share_a_root(self):
        one, two = a_store(diary="assistant"), a_store(diary="assistant")
        fill(one, 5)
        merger.merge(two, one)
        self.assertEqual(sync.digest(one).root, sync.digest(two).root)


class Watermarks(StateTest):
    """S-4, S-5, S-6. What justifies the `arrival` table, which until the S
    series was a mechanism whose only requirement was to clean itself up."""

    def test_S4_a_watermark_is_per_peer(self):
        """`seq` orders MY receipts, not their writes, so one number would be
        wrong for every peer but the last one synced."""
        laptop = a_store(principal=ALICE, diary="assistant")
        phone = a_store(principal=BOB, diary="assistant")
        fill(laptop, 4)
        sync.push(laptop, phone)
        self.assertGreater(sync.watermark(laptop, BOB), 0)
        self.assertEqual(sync.watermark(laptop, "tablet-key"), 0)

    def test_S5_the_diff_is_right_even_when_the_watermark_is_wrong(self):
        """S-5's falsifier. A watermark is an optimization; `lacking` is the
        answer, and an implementation that trusts the watermark fails here."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 3)
        sync.advance(laptop, BOB, 999)          # a lie, however it got there
        ids, _ = sync.lacking(phone, sync.digest(laptop))
        self.assertEqual(len(ids), 3)

    def test_S6_a_watermark_rounds_down(self):
        """The two errors are not symmetric: too low costs bandwidth and merge
        is idempotent; too high silently skips rows and reports success."""
        laptop = a_store(principal=ALICE, diary="assistant")
        phone = a_store(principal=BOB, diary="assistant")
        fill(laptop, 3)
        sync.push(laptop, phone)
        honest = sync.watermark(laptop, BOB)
        self.assertEqual(sync.advance(laptop, BOB, honest + 100), honest)

    def test_S6_a_watermark_does_not_move_backwards_either(self):
        """Rounding down is a clamp on the optimistic side, not a reset."""
        laptop = a_store(principal=ALICE, diary="assistant")
        phone = a_store(principal=BOB, diary="assistant")
        fill(laptop, 3)
        sync.push(laptop, phone)
        honest = sync.watermark(laptop, BOB)
        sync.advance(laptop, BOB, 0)
        self.assertEqual(sync.watermark(laptop, BOB), honest)


class Resumption(StateTest):
    """S-7. A phone that loses a connection continues rather than starting
    over."""

    def test_S7_an_interrupted_sync_resumes(self):
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 10)
        partial = sync.sync(phone, laptop, budget=4)
        rest = sync.sync(phone, laptop, budget=0, cursor=partial.cursor)
        self.assertEqual(partial.transferred, 4)
        self.assertEqual(rest.transferred, 6)
        self.assertTrue(rest.complete)

    def test_S7_a_resumed_sync_does_not_resend_what_arrived(self):
        """Restarting would be correct and wasteful; the point is that it does
        not restart."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 10)
        partial = sync.sync(phone, laptop, budget=4)
        rest = sync.sync(phone, laptop, cursor=partial.cursor)
        self.assertEqual(rest.already_held, 0)

    def test_S7_a_finished_sync_carries_no_cursor(self):
        """Control: a cursor that is always set cannot mean "resume here"."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 3)
        self.assertEqual(sync.sync(phone, laptop).cursor, "")


class ReceiverBoundsItsCost(StateTest):
    """S-8. A sender does not choose how much work a receiver does."""

    def test_S8_a_budget_caps_what_one_call_takes(self):
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 50)
        self.assertEqual(sync.sync(phone, laptop, budget=5).transferred, 5)

    def test_S8_stopping_on_a_budget_is_reported(self):
        """A truncated sync is otherwise indistinguishable from a converged one
        -- and an empty answer reads as a quiet day."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 50)
        report = sync.sync(phone, laptop, budget=5)
        self.assertTrue(report.bounded)
        self.assertFalse(report.complete)

    def test_S8_finishing_within_budget_is_not_reported_as_bounded(self):
        """Control: a flag that is always set carries nothing."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        fill(laptop, 3)
        report = sync.sync(phone, laptop, budget=100)
        self.assertFalse(report.bounded)
        self.assertTrue(report.complete)


class Hub(StateTest):
    """S-9. U-2's cloud PM: a relay that holds no key."""

    def test_S9_a_hub_relays_what_it_cannot_read(self):
        laptop = a_store(principal=ALICE, diary="assistant")
        hub = a_store(principal=BOB, diary="assistant", grants=())
        phone = a_store(principal=ALICE, diary="assistant")
        fill(laptop, 3)
        sync.relay(hub, laptop)
        sync.sync(phone, hub)
        self.assertEqual(sync.lacking(phone, sync.digest(laptop))[0], ())

    def test_S9_a_hub_applies_tombstones(self):
        """THE ONE A RELAY CANNOT BE EXCUSED FROM. A hub that does not sweep
        re-serves erased content to every peer that pulls from it, and holding
        no key is no excuse -- a tombstone names its target by id and needs no
        plaintext to act on."""
        laptop = a_store(principal=ALICE, diary="assistant")
        hub = a_store(principal=BOB, diary="assistant", grants=())
        target = writer.put(laptop, an_assertion(author=ALICE))
        sync.relay(hub, laptop)
        withdrawal.erase(laptop, target.id, Reason.SECRET)
        sync.relay(hub, laptop)
        self.assertEqual(withdrawal.residue(hub, target.id), ())


class Scope(StateTest):
    """S-10. Sync obeys the diary boundary, before any bytes move."""

    def test_S10_a_never_diary_is_not_a_sync_source(self):
        """U-1: the Gmail diary must never sync anywhere, and that has to hold
        on the incremental path as well as the full one."""
        gmail = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        other = a_store(diary="gmail", policy=SyncPolicy.DOMAIN)
        with self.assertRaises(NoSyncPath):
            sync.sync(other, gmail)

    def test_S10_different_diaries_do_not_sync(self):
        gmail = a_store(diary="gmail", policy=SyncPolicy.DOMAIN)
        assistant = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        with self.assertRaises(NoSyncPath):
            sync.sync(assistant, gmail)

    def test_S10_a_digest_names_its_diary(self):
        """So a mismatch is catchable before a transfer rather than after."""
        self.assertEqual(sync.digest(a_store(diary="assistant")).diary,
                         "assistant")


class PeerState(StateTest):
    """D-6 extended by S-4. A store that cannot say where it sends its contents
    is not usable, and that now includes how far it got."""

    def test_S4_a_store_reports_its_state_per_peer(self):
        laptop = a_store(principal=ALICE, diary="assistant")
        phone = a_store(principal=BOB, diary="assistant")
        fill(laptop, 3)
        sync.push(laptop, phone)
        described = sync.state(laptop, BOB)
        self.assertIn("given_thru", described)
        self.assertIn("last_sync", described)
