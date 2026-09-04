"""V -- derivation and publication. The security boundary.

Both use cases are one shape: a sensitive store that never moves, and a smaller
derived artifact that does. What crosses is the only thing anything downstream
can ever see.
"""

from refcore import derive, refs, withdrawal, writer
from refcore.errors import NotPublication
from refcore.model import Reason, SyncPolicy
from tests.support import (AGENT, ALICE, StateTest, a_ref, a_store,
                           an_assertion)


class Edges(StateTest):
    """V-1. Queryable both ways."""

    def test_V1_a_derived_assertion_names_its_sources(self):
        store = a_store()
        source = writer.put(store, an_assertion(reference=a_ref()))
        summary = writer.put(store, an_assertion(
            akey="summary", body=b"one thing happened",
            derived_from=(source.id,)))
        self.assertEqual(derive.sources_of(store, summary.id), (source.id,))

    def test_V1_the_reverse_edge_finds_derivatives(self):
        """This is how erasing a source reaches the summary computed from it --
        a content-addressed tombstone cannot, because the derivative holds
        different bytes (W-12)."""
        store = a_store()
        source = writer.put(store, an_assertion(reference=a_ref()))
        summary = writer.put(store, an_assertion(
            akey="summary", body=b"one thing", derived_from=(source.id,)))
        self.assertEqual(derive.derivatives_of(store, source.id), (summary.id,))

    def test_V1_an_underived_assertion_has_no_sources(self):
        """Control."""
        store = a_store()
        put = writer.put(store, an_assertion())
        self.assertEqual(derive.sources_of(store, put.id), ())


class Publication(StateTest):
    """V-2, V-3, V-4."""

    def test_V4_crossing_from_a_never_diary_by_ordinary_write_is_refused(self):
        """Nothing crosses by accident. U-1: the Gmail diary must never sync."""
        gmail = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        assistant = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        source = writer.put(gmail, an_assertion(reference=a_ref()))
        with self.assertRaises(NotPublication):
            writer.put(assistant, an_assertion(
                akey="summary", body=b"derived", derived_from=(source.id,)))

    def test_V2_publication_is_explicit_and_succeeds(self):
        """Control: the boundary must be crossable, or the product does not
        work. It just has to be crossed on purpose."""
        gmail = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        assistant = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        summary = writer.put(gmail, an_assertion(akey="summary", body=b"derived"))
        record = derive.publish(assistant, assistant, (summary.id,))
        self.assertEqual(record.assertions, (summary.id,))

    def test_V3_a_publication_records_what_crossed(self):
        """THE SECURITY BOUNDARY IS THE REVIEWABLE RECORD. V-6: the blast radius
        of a prompt injection is what reached a syncing diary, and this makes it
        enumerable rather than argued about."""
        gmail = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        assistant = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        summary = writer.put(gmail, an_assertion(akey="summary", body=b"derived"))
        derive.publish(assistant, assistant, (summary.id,))
        self.assertEqual(len(derive.published(assistant)), 1)

    def test_V3_nothing_published_reports_nothing(self):
        """Control: a record that always shows one publication is not a record."""
        self.assertEqual(derive.published(a_store()), ())

    def test_V6_an_agents_output_is_bounded_by_the_record(self):
        """The confinement claim, made checkable: everything an agent got out is
        in the publication record, so the exposure is a list rather than an
        argument."""
        assistant = a_store(principal=AGENT, diary="assistant",
                            policy=SyncPolicy.DOMAIN)
        a = writer.put(assistant, an_assertion(author=AGENT, akey="s1", body=b"x"))
        b = writer.put(assistant, an_assertion(author=AGENT, akey="s2", body=b"y"))
        derive.publish(assistant, assistant, (a.id,))
        published = {i for rec in derive.published(assistant) for i in rec.assertions}
        self.assertEqual(published, {a.id})
        self.assertNotIn(b.id, published)
