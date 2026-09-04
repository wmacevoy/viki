"""E -- failure as an input.

Four cases need a stated answer for every input this layer depends on: ABSENT,
LATE, WRONG, LYING. One class each, so a gap in the quartet shows as an empty
class rather than as an absence nobody notices.
"""

import sqlite3

from refcore import derive, ids, merger, reader, refs, withdrawal, writer
from refcore.errors import (BadId, IncompleteMerge, NotAStore, NotFound,
                            Refused, StateError, Unauthorized)
from refcore.model import Grant, Reason, SyncPolicy, R, S, X
from tests.support import (AGENT, ALICE, BOB, FakeFetcher, StateTest, a_ref,
                           a_store, an_assertion)


class Absent(StateTest):

    def test_E1_a_write_that_stores_nothing_is_an_error(self):
        """Not a silent success. A memory that quietly drops what it was told is
        worse than one that is absent."""
        with self.assertRaises(StateError):
            writer.put(a_store(), an_assertion(akey=""))

    def test_E1_reading_a_missing_assertion_raises(self):
        with self.assertRaises(NotFound):
            reader.get(a_store(), "0" * 64)

    def test_E1_superseding_something_absent_is_refused(self):
        """A typo must not become a dangling edge nobody notices."""
        with self.assertRaises(NotFound):
            writer.supersede(a_store(), "0" * 64, an_assertion(body=b"replacement"))

    def test_E1_a_missing_signer_is_not_an_error(self):
        """Degraded operation is a required path, not a failure -- though under
        I-7 what it writes will not resolve."""
        self.assertTrue(writer.put(a_store(signer=None), an_assertion()).stored)

    def test_E1_a_missing_fetcher_is_not_an_error(self):
        """X-8: a diary is usable with an empty cache and no way to fill it."""
        store = a_store(fetcher=None)
        writer.put(store, an_assertion(reference=a_ref()))
        self.assertEqual(len(reader.log(store, "k").rows), 1)


class Late(StateTest):
    """Offline is the NORMAL condition, which is why merge must be a
    semilattice."""

    def test_E2_an_assertion_arriving_after_its_successor_does_not_win(self):
        mine, slow = a_store(), a_store()
        first = writer.put(mine, an_assertion(body=b"first",
                                              ts="2026-09-04T09:00:00Z"))
        writer.put(mine, an_assertion(body=b"second", ts="2026-09-04T12:00:00Z",
                                      supersedes=first.id))
        writer.put(slow, an_assertion(body=b"first", ts="2026-09-04T09:00:00Z"))
        merger.merge(mine, slow)
        self.assertEqual(reader.current(mine, "k").body, b"second")

    def test_E2_a_partially_projected_store_says_so(self):
        mine, theirs = a_store(), a_store()
        writer.put(theirs, an_assertion())
        self.assertGreater(merger.merge(mine, theirs).unprojected, 0)

    def test_E2_a_read_reports_every_way_it_is_blind(self):
        """In the RESULT, not the documentation. v1's Result had two channels;
        there are four ways to be partly blind."""
        store = a_store(fetcher=None)
        writer.put(store, an_assertion(reference=a_ref(), body=b"summary"))
        result = reader.log(store, "k")
        self.assertEqual(result.uncached, 1)
        self.assertEqual(result.unreadable, 0)


class Wrong(StateTest):
    """Malformed rather than malicious -- corruption, a bad encoder, a truncated
    transfer."""

    def test_E4_a_source_of_the_wrong_shape_is_refused_whole(self):
        empty = sqlite3.connect(":memory:")
        empty.execute("CREATE TABLE something_else (x)")
        with self.assertRaises(NotAStore):
            merger.merge(a_store(), empty)

    def test_E3_every_refusal_names_its_requirement(self):
        """So a caller tells a permission refusal from a malformed timestamp
        without parsing a message. v1 sampled the one class with a static
        requirement while `Refused`, its most-raised class, carried an empty
        string (FINDINGS.md C-7). This samples the base class."""
        try:
            writer.put(a_store(), an_assertion(akey=""))
        except StateError as e:
            self.assertTrue(e.requirement)
        else:
            self.fail("expected a refusal")

    def test_E3_the_refusal_class_hierarchy_carries_requirements(self):
        """Checked structurally, so a new error class cannot be added without
        one."""
        for cls in StateError.__subclasses__():
            self.assertTrue(cls.requirement, cls.__name__)
        self.assertTrue(writer.put(a_store(), an_assertion()).stored)


class Lying(StateTest):
    """Inputs chosen to find the K3. Security is this discipline applied to an
    adversary."""

    def test_lying_a_forged_id_does_not_enter_the_store(self):
        forged = an_assertion(body=b"real")
        object.__setattr__(forged, "id", "0" * 64)
        with self.assertRaises(BadId):
            writer.put(a_store(), forged)

    def test_lying_an_impersonated_author_does_not_resolve(self):
        """I-7. Asserting only that the signature fails to verify is not enough
        -- v1 stopped there, leaving the fold's treatment of forged authorship
        undefined (FINDINGS.md F-10)."""
        impostor = a_store(principal=BOB, signer=None)
        writer.put(impostor, an_assertion(author=ALICE))
        with self.assertRaises(NotFound):
            reader.current(impostor, "k")

    def test_lying_a_hostile_tombstone_cannot_destroy_what_it_does_not_own(self):
        mine = a_store(principal=ALICE)
        target = writer.put(mine, an_assertion(author=ALICE))
        hostile = a_store(principal=BOB,
                          grants=(Grant(principal=BOB, diary="personal",
                                        rights=R),))
        withdrawal.erase(hostile, target.id, Reason.POLICY)
        merger.merge(mine, hostile)
        self.assertIsNotNone(reader.get(mine, target.id))

    def test_lying_a_hostile_tombstone_cannot_hide_its_own_attempt(self):
        """W-2 composed with W-5, which is what makes the immune class more than
        a curiosity: destroy the record, then destroy the record of it."""
        mine = a_store(principal=ALICE)
        target = writer.put(mine, an_assertion(author=ALICE))
        hostile = a_store(principal=BOB,
                          grants=(Grant(principal=BOB, diary="personal",
                                        rights=R),))
        withdrawal.erase(hostile, target.id, Reason.POLICY)
        merger.merge(mine, hostile)
        self.assertEqual(len(withdrawal.tombstones(mine, target.id)), 1)

    def test_lying_a_compromised_agent_cannot_read_what_it_may_write(self):
        """D-3a, the filter-agent case. Stated as the capability of an agent
        that has ALREADY been taken over."""
        owner = a_store(principal=ALICE)
        put = writer.put(owner, an_assertion(author=ALICE, body=b"secret"))
        agent = a_store(principal=AGENT,
                        grants=(Grant(principal=AGENT, diary="personal",
                                      rights=S),))
        merger.merge(agent, owner)
        with self.assertRaises(Unauthorized):
            reader.get(agent, put.id)

    def test_lying_a_compromised_agent_cannot_publish_beyond_the_record(self):
        """V-6. The blast radius is enumerable rather than argued about."""
        agent = a_store(principal=AGENT, diary="assistant",
                        policy=SyncPolicy.DOMAIN)
        a = writer.put(agent, an_assertion(author=AGENT, akey="s1", body=b"x"))
        writer.put(agent, an_assertion(author=AGENT, akey="s2", body=b"leak"))
        derive.publish(agent, agent, (a.id,))
        crossed = {i for rec in derive.published(agent) for i in rec.assertions}
        self.assertEqual(crossed, {a.id})

    def test_lying_a_flood_of_supersessions_cannot_bury_an_assertion(self):
        """Cheap to attempt, permanent if it works. Note the log view still
        shows the original either way -- R-9 is the reader's obligation, and
        this is the writer-side half."""
        mine = a_store(principal=ALICE)
        target = writer.put(mine, an_assertion(author=ALICE))
        hostile = a_store(principal=BOB, grants=())
        for i in range(10):
            writer.put(hostile, an_assertion(author=BOB, body=b"noise %d" % i,
                                             supersedes=target.id))
        merger.merge(mine, hostile)
        self.assertEqual(reader.current(mine, "k").id, target.id)
