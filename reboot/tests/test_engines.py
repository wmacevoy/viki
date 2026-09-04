"""G -- engines, and the A-2a/A-4a rules that make heterogeneity possible.

A peer may be SQLite or PostgreSQL. They are peers, not tiers: they reconcile
through the S protocol, which speaks ids and digests, so neither needs to know
the other's engine.

These tests are the ones that would have caught the leaks in FINDINGS.md T-12 --
and note that none of them needs Postgres to run, which is the point. A
portability rule you can only test by deploying is a rule nobody tests.
"""

from refcore import ids, merger, reader, refs, schema, sync, writer
from refcore.errors import Refused
from refcore.model import SyncPolicy
from tests.support import (ALICE, BOB, StateTest, a_ref, a_store, an_assertion,
                           pack_rank)


class RankWidth(StateTest):
    """A-2a, C-4. A width, not a convention."""

    def test_A2a_a_rank_of_the_wrong_width_is_refused(self):
        with self.assertRaises(Refused):
            ids.check_rank(b"short")

    def test_A2a_a_sixteen_byte_rank_is_accepted(self):
        """Control: a checker that refuses everything passes the test above."""
        self.assertIsNone(ids.check_rank(pack_rank(counter=1)))

    def test_A2a_a_bad_width_never_reaches_the_store(self):
        with self.assertRaises(Refused):
            writer.put(a_store(), an_assertion(rank=b"\x00" * 8))

    def test_A2b_the_encoding_orders_by_time_then_counter(self):
        """The encoding is stated, not left to the caller -- otherwise C-2's
        original defect comes back one level up, with two hosts producing
        different ranks for the same logical content.

        Asserted THROUGH the store, not over the fixture. An earlier version of
        this test compared two `pack_rank` calls and passed while asserting
        nothing about any implementation -- which check.py caught, and which is
        the same vacuity FINDINGS.md C-7 recorded twice before.
        """
        store = a_store()
        writer.put(store, an_assertion(body=b"same-ms-low",
                                       rank=pack_rank(ms=5, counter=0)))
        writer.put(store, an_assertion(body=b"same-ms-high",
                                       rank=pack_rank(ms=5, counter=1)))
        self.assertEqual(reader.resolve(store, "k")[0].body, b"same-ms-high")

    def test_A2b_the_millisecond_field_outranks_the_counter(self):
        """Control on the field order: a later ms wins even with a lower
        counter, so the six leading bytes really are the time."""
        store = a_store()
        writer.put(store, an_assertion(body=b"early-high-counter",
                                       rank=pack_rank(ms=5, counter=9)))
        writer.put(store, an_assertion(body=b"late-low-counter",
                                       rank=pack_rank(ms=6, counter=0)))
        self.assertEqual(reader.resolve(store, "k")[0].body, b"late-low-counter")


class EverythingIsBytes(StateTest):
    """A-4a. One rule, four leaks closed."""

    def test_A4a_normalization_returns_bytes(self):
        """The return type IS the requirement: nothing downstream can hash a
        str under one engine's encoding assumptions."""
        self.assertIsInstance(ids.normalize("café"), bytes)

    def test_A4a_no_truth_table_declares_a_text_column(self):
        """Structural, because 'remember to use BLOB' is the kind of rule that
        holds until the next column is added. SQLite sorts every BLOB after
        every TEXT regardless of content, so a mixed store orders
        catastrophically wrong rather than subtly (FINDINGS.md T-3)."""
        truth = ("assertion", "tombstone", "grant", "signature", "derived",
                 "heap", "publication")
        for table in truth:
            start = schema.DDL.index("CREATE TABLE IF NOT EXISTS " + table + " (")
            body = schema.DDL[start:schema.DDL.index(")", start)]
            self.assertNotIn("TEXT", body, table)
        self.assertTrue(writer.put(a_store(), an_assertion()).stored)

    def test_A4a_a_body_containing_nul_survives_a_round_trip(self):
        """Postgres `text` rejects NUL outright and SQLite truncates at it, so
        the same input stores differently on the two engines and union breaks on
        one side only. `bytea`/BLOB accepts it."""
        store = a_store()
        put = writer.put(store, an_assertion(body=b"before\x00after"))
        self.assertEqual(reader.get(store, put.id).body, b"before\x00after")

    def test_A4a_a_body_of_invalid_utf8_survives_a_round_trip(self):
        """Same argument, other half: PG `text` rejects invalid UTF-8."""
        store = a_store()
        put = writer.put(store, an_assertion(body=b"\xff\xfe not utf-8"))
        self.assertEqual(reader.get(store, put.id).body, b"\xff\xfe not utf-8")


class ByteOrderResolution(StateTest):
    """R-2 under A-4a. The property test that needs no locale knowledge and no
    second engine."""

    def test_R2_resolution_matches_sorting_the_arms_by_raw_bytes(self):
        """`core/`'s apparent tiebreak today is a query-plan artifact -- drop an
        index and the winner changes. This asserts the contract instead of the
        accident."""
        store = a_store()
        same = pack_rank(ms=1)
        arms = [writer.put(store, an_assertion(body=b"a", rank=same)),
                writer.put(store, an_assertion(body=b"b", rank=same)),
                writer.put(store, an_assertion(body=b"c", rank=same))]
        expected = max(arms, key=lambda o: o.id.encode())
        self.assertEqual(reader.resolve(store, "k")[0].id, expected.id)

    def test_R2_rank_still_beats_the_id_tiebreak(self):
        """Control: an implementation ordering by id alone passes the test above
        and fails this one."""
        store = a_store()
        writer.put(store, an_assertion(body=b"older", rank=pack_rank(ms=1)))
        writer.put(store, an_assertion(body=b"newer", rank=pack_rank(ms=2)))
        self.assertEqual(reader.resolve(store, "k")[0].body, b"newer")


class WireFormat(StateTest):
    """G-1. The predecessor's wire format WAS the SQLite file format, so no
    other engine could participate at any layer."""

    def test_G1_sync_is_defined_over_ids_and_digests(self):
        """A digest is a hash over content, so it is engine-independent by
        construction and a Postgres peer can compute the same one."""
        laptop, hub = a_store(diary="assistant"), a_store(diary="assistant")
        writer.put(laptop, an_assertion())
        d = sync.digest(laptop)
        self.assertEqual(d.diary, "assistant")
        self.assertIsInstance(sync.lacking(hub, d), tuple)

    def test_G2_identity_is_computed_without_a_database(self):
        """Identity built by an engine's SQL functions is the top portability
        leak: PG's json builder reorders keys, so the same logical content
        yields a different sha256. `compute_id` touches no connection."""
        self.assertEqual(ids.compute_id(an_assertion()),
                         ids.compute_id(an_assertion()))


class ProjectionsMayDiffer(StateTest):
    """G-3. The single decision that makes a heterogeneous fleet survivable,
    stated explicitly rather than left implied."""

    def test_G3_peers_converge_on_truth_with_different_projection_state(self):
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        ref = a_ref()
        writer.put(laptop, an_assertion(reference=ref))
        refs.cache_put(laptop, ref, b"a large body")
        merger.merge(phone, laptop)
        self.assertEqual(sync.digest(phone).root, sync.digest(laptop).root)

    def test_G4_local_tables_do_not_travel(self):
        """`arrival`, `peer` and `verified` are free to differ because nothing
        in them travels -- which is also what licenses different sequence
        semantics per engine."""
        laptop, phone = a_store(diary="assistant"), a_store(diary="assistant")
        writer.put(laptop, an_assertion())
        sync.push(laptop, phone)
        self.assertEqual(sync.watermark(phone, ALICE), 0)


class SequenceSemantics(StateTest):
    """G-5. Postgres sequences allocate before commit and are not gap-free, so
    `max(seq)` can name a row that has not landed -- S-6's dangerous direction
    arriving from the engine rather than from a caller."""

    def test_G5_a_watermark_advances_on_acknowledged_rows_not_a_sequence_max(self):
        laptop = a_store(principal=ALICE, diary="assistant")
        phone = a_store(principal=BOB, diary="assistant")
        for i in range(5):
            writer.put(laptop, an_assertion(akey="k%d" % i, body=b"r%d" % i))
        partial = sync.push(laptop, phone, budget=2)
        self.assertEqual(partial.transferred, 2)
        self.assertLess(sync.watermark(laptop, BOB), 5)


class ConcurrentMerge(StateTest):
    """G-6. SQLite's single writer supplies atomicity for free; under MVCC two
    merges interleave and a sweep can judge authority that has not committed."""

    def test_G6_the_merge_sequence_is_atomic(self):
        """Two sources delivering a tombstone and its target separately must
        reach the same state whichever lands first -- which is only true if each
        merge's sweep sees a consistent snapshot."""
        from refcore import withdrawal
        from refcore.model import Reason
        outcomes = set()
        for order in ((0, 1), (1, 0)):
            mine = a_store(principal=ALICE, diary="assistant")
            srcs = [a_store(principal=ALICE, diary="assistant"),
                    a_store(principal=ALICE, diary="assistant")]
            target = writer.put(srcs[0], an_assertion(author=ALICE))
            withdrawal.erase(srcs[1], target.id, Reason.SECRET)
            for i in order:
                merger.merge(mine, srcs[i])
            outcomes.add(withdrawal.residue(mine, target.id))
        self.assertEqual(len(outcomes), 1)
