"""C -- time. Two clocks, two meanings, and never a substitution."""

from refcore import ids, merger, reader, writer
from refcore.errors import BadTimestamp
from tests.support import pack_rank, ALICE, FakeClock, StateTest, a_store, an_assertion


class HostOwnsTheClock(StateTest):
    """C-1. What makes a store reproducible from its inputs, and what lets a
    test drive time rather than wait for it."""

    def test_C1_the_same_inputs_produce_the_same_assertion(self):
        one = a_store(clock=FakeClock("2026-09-04T12:00:00Z"))
        two = a_store(clock=FakeClock("2026-09-04T12:00:00Z"))
        self.assertEqual(writer.note(one, "same").id, writer.note(two, "same").id)

    def test_C1_a_different_clock_produces_a_different_assertion(self):
        """Control, and A-2: ts is inside the id."""
        one = a_store(clock=FakeClock("2026-09-04T12:00:00Z"))
        two = a_store(clock=FakeClock("2026-09-04T12:00:01Z"))
        self.assertNotEqual(writer.note(one, "same").id, writer.note(two, "same").id)


class TwoClocks(StateTest):
    """C-2."""

    def test_C2_an_older_assertion_arriving_last_does_not_win(self):
        """Arrival order is not timestamp order. A row that reached me at 40 may
        have reached you at 3, or not at all."""
        mine, late = a_store(), a_store()
        writer.put(mine, an_assertion(body=b"new", ts="2026-09-04T12:00:00Z"))
        writer.put(late, an_assertion(body=b"old", ts="2026-09-04T09:00:00Z"))
        merger.merge(mine, late)
        self.assertEqual(reader.resolve(mine, "k")[0].body, b"new")

    def test_C2_a_local_sequence_never_enters_an_id(self):
        """Folding a local sequence into the id would give the same assertion a
        different id on every peer, and content addressing, union-merge and the
        2P-Set property would go with it."""
        mine, theirs = a_store(), a_store()
        writer.put(mine, an_assertion(akey="other", body=b"filler"))
        a = writer.put(mine, an_assertion())
        b = writer.put(theirs, an_assertion())
        self.assertEqual(a.id, b.id)


class MalformedTime(StateTest):
    """C-3. A record wrong in the direction of confidence is worse than a write
    that fails."""

    def test_C3_an_unparseable_timestamp_is_refused_at_write_time(self):
        with self.assertRaises(BadTimestamp):
            writer.put(a_store(), an_assertion(ts="next Tuesday"))

    def test_C3_a_non_utc_timestamp_is_refused(self):
        """Rank is lexical, so a local-offset timestamp sorts by its digits
        rather than by its instant."""
        with self.assertRaises(BadTimestamp):
            writer.put(a_store(), an_assertion(ts="2026-09-04T12:00:00-06:00"))

    def test_C3_a_future_timestamp_is_bounded_against_the_receiving_clock(self):
        """THE F-4 FIX. ts is author-chosen; without a bound an attacker sets it
        far in the future and owns an akey forever -- and no supersession is
        involved, so R-4's authority check never runs."""
        with self.assertRaises(BadTimestamp):
            ids.check_timestamp("2099-01-01T00:00:00Z",
                                received_at="2026-09-04T12:00:00Z")

    def test_C3_a_plausible_timestamp_passes_the_bound(self):
        """Control: a bound that refuses everything is not a bound. Clocks do
        disagree by minutes and that must remain workable."""
        self.assertIsNone(ids.check_timestamp("2026-09-04T12:01:00Z",
                                              received_at="2026-09-04T12:00:00Z"))


class DeclaredRank(StateTest):
    """C-4. Rank is declared per kind, not implicitly the timestamp."""

    def test_C4_a_kind_with_a_counter_beats_clock_skew(self):
        """The point of letting a kind declare its own rank: with a logical
        counter, skew moves timing rather than outcome. `CountedKind` is
        registered on the store, so this exercises a wired extension point --
        v1's KindSpec was referenced by zero modules (FINDINGS.md C-8)."""
        store = a_store()
        writer.put(store, an_assertion(kind="counted", akey="c", body=b"seq=2",
                                       rank=pack_rank(counter=2),
                                       ts="2026-09-04T09:00:00Z"))
        writer.put(store, an_assertion(kind="counted", akey="c", body=b"seq=1",
                                       rank=pack_rank(counter=1),
                                       ts="2026-09-04T12:00:00Z"))
        self.assertEqual(reader.resolve(store, "c")[0].body, b"seq=2")
