"""M -- merge. The algebra is the requirement: offline IS delivery that is late,
repeated and out of order."""

import sqlite3

from refcore import merger, reader, writer
from refcore.errors import IncompleteMerge, NoSyncPath, NotAStore
from refcore.model import SigState, SyncPolicy
from tests.support import (ALICE, BOB, FakeSigner, StateTest, a_store,
                           an_assertion, plant)


def ids_in(store, akey="k"):
    return sorted(row.id for row in reader.log(store, akey).rows)


class Algebra(StateTest):

    def test_M1_merge_is_idempotent(self):
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion(body=b"one"))
        merger.merge(mine, theirs)
        before = ids_in(mine)
        merger.merge(mine, theirs)
        self.assertEqual(ids_in(mine), before)

    def test_M1_merge_is_commutative(self):
        src_a, src_b = a_store(), a_store()
        writer.put(src_a, an_assertion(body=b"a"))
        writer.put(src_b, an_assertion(body=b"b"))
        left, right = a_store(), a_store()
        merger.merge(left, src_a); merger.merge(left, src_b)
        merger.merge(right, src_b); merger.merge(right, src_a)
        self.assertEqual(ids_in(left), ids_in(right))

    def test_M1_merge_is_associative(self):
        srcs = []
        for body in (b"a", b"b", b"c"):
            s = a_store()
            writer.put(s, an_assertion(body=body))
            srcs.append(s)
        left, right = a_store(), a_store()
        for s in srcs:
            merger.merge(left, s)
        for s in reversed(srcs):
            merger.merge(right, s)
        self.assertEqual(ids_in(left), ids_in(right))

    def test_M2_merging_a_store_into_itself_adds_nothing(self):
        store = a_store()
        writer.put(store, an_assertion())
        self.assertEqual(merger.merge(store, store).added, 0)


class Completeness(StateTest):
    """M-3."""

    def test_E4_a_source_that_is_not_a_store_is_refused(self):
        empty = sqlite3.connect(":memory:")
        empty.execute("CREATE TABLE something_else (x)")
        with self.assertRaises(NotAStore):
            merger.merge(a_store(), empty)

    def test_M3_a_complete_merge_says_so(self):
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion())
        self.assertTrue(merger.merge(mine, theirs).complete)


class Quarantine(StateTest):
    """M-7. The F-8 fix.

    v1 required abort on a bad row, which made one planted row a permanent
    denial of sync: the row is immutable and content-addressed so it never goes
    away, and skipping it would be a partial union M-3 forbids calling whole.
    Quarantine-and-count is the third answer.
    """

    def test_M7_one_bad_row_does_not_wedge_the_merge(self):
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion(body=b"good"))
        # Planted, not put: A-7 refuses a mismatched id at the write path, so a
        # corrupt row can only arrive the way a corrupt row really arrives
        # (FINDINGS T-1a).
        plant(theirs, an_assertion(akey="other", body=b"bad"), "not-a-hash")
        report = merger.merge(mine, theirs)
        self.assertEqual(report.quarantined, 1)
        self.assertEqual(report.added, 1)

    def test_M7_a_quarantined_merge_is_not_reported_complete(self):
        """Honest about what did not land, and not a wedge. Both halves."""
        mine, theirs = a_store(), a_store()
        plant(theirs, an_assertion(body=b"bad"), "not-a-hash")
        self.assertFalse(merger.merge(mine, theirs).complete)

    def test_M7_a_clean_merge_quarantines_nothing(self):
        """Control."""
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion())
        self.assertEqual(merger.merge(mine, theirs).quarantined, 0)


class Signatures(StateTest):
    """M-4."""

    def test_M4_signatures_arrive_with_their_assertions(self):
        """Without this a peer receives a statement while losing the evidence of
        who stood behind it -- which is the half W-5 then needs to judge a
        tombstone."""
        mine, theirs = a_store(), a_store()
        put = writer.put(theirs, an_assertion(author=ALICE))
        merger.merge(mine, theirs)
        self.assertEqual(reader.sig_states(mine, put.id).get(ALICE),
                         SigState.VERIFIED)

    def test_M4_a_signed_copy_is_not_shadowed_by_an_unsigned_one(self):
        """The reason signatures are rows and not a column: under an
        insert-or-ignore union, a signature riding on the assertion means
        whichever copy arrives first wins. The unsigned source here genuinely
        has no signer -- v1's fixture silently supplied one, which made this
        test vacuous (FINDINGS.md C-7)."""
        unsigned_src = a_store(signer=None)
        signed_src = a_store()
        put = writer.put(unsigned_src, an_assertion(author=ALICE))
        writer.put(signed_src, an_assertion(author=ALICE))
        mine = a_store()
        merger.merge(mine, unsigned_src)
        merger.merge(mine, signed_src)
        self.assertEqual(reader.sig_states(mine, put.id).get(ALICE),
                         SigState.VERIFIED)


class Projection(StateTest):
    """M-5."""

    def test_M5_merged_rows_arrive_unprojected_and_are_counted(self):
        """A merge produces a partially projected store by construction, and a
        partially projected store is indistinguishable from a small one by any
        total."""
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion())
        merger.merge(mine, theirs)
        self.assertEqual(reader.unprojected(mine), 1)


class Direction(StateTest):
    """M-6, M-8."""

    def test_M6_push_moves_rows_to_the_destination(self):
        mine, theirs = a_store(), a_store()
        put = writer.put(mine, an_assertion())
        merger.push(mine, theirs)
        self.assertIsNotNone(reader.get(theirs, put.id))

    def test_M6_a_pushed_tombstone_bites_on_the_destination(self):
        """Everything that happens on receipt happens to the destination."""
        from refcore import withdrawal
        from refcore.model import Reason
        mine, hub = a_store(principal=ALICE), a_store(principal=ALICE)
        put = writer.put(mine, an_assertion(author=ALICE))
        merger.push(mine, hub)
        withdrawal.erase(mine, put.id, Reason.SECRET)
        merger.push(mine, hub)
        self.assertEqual(withdrawal.residue(hub, put.id), ())

    def test_M8_diaries_of_different_identity_do_not_merge(self):
        """The Gmail diary and the assistant diary are different stores with
        different policies. Merging them is the accident V-4 exists to prevent,
        one layer down."""
        gmail = a_store(diary="gmail", policy=SyncPolicy.DOMAIN)
        assistant = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        writer.put(gmail, an_assertion())
        with self.assertRaises(NoSyncPath):
            merger.merge(assistant, gmail)
