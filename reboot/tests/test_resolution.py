"""R -- resolution, and R-8's two views over one mechanism.

The v1 defect this file exists to not repeat: `current` both required a unique
arm AND carried a rank tiebreak, so the tiebreak was unreachable and the fork
tests were unsatisfiable alongside the ranking tests (FINDINGS.md C-7). `current`
and `resolve` are now different functions with different contracts.
"""

import itertools

from refcore import ids, reader, writer
from refcore.errors import Forked, NotFound
from refcore.model import R, S, X
from tests.support import (pack_rank, ALICE, BOB, FakeSigner, StateTest, a_store,
                           an_assertion)


class Head(StateTest):
    """R-1. The replacement view."""

    def test_R1_the_head_of_a_chain_is_current(self):
        store = a_store()
        first = writer.put(store, an_assertion(body=b"first"))
        writer.put(store, an_assertion(body=b"second", supersedes=(first.id,)))
        self.assertEqual(reader.current(store, "k").body, b"second")

    def test_R1_a_three_link_chain_resolves_to_its_head(self):
        store = a_store()
        a = writer.put(store, an_assertion(body=b"a"))
        b = writer.put(store, an_assertion(body=b"b", supersedes=(a.id,)))
        writer.put(store, an_assertion(body=b"c", supersedes=(b.id,)))
        self.assertEqual(reader.current(store, "k").body, b"c")

    def test_R1_rank_is_not_consulted_for_a_chain(self):
        """A chain has one head. An older-ranked successor still wins, because
        the causal edge is exact and the clock is not."""
        store = a_store()
        first = writer.put(store, an_assertion(body=b"first",
                                               ts="2026-09-04T12:00:00Z"))
        writer.put(store, an_assertion(body=b"second", rank=pack_rank(counter=0),
                                       ts="2026-09-04T11:00:00Z",
                                       supersedes=(first.id,)))
        self.assertEqual(reader.current(store, "k").body, b"second")

    def test_R1_an_unknown_akey_is_not_found(self):
        with self.assertRaises(NotFound):
            reader.current(a_store(), "nothing-here")

    def test_R1_a_forked_akey_refuses(self):
        store = a_store()
        writer.put(store, an_assertion(body=b"left"))
        writer.put(store, an_assertion(body=b"right"))
        with self.assertRaises(Forked):
            reader.current(store, "k")


class Resolve(StateTest):
    """R-2. Deterministic fork resolution -- because a summary on a phone must
    answer without prompting anyone."""

    def test_R2_two_stores_resolve_a_fork_identically(self):
        """Inserted in opposite orders. Without an explicit tiebreak the winner
        is whatever the query plan yields."""
        one, two = a_store(), a_store()
        a = an_assertion(body=b"aaa", ts="2026-09-04T12:00:00Z")
        b = an_assertion(body=b"bbb", ts="2026-09-04T12:00:00Z")
        writer.put(one, a); writer.put(one, b)
        writer.put(two, b); writer.put(two, a)
        self.assertEqual(reader.resolve(one, "k")[0].id,
                         reader.resolve(two, "k")[0].id)

    def test_R2_resolution_reports_that_it_resolved_a_fork(self):
        """An audit must be told the disagreement existed."""
        store = a_store()
        writer.put(store, an_assertion(body=b"left"))
        writer.put(store, an_assertion(body=b"right"))
        self.assertTrue(reader.resolve(store, "k")[1])

    def test_R2_resolution_of_an_unforked_key_reports_no_fork(self):
        """Control: a flag that is always true carries nothing."""
        store = a_store()
        writer.put(store, an_assertion())
        self.assertFalse(reader.resolve(store, "k")[1])

    def test_R2_higher_rank_wins_before_the_id_tiebreak(self):
        """Rank first, id only to break a tie. An implementation ordering by id
        alone passes the first test in this class and fails this one."""
        store = a_store()
        writer.put(store, an_assertion(body=b"older", rank=pack_rank(counter=1)))
        writer.put(store, an_assertion(body=b"newer", rank=pack_rank(counter=2)))
        self.assertEqual(reader.resolve(store, "k")[0].body, b"newer")


class FoldOrderPurity(StateTest):
    """R-3. The cheapest defence against the class of bug that otherwise appears
    only when two peers sync in different orders."""

    def test_R3_insertion_order_does_not_change_the_result(self):
        rows = [an_assertion(body=b"a", ts="2026-09-04T12:00:00Z"),
                an_assertion(body=b"b", ts="2026-09-04T12:00:01Z"),
                an_assertion(body=b"c", ts="2026-09-04T12:00:01Z")]
        winners = set()
        for order in itertools.permutations(rows):
            store = a_store()
            for row in order:
                writer.put(store, row)
            winners.add(reader.resolve(store, "k")[0].id)
        self.assertEqual(len(winners), 1)


class TwoViews(StateTest):
    """R-8. A conversation and a file's version history are the same edges read
    two ways, not two data models."""

    def test_R8_the_log_view_shows_every_entry(self):
        store = a_store()
        a = writer.put(store, an_assertion(body=b"one"))
        b = writer.put(store, an_assertion(body=b"two", supersedes=(a.id,)))
        writer.put(store, an_assertion(body=b"three", supersedes=(b.id,)))
        self.assertEqual([r.body for r in reader.log(store, "k").rows],
                         [b"one", b"two", b"three"])

    def test_R8_the_replacement_view_shows_only_the_head(self):
        """Same chain, same store, different view. If these disagree about
        which entries exist, the two views are two data models."""
        store = a_store()
        a = writer.put(store, an_assertion(body=b"one"))
        writer.put(store, an_assertion(body=b"two", supersedes=(a.id,)))
        self.assertEqual(reader.current(store, "k").body, b"two")
        self.assertEqual(len(reader.log(store, "k").rows), 2)

    def test_R9_superseding_does_not_remove_the_earlier_entry(self):
        """D-3c. `s` is safe to grant precisely because it destroys nothing: a
        compromised agent can write a later verdict but cannot un-write an
        earlier one."""
        store = a_store(principal=BOB)
        flagged = writer.put(store, an_assertion(author=ALICE, body=b"flagged"))
        writer.put(store, an_assertion(author=BOB, body=b"safe",
                                       supersedes=(flagged.id,)))
        bodies = [r.body for r in reader.log(store, "k").rows]
        self.assertIn(b"flagged", bodies)
        self.assertEqual(reader.current(store, "k").body, b"safe")


class Forks(StateTest):
    """R-5, R-7."""

    def test_R5_two_unsuperseded_arms_are_reported(self):
        store = a_store()
        writer.put(store, an_assertion(body=b"left"))
        writer.put(store, an_assertion(body=b"right"))
        self.assertEqual(len(reader.forks(store, "k").arms), 2)

    def test_R5_an_unforked_key_reports_no_fork(self):
        """Control: a detector that always fires is not a detector."""
        store = a_store()
        writer.put(store, an_assertion())
        self.assertIsNone(reader.forks(store, "k"))

    def test_R7_a_fork_is_healed_by_ONE_assertion_naming_BOTH_arms(self):
        """THE FIX FOR FINDINGS T-1b. Healing with two single-parent assertions
        produces two NEW unsuperseded heads and the fork survives -- which is
        why `supersedes` is a list. One node reconciling two lines is a real
        thing a single parent cannot say, and Fossil has expressed it since 1996
        in a check-in's P card."""
        store = a_store()
        left = writer.put(store, an_assertion(body=b"left"))
        right = writer.put(store, an_assertion(body=b"right"))
        writer.put(store, an_assertion(body=b"merged",
                                       supersedes=(left.id, right.id)))
        self.assertIsNone(reader.forks(store, "k"))
        self.assertEqual(reader.current(store, "k").body, b"merged")

    def test_R7_healing_only_one_arm_leaves_the_fork(self):
        """Control, and it is the defect stated as a test: a single-parent heal
        retires one arm and adds another, so the count does not go down."""
        store = a_store()
        left = writer.put(store, an_assertion(body=b"left"))
        writer.put(store, an_assertion(body=b"right"))
        writer.put(store, an_assertion(body=b"half", supersedes=(left.id,)))
        self.assertEqual(len(reader.forks(store, "k").arms), 2)

    def test_A2_the_superseded_list_is_sorted_before_framing(self):
        """So the same reconciliation written by two peers is ONE assertion.
        Unsorted, the order a peer happened to list the arms in would fork the
        heal itself."""
        left, right = "a" * 64, "b" * 64
        self.assertEqual(
            ids.compute_id(an_assertion(supersedes=(left, right))),
            ids.compute_id(an_assertion(supersedes=(right, left))))


class UnsignedDoesNotResolve(StateTest):
    """I-7. v1's open question 5, answered. Until it was, author-spoofing was a
    supported operation that wins by rank (FINDINGS.md F-10)."""

    def test_I7_an_unsigned_assertion_never_becomes_current(self):
        store = a_store(principal=ALICE, signer=None)
        writer.put(store, an_assertion(author=ALICE))
        with self.assertRaises(NotFound):
            reader.current(store, "k")

    def test_I7_an_unsigned_assertion_is_still_stored_and_readable(self):
        """Stored and reported as a claim, not refused: refusing it would lose
        the evidence that the claim was made."""
        store = a_store(principal=ALICE, signer=None)
        put = writer.put(store, an_assertion(author=ALICE))
        self.assertIsNotNone(reader.get(store, put.id))

    def test_I7_a_signed_assertion_does_become_current(self):
        """Control."""
        store = a_store(principal=ALICE)
        writer.put(store, an_assertion(author=ALICE))
        self.assertIsNotNone(reader.current(store, "k"))
