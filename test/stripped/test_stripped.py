"""Tests for stripped.py. Requirements STRIP1/1..6 are in README.md, all
(inferred) from the 2026-11-07 request. Run:

    python3 -m unittest discover -s test/stripped -v
"""
import json, os, re, sys, tempfile, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stripped as S

FIXTURE = """# FINDINGS.md

Preamble describing the convention: corrections go in a block quote.

---

## Alpha: the gamma leg made recall worse

**Date:** 2026-08-14

The entry below ("Beta: the gamma leg ... is harmless") judged it harmless.
That judgement is wrong: recall@1 fell from 0.302 to 0.256 on the dev set.

Second paragraph so this entry is long enough to echo.

## Beta: the gamma leg is harmless, measured
on three documents

Measured on three documents, the gamma leg moved nothing at all here.

> **Correction, 2026-08-13 (later the same day):** that count is stale.
> It is now **8**.

Tail paragraph after the block.

~~**Until someone rebuilds, the binary has the bug.**~~ -- superseded by the
rebuild noted above.

## Delta: a plain citation is not a correction

See the entry below ("Epsilon") for the details of how it was measured.

Delta has a second paragraph so it can be echoed in the fixture.

## Epsilon: opening a raw key is 52x cheaper than a passphrase

Opening with a raw 256-bit key took 5.99 ms against 345.93 ms for a passphrase.

The KDF is skipped entirely when the key is 64 hex characters long, measured.
"""

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


if __name__ == "__main__":
    unittest.main()
