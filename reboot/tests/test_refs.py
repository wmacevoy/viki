"""X -- references and the cache. None of this existed in v1.

U-4: mail and documents are too large to carry and may be deleted, revoked or
edited at the source, so a diary holds references plus a bounded cache.
"""

from refcore import reader, refs, writer
from refcore.errors import Gone, Unrebuildable
from refcore.model import RefState
from tests.support import (ALICE, FakeFetcher, StateTest, a_ref, a_store,
                           an_assertion)


class ThreeStates(StateTest):
    """X-2, X-3. The pair that matters is AWAY and GONE."""

    def test_X2_cached_content_reports_cached(self):
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher({("gmail", "msg-1"): b"body"}))
        refs.cache_put(store, ref, b"body")
        self.assertIs(refs.state(store, ref), RefState.CACHED)

    def test_X2_an_uncached_referent_reports_away(self):
        store = a_store(fetcher=FakeFetcher())
        self.assertIs(refs.state(store, a_ref()), RefState.AWAY)

    def test_X2_a_deleted_referent_reports_gone(self):
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher(gone=[("gmail", "msg-1")]))
        refs.mark_gone(store, ref)
        self.assertIs(refs.state(store, ref), RefState.GONE)

    def test_X3_an_empty_cache_is_not_an_emptied_mailbox(self):
        """THE DISTINCTION. A phone with no cache must not read as a source that
        was purged -- and an adapter returning None for a deleted message makes
        a purge look like a network hiccup forever."""
        ref = a_ref()
        phone = a_store(fetcher=None)                 # offline, nothing cached
        purged = a_store(fetcher=FakeFetcher(gone=[("gmail", "msg-1")]))
        refs.mark_gone(purged, ref)
        self.assertIsNot(refs.state(phone, ref), refs.state(purged, ref))

    def test_X2_content_raises_gone_rather_than_returning_nothing(self):
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher(gone=[("gmail", "msg-1")]))
        with self.assertRaises(Gone):
            refs.content(store, ref)


class CacheIsAProjection(StateTest):
    """X-4."""

    def test_X4_the_cache_is_not_merged(self):
        """Each device caches independently, which is what lets the phone hold
        every assertion and almost no content."""
        from refcore import merger
        ref = a_ref()
        laptop = a_store(diary="assistant")
        phone = a_store(diary="assistant")
        writer.put(laptop, an_assertion(reference=ref))
        refs.cache_put(laptop, ref, b"a large message body")
        merger.merge(phone, laptop)
        self.assertIs(refs.state(phone, ref), RefState.AWAY)

    def test_X8_a_diary_is_usable_with_an_empty_cache(self):
        """Degraded operation is a required path. The read succeeds and reports
        what it could not show."""
        ref = a_ref()
        store = a_store(fetcher=None)
        writer.put(store, an_assertion(reference=ref, body=b"summary"))
        result = reader.log(store, "k")
        self.assertEqual(len(result.rows), 1)
        self.assertEqual(result.uncached, 1)

    def test_X8_a_full_cache_reports_nothing_uncached(self):
        """Control: a counter that always reports one is not a counter."""
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher({("gmail", "msg-1"): b"body"}))
        writer.put(store, an_assertion(reference=ref))
        refs.cache_put(store, ref, b"body")
        self.assertEqual(reader.log(store, "k").uncached, 0)


class Eviction(StateTest):
    """X-5, X-6. The category v1 did not have.

    "Derived, rebuildable, disposable" was true of chunks, which regenerate from
    stored text. A cache of an email does not regenerate once the message is
    deleted.
    """

    def test_X5_evicting_recoverable_content_is_allowed(self):
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher({("gmail", "msg-1"): b"body"}))
        refs.cache_put(store, ref, b"body")
        self.assertEqual(refs.evict(store, budget=0), 1)

    def test_X5_evicting_the_last_copy_of_gone_content_refuses(self):
        """The caller is destroying something and should have to know."""
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher(gone=[("gmail", "msg-1")]))
        refs.cache_put(store, ref, b"the only copy")
        refs.mark_gone(store, ref)
        with self.assertRaises(Unrebuildable):
            refs.evict(store, budget=0)

    def test_X6_pinned_content_is_not_evicted(self):
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher({("gmail", "msg-1"): b"body"}))
        refs.cache_put(store, ref, b"body")
        refs.pin(store, ref)
        self.assertEqual(refs.evict(store, budget=0), 0)

    def test_X6_pinning_makes_gone_content_safe_to_keep(self):
        """Pinning is the deliberate act of deciding to carry something, and it
        is the answer to X-5 rather than a workaround for it."""
        ref = a_ref()
        store = a_store(fetcher=FakeFetcher(gone=[("gmail", "msg-1")]))
        refs.cache_put(store, ref, b"the only copy")
        refs.pin(store, ref)
        refs.mark_gone(store, ref)
        self.assertEqual(refs.evict(store, budget=0), 0)
        self.assertEqual(refs.content(store, ref), b"the only copy")


class Versions(StateTest):
    """X-7. Without this a summary silently describes a document that no longer
    says that."""

    def test_X7_a_changed_source_is_detectable(self):
        ref = a_ref(version="v1")
        store = a_store(fetcher=FakeFetcher(versions={("gmail", "msg-1"): "v2"}))
        writer.put(store, an_assertion(reference=ref))
        self.assertTrue(refs.stale(store, ref))

    def test_X7_an_unchanged_source_is_not_stale(self):
        """Control."""
        ref = a_ref(version="v1")
        store = a_store(fetcher=FakeFetcher(versions={("gmail", "msg-1"): "v1"}))
        self.assertFalse(refs.stale(store, ref))

    def test_X7_a_system_with_no_version_reports_none(self):
        """Reporting the absence beats inventing a value. Open question 4 asks
        whether Gmail, Graph and Drive all offer one; until that is probed, this
        is the honest answer."""
        store = a_store(fetcher=FakeFetcher(versions={}))
        self.assertIsNone(refs.stale(store, a_ref(version="")))
