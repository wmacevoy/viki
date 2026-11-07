"""Tests for stripped.py. Requirements STRIP1/1..6 are in README.md, all
(inferred) from the 2026-11-07 request. Run:

    python3 -m unittest discover -s test/stripped -v
"""
import json, os, re, sys, tempfile, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stripped as S

# A small FINDINGS-shaped file with every marker kind; also the pilot's pile.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixture-findings.md")) as _f:
    FIXTURE = _f.read()

def build_fixture(seed=7):
    return S.build(FIXTURE, seed, "FIX.md")

def by_role(key, role):
    return [fid for fid, m in key["fragments"].items() if m["role"] == role]

def heading_of(key, fid):
    return key["fragments"][fid].get("heading", "")


class Strip1_1_NoMarkers(unittest.TestCase):
    """STRIP1/1 (inferred): the corpus carries none of the declared structure
    -- no correction labels, no strikethrough, no heading-quoting pointer,
    no preamble -- and the text those markers carried is kept, not lost."""

    def test_markers_absent_and_text_kept(self):
        frags, key = build_fixture()
        allt = "\n".join(frags.values())
        self.assertNotRegex(allt, r"\*\*(Correction|Superseded)")
        self.assertNotIn("~~", allt)
        self.assertNotRegex(allt, r"[Tt]he entry (below|above) \(\"Beta")
        self.assertNotIn("Preamble describing", allt)
        self.assertNotRegex(allt, r"(?m)^> ")
        # Kept: the corrected claim, the retracted sentence, the pointer's claim.
        self.assertIn("that count is stale", allt)
        self.assertIn("Until someone rebuilds, the binary has the bug.", allt)
        self.assertIn("That judgement is wrong", allt)

    def test_real_findings(self):
        with open(S.DEFAULT_FINDINGS) as f:
            frags, _ = S.build(f.read(), 1)
        allt = "\n".join(frags.values())
        self.assertNotRegex(allt, r"(?m)^> \*\*(Correction|Superseded)")
        self.assertNotIn("~~", allt)
        self.assertIsNone(S.POINTER.search(allt))


class Strip1_2_KeyFromDeclarations(unittest.TestCase):
    """STRIP1/2 (inferred): every relation FINDINGS declares reaches the key,
    with the line that declared it; a citation is not a correction."""

    def test_fixture_relations(self):
        _, key = build_fixture()
        corr = [(r["a"], r["b"], r["evidence"]) for r in key["relations"]
                if r["kind"] == "corrects"]
        beta = [f for f in key["fragments"] if heading_of(key, f).startswith("Beta")][0]
        alpha = [f for f in key["fragments"] if heading_of(key, f).startswith("Alpha")][0]
        # One block, one strikethrough, one pointer whose paragraph says "wrong".
        self.assertEqual(len(corr), 3)
        self.assertTrue(all(b == beta for _, b, _ in corr))
        self.assertIn((alpha, beta), [(a, b) for a, b, _ in corr])
        self.assertTrue(all(re.match(r"FIX\.md:\d+ ", e) for _, _, e in corr))

    def test_real_findings_cross_entry_pointers(self):
        with open(S.DEFAULT_FINDINGS) as f:
            _, key = S.build(f.read(), 1)
        pairs = {(heading_of(key, r["a"])[:30], heading_of(key, r["b"])[:30])
                 for r in key["relations"] if r["evidence"].endswith("heading pointer")}
        # Both resolve only if the matcher handles a wrapped quote and a
        # trailing "..., item 4" locator.
        self.assertIn(("A cache holding two `model_id`", "`viki ask`'s \"hybrid mode\" ban"), pairs)
        self.assertIn(("A real forum post CAN be creat", "Forum posts: no `fossil forum`"), pairs)
        blocks = [r for r in key["relations"] if "labelled block" in r["evidence"]]
        self.assertGreaterEqual(len(blocks), 4)


class Strip1_3_Deterministic(unittest.TestCase):
    """STRIP1/3 (inferred): the same seed and source give byte-identical
    corpus and key; another seed gives other ids."""

    def test_same_seed_same_bytes(self):
        a, b = build_fixture(3), build_fixture(3)
        self.assertEqual(json.dumps(a, sort_keys=True), json.dumps(b, sort_keys=True))
        self.assertNotEqual(sorted(a[0]), sorted(build_fixture(4)[0]))


class Strip1_4_KeyStaysOut(unittest.TestCase):
    """STRIP1/4 (inferred): the key never lands in the corpus, and fragment
    ids carry no source order."""

    def test_layout(self):
        with open(S.DEFAULT_FINDINGS) as f:
            frags, key = S.build(f.read(), 1)
        with tempfile.TemporaryDirectory() as d:
            S.write_build(d, frags, key)
            names = os.listdir(os.path.join(d, "corpus"))
            self.assertTrue(all(re.fullmatch(r"f[0-9a-f]{8}\.md", n) for n in names))
            self.assertEqual(len(names), len(frags))
            self.assertTrue(os.path.exists(os.path.join(d, "key.json")))
        entries = [fid for fid, m in key["fragments"].items() if m["role"] == "entry"]
        self.assertNotEqual(entries, sorted(entries))


class Strip1_5_ScorerDiscriminates(unittest.TestCase):
    """STRIP1/5 (inferred): the scorer gives the key 1 and silence 0, punishes
    reversed direction, and costs a lie adopted above a lie seen."""

    def setUp(self):
        with open(S.DEFAULT_FINDINGS) as f:
            self.frags, self.key = S.build(f.read(), 1)

    def run_control(self, name):
        return S.score(self.key, S.control(name, self.key, self.frags))

    def test_controls(self):
        self.assertEqual(self.run_control("oracle")["score"], 1.0)
        self.assertEqual(self.run_control("null")["score"], 0.0)
        rev = self.run_control("reversed")
        self.assertLess(rev["score"], 1.0)
        self.assertEqual(rev["per_kind"]["corrects"]["exact"], 0)

    def test_lie_adopted_costs_more_than_lie_seen(self):
        r = [x for x in self.key["relations"] if x["kind"] == "contradicts"][0]
        adopt = S.score(self.key, {"relations": [{"kind": "corrects", "a": r["a"], "b": r["b"]}]})
        reject = S.score(self.key, {"relations": [{"kind": "corrects", "a": r["b"], "b": r["a"]}]})
        seen = S.score(self.key, {"relations": [{"kind": "contradicts", "a": r["b"], "b": r["a"]}]})
        self.assertGreater(adopt["cost"], reject["cost"])
        self.assertGreater(reject["cost"], seen["cost"])
        self.assertEqual((adopt["lie_wins"], reject["lie_wins"], seen["lie_wins"]), (1, 0, 0))
        n_lies = sum(1 for x in self.key["relations"] if x["kind"] == "contradicts")
        self.assertEqual(self.run_control("reversed")["lie_wins"], n_lies)

    def test_worse_of_two_answers_counts(self):
        # Both orders: with only one, "last answer wins" passes by accident.
        r = [x for x in self.key["relations"] if x["kind"] == "corrects"][0]
        right = {"kind": "corrects", "a": r["a"], "b": r["b"]}
        wrong = {"kind": "corrects", "a": r["b"], "b": r["a"]}
        alone = S.score(self.key, {"relations": [wrong]})["cost"]
        self.assertEqual(S.score(self.key, {"relations": [right, wrong]})["cost"], alone)
        self.assertEqual(S.score(self.key, {"relations": [wrong, right]})["cost"], alone)

    def test_unknown_kind_refused(self):
        with self.assertRaises(ValueError):
            S.score(self.key, {"relations": [{"kind": "related", "a": "x", "b": "y"}]})


class Strip1_6_TrapIsPosed(unittest.TestCase):
    """STRIP1/6 (inferred): the corpus poses the false-consensus trap -- a
    word-overlap librarian finds every echo and mistakes every lie for one.
    If this goes green-to-red, the lies stopped looking like their sources
    and the test stopped testing the harm it was built for."""

    def test_lexical_falls_for_lies(self):
        with open(S.DEFAULT_FINDINGS) as f:
            frags, key = S.build(f.read(), 1)
        res = S.score(key, S.control("lexical", key, frags))
        same, contra = res["per_kind"]["same"], res["per_kind"]["contradicts"]
        self.assertEqual(same["exact"], same["n"])
        self.assertEqual(contra["wrong"], contra["n"])
        for r in key["relations"]:
            if r["kind"] == "contradicts":
                meta = key["fragments"][r["a"]]
                self.assertNotEqual(frags[r["a"]], meta["altered"])
                self.assertIn(" ".join(meta["altered"].split()[:4]),
                              " ".join(frags[r["b"]].split()))


class Strip1_7_HoldoutFenced(unittest.TestCase):
    """STRIP1/7 (inferred): scoring a holdout seed is refused without a stated
    reason, and a refused look leaves no ledger line."""

    def test_refused_without_why(self):
        with open(S.DEFAULT_FINDINGS) as f:
            frags, key = S.build(f.read(), S.HOLDOUT_FLOOR + 7)
        self.assertEqual(S.split_of(key), "holdout")
        before = os.path.getsize(S.LEDGER) if os.path.exists(S.LEDGER) else -1
        with tempfile.TemporaryDirectory() as d:
            S.write_build(d, frags, key)
            ans = os.path.join(d, "a.json")
            with open(ans, "w") as f:
                json.dump({"relations": []}, f)
            rc = S.main(["score", os.path.join(d, "key.json"), ans, "--what", "t", "--against", "t"])
        self.assertEqual(rc, 2)
        after = os.path.getsize(S.LEDGER) if os.path.exists(S.LEDGER) else -1
        self.assertEqual(before, after)

    def test_splits(self):
        self.assertEqual(S.split_of({"source": "FINDINGS.md", "seed": 2}), "dev")
        self.assertEqual(S.split_of({"source": "FINDINGS.md", "seed": S.HOLDOUT_FLOOR}), "holdout")
        self.assertEqual(S.split_of({"source": "fixture-findings.md", "seed": S.HOLDOUT_FLOOR}), "pilot")


class Strip1_8_EveryLookLedgered(unittest.TestCase):
    """STRIP1/8 (inferred): a score writes one ledger line carrying what was
    tried, what would count against it, the split, seed, data version,
    answer hash, commit and metrics."""

    def test_line(self):
        from datetime import datetime, timezone
        frags, key = build_fixture()
        res = S.score(key, S.control("oracle", key, frags))
        args = type("A", (), {"what": "oracle", "against": "below 1", "why": None, "answer": "a.json"})
        line = S.ledger_line(key, "pilot", res, args, "ab" * 32,
                             datetime(2026, 11, 7, 12, 0, tzinfo=timezone.utc), "abc123")
        self.assertEqual(line["when"], "2026-11-07T12:00Z")
        self.assertEqual(line["data"], "FIX.md@" + key["source_sha256"][:16])
        self.assertEqual(line["metrics"]["score"], 1.0)
        for k in ("what", "against", "split", "seed", "answer_sha256", "commit"):
            self.assertTrue(line[k] is not None and line[k] != "", k)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "l.jsonl")
            S.append_ledger(p, line); S.append_ledger(p, line)
            with open(p) as f:
                self.assertEqual(len(f.read().splitlines()), 2)


if __name__ == "__main__":
    unittest.main()
