"""D -- diary scope and access. The primary security mechanism.

v1 made per-assertion permissions the boundary. Both reviews showed a filter
inside the process cannot confine anything holding the bytes, and neither use
case asks for it: the sensitive diaries do not sync, and the ones that do are
replicated inside one trust domain.
"""

from refcore import diary, merger, reader, withdrawal, writer
from refcore.errors import NoSyncPath, Refused, Unauthorized
from refcore.model import Grant, Reason, SyncPolicy, R, S, X
from tests.support import (AGENT, ALICE, BOB, StateTest, a_store, an_assertion)


class Policy(StateTest):
    """D-1, D-2."""

    def test_D1_a_diary_states_its_policy(self):
        store = a_store(policy=SyncPolicy.NEVER)
        self.assertIs(diary.policy_of(store, "personal"), SyncPolicy.NEVER)

    def test_D2_a_never_diary_refuses_to_be_a_merge_source(self):
        """U-1: the Gmail and Outlook diaries hold a great deal that must never
        sync anywhere. Not a flag and not an override."""
        source = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        dest = a_store(diary="gmail", policy=SyncPolicy.DOMAIN)
        with self.assertRaises(NoSyncPath):
            merger.merge(dest, source)

    def test_D2_a_never_diary_refuses_to_be_a_push_destination(self):
        source = a_store(diary="gmail", policy=SyncPolicy.DOMAIN)
        dest = a_store(diary="gmail", policy=SyncPolicy.NEVER)
        with self.assertRaises(NoSyncPath):
            merger.push(source, dest)

    def test_D1_a_domain_diary_syncs(self):
        """Control. U-3: the assistant diary syncs between phone and laptop and
        holds sensitive content -- syncing is not publishing."""
        mine = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        theirs = a_store(diary="assistant", policy=SyncPolicy.DOMAIN)
        writer.put(theirs, an_assertion())
        self.assertTrue(merger.merge(mine, theirs).complete)

    def test_D6_a_store_reports_where_it_sends_its_contents(self):
        """A store that cannot say this is not usable."""
        described = diary.describe(a_store(policy=SyncPolicy.DOMAIN))
        self.assertIn("policy", described)
        self.assertIn("domain", described)


class Rights(StateTest):
    """D-3. Per principal, per diary. Three bits, no per-assertion control."""

    def test_D3_a_principal_without_r_cannot_read(self):
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        blind = a_store(principal=BOB, grants=())
        merger.merge(blind, store)
        with self.assertRaises(Unauthorized):
            reader.get(blind, put.id)

    def test_D3_a_principal_with_r_can_read(self):
        """Control, and D-3 in full: within a diary everyone with `r` reads
        EVERYTHING. There is no per-assertion exception."""
        store = a_store(principal=ALICE)
        put = writer.put(store, an_assertion(author=ALICE))
        member = a_store(principal=BOB,
                         grants=(Grant(principal=BOB, diary="personal", rights=R),))
        merger.merge(member, store)
        self.assertIsNotNone(reader.get(member, put.id))

    def test_D3_x_is_required_to_erase(self):
        store = a_store(principal=BOB,
                        grants=(Grant(principal=BOB, diary="personal", rights=R | S),))
        put = writer.put(store, an_assertion(author=BOB))
        with self.assertRaises(Unauthorized):
            withdrawal.erase(store, put.id, Reason.POLICY)

    def test_D3c_s_without_x_can_supersede_but_not_remove(self):
        """The filter-agent case. `s` is safe to grant because it destroys
        nothing: the predecessor stays in the log view."""
        store = a_store(principal=AGENT,
                        grants=(Grant(principal=AGENT, diary="personal", rights=S),))
        first = writer.put(store, an_assertion(author=AGENT, body=b"flagged"))
        writer.supersede(store, first.id,
                         an_assertion(author=AGENT, body=b"safe",
                                      supersedes=(first.id,)))
        self.assertEqual(len(reader.log(store, "k").rows), 2)

    def test_D3a_an_agent_with_s_and_no_r_cannot_read(self):
        """THE CONFINEMENT CASE. Every grant is stated as the capability of an
        agent that has already been taken over: a compromised filter holding `s`
        alone still cannot read anything."""
        owner = a_store(principal=ALICE)
        put = writer.put(owner, an_assertion(author=ALICE, body=b"secret"))
        agent = a_store(principal=AGENT,
                        grants=(Grant(principal=AGENT, diary="personal", rights=S),))
        merger.merge(agent, owner)
        with self.assertRaises(Unauthorized):
            reader.get(agent, put.id)


class Grants(StateTest):
    """D-5, I-4. From signed grants, not an unsigned constructor argument.

    v1's positive control passed membership in as a tuple, so the host asserted
    membership and the state layer trusted it -- exactly the unsigned field I-4
    forbids (FINDINGS.md F-9).
    """

    def test_D5_a_grant_is_traceable_to_a_signer(self):
        store = a_store(principal=ALICE)
        g = diary.grant(store, AGENT, "personal", S)
        self.assertEqual(reader.sig_states(store, g).get(ALICE).value, "verified")

    def test_D5_revoking_is_a_superseding_grant(self):
        store = a_store(principal=ALICE)
        diary.grant(store, AGENT, "personal", R | S)
        diary.grant(store, AGENT, "personal", 0)
        self.assertEqual(diary.rights(store, AGENT, "personal"), 0)

    def test_I4_an_unsigned_grant_confers_nothing(self):
        store = a_store(principal=ALICE, signer=None)
        diary.grant(store, AGENT, "personal", R)
        self.assertEqual(diary.rights(store, AGENT, "personal"), 0)


class Vocabulary(StateTest):
    """D-3b, D-3e."""

    def test_D3b_a_writable_diary_states_its_vocabulary(self):
        """Free text is unbounded bandwidth; an enum and a score are close to
        none. The residual covert channel is this vocabulary's entropy times the
        number of items processed."""
        store = a_store(diary="filter-status")
        self.assertNotEqual(diary.vocabulary(store, "filter-status"), ())

    def test_D3e_an_unerasable_diary_must_bound_its_vocabulary(self):
        """An append-only diary of free text is a store you are legally unable
        to clean. The bound and the immutability are one decision, not two, so
        declaring a diary nobody holds `x` on without a vocabulary is refused."""
        store = a_store(diary="audit", grants=(
            Grant(principal=ALICE, diary="audit", rights=R | S),))
        with self.assertRaises(Refused):
            diary.declare(store, "audit", SyncPolicy.DOMAIN)

    def test_D3e_a_bounded_unerasable_diary_is_allowed(self):
        """Control: an audit log SHOULD be un-erasable. The refusal above is
        about the vocabulary, not about immutability."""
        store = a_store(diary="audit", grants=(
            Grant(principal=ALICE, diary="audit", rights=R | S),))
        self.assertIsNone(
            diary.declare(store, "audit", SyncPolicy.DOMAIN, endpoint="enum:ok,spam"))
