#!/usr/bin/env python3
"""Tests for dm.py (REQ1/DOC-1): every row of the data doctor is seen to fail on a fixture built to fail it (DM1/T3,
SE1/T3), and a fixture built to pass passes every row. Run: python3 -I tools/test_dm.py. Standard
library only; no network, no wall clock, no shared state: every fixture is a fresh git repository in a
temporary directory. SE1's own rows are se.py's to test; here they are only seen to run when se.py is
found (SE_GUIDE, .se1/, or ../software-engineering/)."""
import contextlib
import importlib.util
import io
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GUIDE = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("dm", GUIDE / "dm.py")
dm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dm)

DM_AGENTS = (GUIDE / "AGENTS.md").read_text(encoding="utf8")
SE_AGENTS = ("# AGENTS.md\n\nA working agreement. The IDs are defined in the guide at "
             "https://github.com/wmacevoy/software-engineering/tree/v1.7; This copy is from **v1.7**.\n")

GOOD = {
    "AGENTS.md": SE_AGENTS,
    "AGENTS-DM.md": DM_AGENTS,
    "CLAUDE.md": "# Project\n\n@AGENTS.md\n@AGENTS-DM.md\n@QUICKREF.md\n",
    "QUICKREF.md": "# SE1 quick reference\n",
    "CONTRIBUTING.md": (
        "# Contributing\n\n## Run it\n\n"
        "- **Raw to report:** `make report` — every reported number, about four minutes\n"
        "- **Main product:** `reports/report.md` — the report the client reads\n"
        "- **Fetch / refresh raw data:** `make fetch`\n\n## Check it\n\n"
        "- **The full check:** `python -m unittest` — the same one CI runs\n- **Success prints:** `OK`\n\n"
        "## Data rules for this repository\n\n"
        "- **Raw data:** lives in `data/raw`, read-only; never hand-edited.\n"
        "- **The holdout:** lives in `../holdout/`; Ann may read it; spent only at a decision.\n"),
    "docs/QUESTION_v1.md": (
        "# Question — v1\n\nClient: Ann · Date sent: 2026-10-20\n\n## The decision\n\n- **Decision:** open lot C early or not\n\n"
        "## Bias\n\n- **Groups that matter:** students and staff — the client\n"
        "- **BIAS-1** *(since v1)* Error rate per group, gap at most 5 points; Ann chose it.\n\n"
        "## Definitions\n\n- **KIND-1** *(since v1)* The kind of claim, one of predicted, described, explained, extracted: predicted.\n"
        "- **UNIT-1** *(since v1)* One row is: one lot on one event night.\n"
        "- **OUT-1** *(since v1)* The outcome: lot full by 18:40 local.\n"
        "- **PRED-1** *(since v1)* The prediction point: 17:00 local on the event day.\n\n"
        "## The bar\n\n- **BAR-1** *(since v1)* MAE minutes on the 2026 holdout, at most 15, against same-as-last-event at 19.2, with the interval.\n\n"
        "## Not for\n\n- lots off campus — no data\n"),
    "docs/PLAN_v1.md": (
        "# Analysis plan — v1\n\nQuestion: `QUESTION_v1` · Written: 2026-10-21 · Outcome examined yet? _no_\n\n"
        "## Split\n\n- **Grouping:** rows sharing an event stay on one side.\n"
        "- **Holdout:** `../holdout/` — kept by Ann outside the repository; spent once, at the final decision.\n\n"
        "## Metric and baseline\n\n- **Primary metric:** MAE minutes\n"
        "- **Baseline:** same as last comparable event — scored on 2026-10-21: MAE 19.2\n- **Interval:** bootstrap\n\n"
        "## Slices\n\n| Slice | Why it matters | Expected *n* |\n| ----- | -------------- | ------------ |\n| small lots | pooled toward the big ones | 40 |\n\n"
        "## Departures\n"),
    "docs/datasheets/gate.md": (
        "# Datasheet — gate scans\n\n## Origin\n\n- **Source:** the gate system\n"
        "- **This sheet:** written by Ann on 2026-10-21; measured. **What would show it wrong:** the row-count assertion in `src/load.py`\n\n"
        "## Columns\n\n| Column | Meaning | Units / clock | Sentinels and defaults | Knowable at | Absent / late / wrong / lying |\n"
        "| ------ | ------- | ------------- | ---------------------- | ----------- | ----------------------------- |\n"
        "| ts | scan time | UTC | none | ts + 5 min upload lag | dropped when offline |\n"
        "| lot | lot id | — | none | ts | — |\n"),
    "docs/ledger.jsonl": (
        json.dumps({"when": "2026-10-22T18:00Z", "tried": "baseline", "split": "validation", "holdout": False}) + "\n"
        + json.dumps({"when": "2026-10-25T18:00Z", "tried": "gbm 3", "split": "holdout", "holdout": True, "why": "final choice"}) + "\n"),
    "docs/model_cards/lot_fill.md": (
        "# Model card — lot fill, v1\n\n## Identity\n\n"
        "- **Data version:** raw@2026-10-20 · **Code commit:** abc123 · **Seed(s):** 1, 2, 3 · **Environment:** pixi.lock\n"
        "- **Owner:** Ann · **Next check that it is still true:** 2026-12-01\n"
        "- **This card:** written by Ann on 2026-10-25 · **What would retire it:** MAE above 15 on any slice\n\n"
        "## Bias audit\n\n| Group | *n* | Measure (interval) | Calibration | Gap vs. `Q1/BIAS-1` |\n"
        "| ----- | --- | ------------------ | ----------- | ------------------- |\n| students | 400 | 12 (10–14) | ok | 2 |\n"),
    "src/score.py": "LEDGER = 'docs/ledger.jsonl'\n",
    "data/raw/a.csv": "ts,lot\n2026-10-01T00:00Z,C\n",
    "pixi.lock": "version: 6\n",
    "tests/test_pipeline.py": "# Q1/OUT-1 and Q1/BAR-1 are asserted here\n",
    ".gitignore": "data/holdout/\n",
    ".github/workflows/check.yml": "on: push\njobs:\n  check:\n    steps:\n      - run: python -m unittest\n",
}
READ_ONLY = ("data/raw/a.csv",)


def git(repo, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *args], cwd=repo,
                          capture_output=True, text=True, check=True).stdout


def make_repo(tmp, files=GOOD, read_only=READ_ONLY):
    repo = Path(tmp)
    for rel, text in files.items():
        p = repo / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w", encoding="utf8", newline="\n") as fh:  # Path.write_text(newline=) is 3.10+; this runs on 3.9
            fh.write(text)
    git(repo, "init", "-q")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "chore: start")
    for rel in read_only:
        if (repo / rel).exists():
            os.chmod(repo / rel, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
    return repo


def finding(findings, name):
    return next(f for f in findings if f.name == name)


class Fixture(unittest.TestCase):
    """A fresh repository per test; only DM1's rows, so a missing se.py cannot fail these."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        for p in Path(self.tmp.name).rglob("*"):  # a read-only file would block cleanup on Windows
            if p.is_file():
                os.chmod(p, stat.S_IRUSR | stat.S_IWUSR)
        self.tmp.cleanup()

    def doctor(self, files=GOOD, read_only=READ_ONLY):
        repo = make_repo(self.tmp.name, files, read_only)
        return dm.doctor(repo, online=False, se=None)

    def changed(self, **edits):
        files = dict(GOOD)
        for k, v in edits.items():
            if v is None:
                files.pop(k, None)
            else:
                files[k] = v
        return files


DM_ROWS = ("data working agreement", "extends", "question", "plan", "holdout", "datasheets", "raw is read-only",
           "raw to report", "ledger", "model card", "environment", "question citations")


class TestGood(Fixture):
    def test_good_project_passes_every_dm_row(self):
        f = self.doctor()
        states = {x.name: x.state for x in f}
        self.assertEqual(states["SE1 doctor"], dm.NONE)  # no se.py handed in: said, not hidden
        for name in DM_ROWS:
            self.assertEqual(states[name], dm.PASS, f"{name}: {finding(f, name).text}")

    def test_guide_itself_is_not_a_project(self):
        f = dm.dm_rows(GUIDE)
        self.assertEqual(finding(f, "question").state, dm.NONE)
        self.assertEqual(finding(f, "datasheets").state, dm.NONE)


class TestAgreements(Fixture):
    def test_no_dm_copy(self):
        f = self.doctor(self.changed(**{"AGENTS-DM.md": None, "CLAUDE.md": "@AGENTS.md\n"}))
        self.assertEqual(finding(f, "data working agreement").state, dm.FAIL)
        self.assertIn("no copy", finding(f, "data working agreement").text)

    def test_no_se_copy(self):
        f = self.doctor(self.changed(**{"AGENTS.md": None, "CLAUDE.md": "@AGENTS-DM.md\n"}))
        self.assertEqual(finding(f, "data working agreement").state, dm.FAIL)
        self.assertIn("SE1 AGENTS.md", finding(f, "data working agreement").text)

    def test_unlabeled_dm_copy(self):
        f = self.doctor(self.changed(**{"AGENTS-DM.md": re.sub(r"\*\*(v\d+\.\d+)\*\*", r"\1", DM_AGENTS)}))
        self.assertEqual(finding(f, "data working agreement").state, dm.FAIL)
        self.assertIn("no release label", finding(f, "data working agreement").text)

    def test_claude_md_imports_only_one(self):
        f = self.doctor(self.changed(**{"CLAUDE.md": "@AGENTS.md\n"}))
        self.assertEqual(finding(f, "data working agreement").state, dm.FAIL)
        self.assertIn("does not import AGENTS-DM.md", finding(f, "data working agreement").text)

    def test_other_names_pass_and_are_told_the_guides_names(self):
        f = self.doctor(self.changed(**{"AGENTS-DM.md": None, "docs/AGENTS-DATA.md": DM_AGENTS, "CLAUDE.md": "@AGENTS.md\n@docs/AGENTS-DATA.md\n"}))
        self.assertEqual(finding(f, "data working agreement").state, dm.PASS)
        self.assertIn("AGENTS-DM.md", finding(f, "data working agreement").text)

    def test_extends_the_se1_copy_present(self):
        # REQ1/AGR-1: this guide's agreement extends SE1's at a named release.
        f = self.doctor()
        self.assertEqual(finding(f, "extends").state, dm.PASS)
        self.assertIn("extends SE1 v1.7", finding(f, "extends").text)

    def test_extends_an_older_se1_than_the_copy_here(self):
        f = self.doctor(self.changed(**{"AGENTS.md": SE_AGENTS.replace("v1.7", "v1.8")}))
        self.assertEqual(finding(f, "extends").state, dm.WARN)
        self.assertIn("not reviewed", finding(f, "extends").text)

    def test_extends_a_newer_se1_than_the_copy_here(self):
        f = self.doctor(self.changed(**{"AGENTS.md": SE_AGENTS.replace("v1.7", "v1.6")}))
        self.assertEqual(finding(f, "extends").state, dm.FAIL)
        self.assertIn("older", finding(f, "extends").text)

    def test_extends_needs_a_labeled_se1_copy(self):
        f = self.doctor(self.changed(**{"AGENTS.md": SE_AGENTS.replace("This copy is from **v1.7**", "no label")}))
        self.assertEqual(finding(f, "extends").state, dm.WARN)

    def test_agreement_row_says_configured_not_observed(self):
        # course field report: the row said "observed loading" having only read the imports
        text = finding(self.doctor(), "data working agreement").text
        self.assertIn("configured", text)
        self.assertIn("phrase test", text)
        self.assertNotIn("observed loading", text)

    def test_no_claude_md_loads_only_the_root_file(self):
        f = self.doctor(self.changed(**{"CLAUDE.md": None}))
        self.assertEqual(finding(f, "data working agreement").state, dm.FAIL)
        self.assertIn("nothing loads", finding(f, "data working agreement").text)


class TestQuestionAndPlan(Fixture):
    def test_no_question(self):
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": None, "tests/test_pipeline.py": "# nothing\n",
                                        "docs/model_cards/lot_fill.md": GOOD["docs/model_cards/lot_fill.md"].replace("`Q1/BIAS-1`", "the bias measure")}))
        self.assertEqual(finding(f, "question").state, dm.NONE)
        self.assertEqual(finding(f, "question citations").state, dm.NONE)

    def test_question_missing_a_one_way_door(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("- **PRED-1** *(since v1)* The prediction point: 17:00 local on the event day.\n", "")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q}))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertIn("PRED", finding(f, "question").text)

    def test_question_with_the_templates_placeholder(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("One row is: one lot on one event night.", "One row is: _…_")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q}))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertIn("placeholder", finding(f, "question").text)

    def test_question_without_a_bar(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("**BAR-1** *(since v1)*", "**BAR-1**")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q, "tests/test_pipeline.py": "# Q1/OUT-1\n"}))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertIn("bar", finding(f, "question").text)

    def test_question_without_not_for(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("## Not for", "## Later")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q}))
        self.assertEqual(finding(f, "question").state, dm.WARN)

    def test_no_plan_with_a_question(self):
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": None}))
        self.assertEqual(finding(f, "plan").state, dm.WARN)

    def test_plan_holdout_placeholder(self):
        p = GOOD["docs/PLAN_v1.md"].replace("`../holdout/` — kept by Ann outside the repository; spent once, at the final decision.",
                                            "where it is kept, who may read it, and when it will be spent: _…_")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p}))
        self.assertEqual(finding(f, "plan").state, dm.FAIL)
        self.assertIn("holdout", finding(f, "plan").text)

    def test_plan_without_slices_or_a_scored_baseline(self):
        p = GOOD["docs/PLAN_v1.md"].replace("| small lots | pooled toward the big ones | 40 |\n", "").replace("scored on 2026-10-21: MAE 19.2", "scored on _yyyy-mm-dd_: _…_")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p}))
        self.assertEqual(finding(f, "plan").state, dm.WARN)
        self.assertIn("slices", finding(f, "plan").text)
        self.assertIn("baseline", finding(f, "plan").text)


class TestKindOfClaim(Fixture):
    KIND = "extracted: predicted."
    OUT = "- **OUT-1** *(since v1)* The outcome: lot full by 18:40 local.\n"
    PRED = "- **PRED-1** *(since v1)* The prediction point: 17:00 local on the event day.\n"

    def question(self, kind, *drop, waive=""):
        q = GOOD["docs/QUESTION_v1.md"].replace(self.KIND, f"extracted: {kind}.")
        for line in drop:
            q = q.replace(line, "")
        return q + (f"\n## Not applicable\n\n| Row | Why |\n| --- | --- |\n{waive}" if waive else "")

    def files(self, q, **more):
        return self.changed(**{"docs/QUESTION_v1.md": q, "tests/test_pipeline.py": "# Q1/BAR-1 is asserted here\n", **more})

    def test_no_kind_warns_and_is_read_as_predicted(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("- **KIND-1** *(since v1)* The kind of claim, one of predicted, described, explained, extracted: predicted.\n", "")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q}))
        self.assertEqual(finding(f, "question").state, dm.WARN)
        self.assertIn("kind of claim", finding(f, "question").text)

    def test_a_kind_it_does_not_know_fails(self):
        f = self.doctor(self.files(self.question("a hunch")))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertIn("KIND-n", finding(f, "question").text)

    def test_the_kinds_on_the_label_are_not_the_answer(self):
        f = self.doctor(self.files(self.question("_…_")))
        self.assertEqual(finding(f, "question").state, dm.FAIL)

    def test_extracted_and_described_need_no_outcome_or_prediction_point(self):
        for kind in ("extracted", "described"):
            with self.subTest(kind):
                f = dm.dm_rows(make_repo(tempfile.mkdtemp(dir=self.tmp.name), self.files(self.question(kind, self.OUT, self.PRED))))
                self.assertEqual(finding(f, "question").state, dm.PASS, finding(f, "question").text)
                self.assertIn(kind, finding(f, "question").text)

    def test_explained_needs_an_outcome_and_no_prediction_point(self):
        f = dm.dm_rows(make_repo(tempfile.mkdtemp(dir=self.tmp.name), self.files(self.question("explained", self.PRED))))
        self.assertEqual(finding(f, "question").state, dm.PASS, finding(f, "question").text)
        f = dm.dm_rows(make_repo(tempfile.mkdtemp(dir=self.tmp.name), self.files(self.question("explained", self.OUT))))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertIn("OUT", finding(f, "question").text)

    def test_predicted_still_needs_both(self):
        f = self.doctor(self.files(self.question("predicted", self.OUT)))
        self.assertEqual(finding(f, "question").state, dm.FAIL)

    def test_not_applicable_needs_a_kind_that_allows_it_and_a_reason(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| ledger | nothing is scored |\n| model card | no model |\n")
        f = self.doctor(self.files(q, **{"docs/ledger.jsonl": None, "docs/model_cards/lot_fill.md": None}))
        self.assertEqual(finding(f, "ledger").state, dm.NA)
        self.assertEqual(finding(f, "ledger").text, "nothing is scored")
        self.assertEqual(finding(f, "model card").state, dm.NA)

    def test_not_applicable_without_a_reason_warns(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| ledger | |\n")
        f = self.doctor(self.files(q, **{"docs/ledger.jsonl": None}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)
        self.assertIn("no reason", finding(f, "ledger").text)

    def test_a_predicted_claim_cannot_set_a_row_aside(self):
        f = self.doctor(self.files(self.question("predicted", waive="| ledger | we would rather not |\n"), **{"docs/ledger.jsonl": None}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)
        self.assertIn("predicted", finding(f, "ledger").text)

    def test_no_kind_cannot_set_a_row_aside(self):
        q = GOOD["docs/QUESTION_v1.md"].replace("- **KIND-1** *(since v1)* The kind of claim, one of predicted, described, explained, extracted: predicted.\n", "")
        q += "\n## Not applicable\n\n| Row | Why |\n| --- | --- |\n| ledger | we would rather not |\n"
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q, "docs/ledger.jsonl": None}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)

    def test_no_kind_can_set_aside_a_datasheet(self):
        # the first field report: the datasheet is the row that should have survived
        for kind in ("described", "extracted", "explained"):
            with self.subTest(kind):
                q = self.question(kind, self.PRED, waive="| datasheets | documents, not columns |\n")
                f = dm.dm_rows(make_repo(tempfile.mkdtemp(dir=self.tmp.name), self.files(q, **{"docs/datasheets/gate.md": None})))
                self.assertEqual(finding(f, "datasheets").state, dm.WARN)

    def test_a_row_set_aside_that_fails_still_fails(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| holdout | no holdout here |\n")
        f = self.doctor(self.files(q, **{"data/holdout.csv": "a\n"}))
        self.assertEqual(finding(f, "holdout").state, dm.FAIL)
        self.assertIn("not applicable", finding(f, "holdout").text)

    def test_a_row_set_aside_whose_thing_is_here_says_so(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| ledger | nothing is scored |\n")
        f = self.doctor(self.files(q))
        self.assertEqual(finding(f, "ledger").state, dm.PASS)
        self.assertIn("not applicable", finding(f, "ledger").text)

    def test_the_templates_example_row_sets_nothing_aside(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| _row_ | _why, in a sentence_ |\n")
        f = self.doctor(self.files(q, **{"docs/ledger.jsonl": None}))
        self.assertEqual(finding(f, "ledger").state, dm.NONE)

    def test_a_baseline_that_says_not_applicable(self):
        # course field report: the plan warned "baseline not yet scored" on a plan that says nothing is scored
        p = GOOD["docs/PLAN_v1.md"].replace("same as last comparable event — scored on 2026-10-21: MAE 19.2", "not applicable: nothing is scored")
        f = self.doctor(self.files(self.question("described", self.OUT, self.PRED), **{"docs/PLAN_v1.md": p}))
        self.assertEqual(finding(f, "plan").state, dm.PASS, finding(f, "plan").text)
        f = dm.dm_rows(make_repo(tempfile.mkdtemp(dir=self.tmp.name), self.changed(**{"docs/PLAN_v1.md": p})))
        self.assertEqual(finding(f, "plan").state, dm.WARN)  # a predicted claim has a baseline

    def test_main_prints_it_and_does_not_fail_on_it(self):
        q = self.question("extracted", self.OUT, self.PRED, waive="| ledger | nothing is scored |\n")
        repo = make_repo(self.tmp.name, self.files(q, **{"docs/ledger.jsonl": None}))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = dm.main(["--doctor", "--repo", str(repo), "--dm-only", "--offline"])
        self.assertEqual(rc, 0, out.getvalue())
        self.assertIn("[     n/a] ledger", out.getvalue())

    def test_the_question_template_is_read_as_unfilled(self):
        q = (GUIDE / "templates" / "QUESTION_v1.md").read_text(encoding="utf8")
        f = self.doctor(self.changed(**{"docs/QUESTION_v1.md": q}))
        self.assertEqual(finding(f, "question").state, dm.FAIL)
        self.assertEqual(finding(f, "ledger").state, dm.PASS)  # its Not applicable table waives nothing


class TestHoldout(Fixture):
    def inside(self, text):
        return self.changed(**{"docs/PLAN_v1.md": GOOD["docs/PLAN_v1.md"].replace("`../holdout/`", "`data/holdout/`"),
                               "CONTRIBUTING.md": GOOD["CONTRIBUTING.md"].replace("`../holdout/`", "`data/holdout/`"), **text})

    def test_absent_and_ignored_is_out_of_reach(self):
        f = self.doctor(self.inside({}))
        self.assertEqual(finding(f, "holdout").state, dm.PASS)
        self.assertIn("git-ignored", finding(f, "holdout").text)

    def test_present_and_readable_is_in_reach(self):
        repo = make_repo(self.tmp.name, self.inside({}))
        (repo / "data" / "holdout").mkdir()
        (repo / "data" / "holdout" / "x.csv").write_text("a\n")
        f = dm.dm_rows(repo)
        self.assertEqual(finding(f, "holdout").state, dm.FAIL)
        self.assertIn("readable", finding(f, "holdout").text)

    def test_tracked_file_named_holdout(self):
        f = self.doctor(self.changed(**{"data/holdout.csv": "a\n"}))
        self.assertEqual(finding(f, "holdout").state, dm.FAIL)
        self.assertIn("tracked", finding(f, "holdout").text)

    def test_none_is_a_true_answer(self):
        # first field report: the names on a line saying there is no holdout were read as its paths
        c = GOOD["CONTRIBUTING.md"].replace("lives in `../holdout/`; Ann may read it; spent only at a decision.",
                                            "none, in any project. `src` scores every run on its test split.")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": None, "CONTRIBUTING.md": c, "src/split.py": "TEST = 0.2\n"}))
        self.assertEqual(finding(f, "holdout").state, dm.NONE)
        self.assertIn("none", finding(f, "holdout").text)

    def test_a_command_on_the_line_is_not_a_path(self):
        # course field report (N5): `make split` beside the real path was read as a second path, and warned
        c = GOOD["CONTRIBUTING.md"].replace("spent only at a decision.", "made by `make split`; spent only at a decision.")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "holdout").state, dm.PASS, finding(f, "holdout").text)
        self.assertNotIn("make split", finding(f, "holdout").text)

    def test_a_path_with_a_space_that_exists_is_still_a_path(self):
        c = GOOD["CONTRIBUTING.md"].replace("`../holdout/`", "`my holdout/x.csv`")
        p = GOOD["docs/PLAN_v1.md"].replace("`../holdout/`", "`my holdout/x.csv`")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c, "docs/PLAN_v1.md": p, "my holdout/x.csv": "a\n"}))
        self.assertEqual(finding(f, "holdout").state, dm.FAIL)

    def test_named_without_a_path(self):
        p = GOOD["docs/PLAN_v1.md"].replace("`../holdout/` — kept by Ann", "kept by Ann")
        c = GOOD["CONTRIBUTING.md"].replace("lives in `../holdout/`", "Ann has it")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p, "CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "holdout").state, dm.WARN)


class TestDataAndPipeline(Fixture):
    def test_holdout_only_in_unfilled_templates_is_not_a_pass(self):
        # REQ1/DOC-1. Both lines as the templates ship them: nobody has said where the holdout lives.
        p = GOOD["docs/PLAN_v1.md"].replace("`../holdout/` — kept by Ann outside the repository; spent once, at the final decision.",
                                            "_where it is kept, who may read it, and when it will be spent_")
        c = GOOD["CONTRIBUTING.md"].replace("lives in `../holdout/`; Ann may read it", "lives in `_path_`; _who_ may read it")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p, "CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "holdout").state, dm.NONE)
        self.assertIn("template", finding(f, "holdout").text)

    def test_holdout_named_in_prose_beside_an_unfilled_template_warns(self):
        # REQ1/DOC-1. One real line without a path is still a holdout somebody must check by hand.
        p = GOOD["docs/PLAN_v1.md"].replace("`../holdout/` — kept by Ann", "kept by Ann")
        c = GOOD["CONTRIBUTING.md"].replace("lives in `../holdout/`; Ann may read it", "lives in `_path_`; _who_ may read it")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p, "CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "holdout").state, dm.WARN)

    def test_plan_template_unfilled_says_the_holdout_has_no_home(self):
        # REQ1/DOC-1. The published template, copied as is.
        p = (GUIDE / "templates" / "PLAN_v1.md").read_text(encoding="utf8")
        f = self.doctor(self.changed(**{"docs/PLAN_v1.md": p}))
        self.assertEqual(finding(f, "plan").state, dm.FAIL)
        self.assertIn("holdout", finding(f, "plan").text)

    def test_datasheet_template_unfilled_is_not_a_pass(self):
        # REQ1/DOC-1. The published template, copied as is: zero columns cannot all say anything.
        d = (GUIDE / "templates" / "DATASHEET.md").read_text(encoding="utf8")
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": d}))
        self.assertEqual(finding(f, "datasheets").state, dm.FAIL)
        self.assertIn("no columns", finding(f, "datasheets").text)

    def test_the_vendored_doctor_is_not_the_ledgers_writer(self):
        # REQ1/DOC-1. Seen on a real adoption: dm.py names ledger.jsonl in its own source and read as the writer.
        f = self.doctor(self.changed(**{"src/score.py": None, "dm.py": "LEDGER = 'ledger.jsonl'\n", "se.py": "# ledger.jsonl\n"}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)
        self.assertIn("no code references it", finding(f, "ledger").text)

    def test_no_datasheet(self):
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": None}))
        self.assertEqual(finding(f, "datasheets").state, dm.NONE)

    def test_datasheet_column_without_knowable_at(self):
        d = GOOD["docs/datasheets/gate.md"].replace("| lot | lot id | — | none | ts | — |", "| lot | lot id | — | none |  | — |")
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": d}))
        self.assertEqual(finding(f, "datasheets").state, dm.FAIL)
        self.assertIn("lot", finding(f, "datasheets").text)

    def test_datasheet_with_the_templates_placeholder(self):
        d = GOOD["docs/datasheets/gate.md"].replace("| ts | scan time | UTC | none | ts + 5 min upload lag |", "| ts | scan time | UTC | none | _ts + upload lag (up to …)_ |")
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": d}))
        self.assertEqual(finding(f, "datasheets").state, dm.FAIL)

    def test_datasheet_nobody_signed(self):
        d = GOOD["docs/datasheets/gate.md"].replace("- **This sheet:**", "- **Sheet:**")
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": d}))
        self.assertEqual(finding(f, "datasheets").state, dm.WARN)

    def test_a_datasheet_for_documents(self):
        # first field report: a document set has parts, not columns, and the row should survive for it
        d = ("# Datasheet — case files\n\n- **This sheet:** written by Ann on 2026-10-21; measured.\n\n## Documents\n\n"
             "| Part | How each page was made | Text layer? | Identifiers printed | Knowable at | Absent / late / wrong / lying |\n"
             "| - | - | - | - | - | - |\n| letter | scanned | none: OCR | a signature | its date | late |\n| form | typed | yes | none seen | |  |\n")
        f = self.doctor(self.changed(**{"docs/datasheets/gate.md": d}))
        self.assertEqual(finding(f, "datasheets").state, dm.FAIL)
        self.assertIn("form", finding(f, "datasheets").text)

    def test_raw_writable(self):
        f = self.doctor(read_only=())
        self.assertEqual(finding(f, "raw is read-only").state, dm.FAIL)
        self.assertIn("writable", finding(f, "raw is read-only").text)

    def test_raw_absent(self):
        f = self.doctor(self.changed(**{"data/raw/a.csv": None}))
        self.assertEqual(finding(f, "raw is read-only").state, dm.NONE)

    def test_raw_path_on_a_wrapped_line(self):
        # first field report: the path on the template's second, indented line was never read
        c = GOOD["CONTRIBUTING.md"].replace("lives in `data/raw`, read-only;", "the only raw data, in\n  `data/raw`, read-only;")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}), read_only=())
        self.assertEqual(finding(f, "raw is read-only").state, dm.FAIL)
        self.assertIn("writable", finding(f, "raw is read-only").text)

    GLOB = "lives in `inputs/*.zip`, read-only;"

    def test_raw_named_by_a_pattern_leaves_sidecars_alone(self):
        # first field report: reports the owner asked for, beside the raw files, failed the row
        c = GOOD["CONTRIBUTING.md"].replace("lives in `data/raw`, read-only;", self.GLOB)
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c, "inputs/a.zip": "z", "inputs/a-report.md": "a sidecar\n"}), read_only=("inputs/a.zip",))
        self.assertEqual(finding(f, "raw is read-only").state, dm.PASS, finding(f, "raw is read-only").text)
        self.assertIn("1 file", finding(f, "raw is read-only").text)

    def test_raw_named_by_a_pattern_still_fails_on_what_it_names(self):
        c = GOOD["CONTRIBUTING.md"].replace("lives in `data/raw`, read-only;", self.GLOB)
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c, "inputs/a.zip": "z"}), read_only=())
        self.assertEqual(finding(f, "raw is read-only").state, dm.FAIL)

    def test_raw_is_the_first_path_and_the_rest_of_the_line_is_said_not_read(self):
        # the course repository, run after this row checked every backtick: its line names three projects that are
        # NOT raw, and the row counted their files, 303 where the truth is 80. No fixture had a line like that.
        c = GOOD["CONTRIBUTING.md"].replace("lives in `data/raw`, read-only;", "lives in `data/raw`; `other` keeps a cache, which is not raw;")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c, "other/b.csv": "z\n"}))
        self.assertEqual(finding(f, "raw is read-only").state, dm.PASS, finding(f, "raw is read-only").text)
        self.assertIn("1 file(s) under data/raw", finding(f, "raw is read-only").text)
        self.assertIn("not checked: other", finding(f, "raw is read-only").text)

    def test_raw_a_command_on_the_line_is_not_the_first_path(self):
        c = GOOD["CONTRIBUTING.md"].replace("lives in `data/raw`, read-only;", "pulled by `make fetch` into `data/raw`, read-only;")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "raw is read-only").state, dm.PASS, finding(f, "raw is read-only").text)
        self.assertNotIn("make fetch", finding(f, "raw is read-only").text)

    def test_raw_to_report_placeholder(self):
        c = GOOD["CONTRIBUTING.md"].replace("`make report`", "`_command_`")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "raw to report").state, dm.FAIL)

    def test_raw_to_report_says_it_was_not_run(self):
        # course field report: the row passed a command that could not run, because it only reads the line
        text = finding(self.doctor(), "raw to report").text
        self.assertIn("configured", text)
        self.assertIn("not run", text)

    def test_raw_to_report_says_what_it_makes(self):
        self.assertIn("`reports/report.md`", finding(self.doctor(), "raw to report").text)

    def test_raw_to_report_without_a_main_product_warns(self):
        # first field report: it passed a command that does not make the project's main product
        c = GOOD["CONTRIBUTING.md"].replace("- **Main product:** `reports/report.md` — the report the client reads\n", "")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "raw to report").state, dm.WARN)
        self.assertIn("main product", finding(f, "raw to report").text)

    def test_raw_to_report_none_is_a_true_answer(self):
        # course field report: a line with no backtick could only fail as a placeholder
        c = GOOD["CONTRIBUTING.md"].replace("`make report` — every reported number, about four minutes", "none yet; the project starts next week")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "raw to report").state, dm.NONE)

    def test_raw_to_report_missing(self):
        c = GOOD["CONTRIBUTING.md"].replace("- **Raw to report:** `make report` — every reported number, about four minutes\n", "")
        f = self.doctor(self.changed(**{"CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "raw to report").state, dm.NONE)


class TestLedgerCardEnvironment(Fixture):
    def test_no_ledger(self):
        f = self.doctor(self.changed(**{"docs/ledger.jsonl": None}))
        self.assertEqual(finding(f, "ledger").state, dm.NONE)

    def test_ledger_nobody_writes(self):
        f = self.doctor(self.changed(**{"src/score.py": "x = 1\n"}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)
        self.assertIn("from memory", finding(f, "ledger").text)

    def test_ledger_counts_holdout_spends(self):
        f = self.doctor()
        self.assertIn("2 rows, holdout spent 1 time", finding(f, "ledger").text)

    def test_markdown_ledger_ignores_the_templates_example_row(self):
        md = "# Ledger\n\n| When (UTC) | What was tried | Split | Holdout? why |\n| - | - | - | - |\n| _2026-10-27T18:02Z_ | _baseline_ | _validation_ | _no_ |\n"
        f = self.doctor(self.changed(**{"docs/ledger.jsonl": None, "docs/LEDGER.md": md}))
        self.assertEqual(finding(f, "ledger").state, dm.NONE)
        self.assertIn("empty", finding(f, "ledger").text)

    def test_a_named_log_of_reads_is_a_ledger(self):
        # first field report: the row said none yet with logs of every search and page read in place
        c = GOOD["CONTRIBUTING.md"] + "- **The ledger:** `logs/reads.log` and `logs/searches.jsonl`\n"
        f = self.doctor(self.changed(**{"docs/ledger.jsonl": None, "CONTRIBUTING.md": c, "logs/reads.log": "p1\np2\n\n",
                                        "logs/searches.jsonl": '{"q": "x"}\n', "src/score.py": "LOG = 'reads.log'\n"}))
        self.assertEqual(finding(f, "ledger").state, dm.PASS, finding(f, "ledger").text)
        self.assertIn("3 rows", finding(f, "ledger").text)
        self.assertIn("logs/reads.log", finding(f, "ledger").text)

    def test_a_named_ledger_that_is_not_there_warns(self):
        c = GOOD["CONTRIBUTING.md"] + "- **The ledger:** `logs/reads.log`\n"
        f = self.doctor(self.changed(**{"docs/ledger.jsonl": None, "CONTRIBUTING.md": c}))
        self.assertEqual(finding(f, "ledger").state, dm.WARN)
        self.assertIn("not here", finding(f, "ledger").text)

    def test_model_card_identity_placeholder(self):
        c = GOOD["docs/model_cards/lot_fill.md"].replace("**Seed(s):** 1, 2, 3", "**Seed(s):** _…_")
        f = self.doctor(self.changed(**{"docs/model_cards/lot_fill.md": c}))
        self.assertEqual(finding(f, "model card").state, dm.FAIL)
        self.assertIn("Seed", finding(f, "model card").text)

    def test_model_card_bias_audit_empty_while_groups_named(self):
        c = GOOD["docs/model_cards/lot_fill.md"].replace("| students | 400 | 12 (10–14) | ok | 2 |\n", "")
        f = self.doctor(self.changed(**{"docs/model_cards/lot_fill.md": c}))
        self.assertEqual(finding(f, "model card").state, dm.FAIL)
        self.assertIn("bias audit empty", finding(f, "model card").text)

    def test_installed_packages_and_dot_directories_are_not_the_project(self):
        f = self.doctor(self.changed(**{".pixi/envs/x/lib/site-packages/lib/model_card_template.md": "# Model card\n\n- **Data version:** _…_\n",
                                        "node_modules/x/DATASHEET.md": "# Datasheet\n", ".venv/ledger.csv": "a\n1\n"}))
        self.assertEqual(finding(f, "model card").state, dm.PASS)
        self.assertIn("1 card(s)", finding(f, "model card").text)
        self.assertIn("1 datasheet(s)", finding(f, "datasheets").text)
        self.assertIn("docs/ledger.jsonl", finding(f, "ledger").text)

    def test_model_card_nobody_signed(self):
        c = GOOD["docs/model_cards/lot_fill.md"].replace("- **This card:**", "- **Card:**")
        f = self.doctor(self.changed(**{"docs/model_cards/lot_fill.md": c}))
        self.assertEqual(finding(f, "model card").state, dm.WARN)

    def test_no_environment_file(self):
        f = self.doctor(self.changed(**{"pixi.lock": None}))
        self.assertEqual(finding(f, "environment").state, dm.NONE)

    def test_untracked_lock_is_the_known_gap(self):
        f = self.doctor(self.changed(**{"pixi.lock": None, ".gitignore": "data/holdout/\npixi.lock\n"}))
        Path(self.tmp.name, "pixi.lock").write_text("version: 6\n")
        f = dm.dm_rows(self.tmp.name)
        self.assertEqual(finding(f, "environment").state, dm.WARN)
        self.assertIn("not tracked", finding(f, "environment").text)


class TestCitations(Fixture):
    def test_bare_citation(self):
        f = self.doctor(self.changed(**{"tests/test_pipeline.py": "# OUT-1 is asserted here\n"}))
        self.assertEqual(finding(f, "question citations").state, dm.FAIL)
        self.assertIn("unversioned", finding(f, "question citations").text)

    def test_stale_citation_after_the_definition_moved(self):
        q2 = GOOD["docs/QUESTION_v1.md"].replace("# Question — v1", "# Question — v2").replace("**OUT-1** *(since v1)*", "**OUT-1** *(since v2)*")
        f = self.doctor(self.changed(**{"docs/QUESTION_v2.md": q2}))
        self.assertEqual(finding(f, "question citations").state, dm.FAIL)
        self.assertIn("stale", finding(f, "question citations").text)


class TestOneQuestionPerCheck(Fixture):
    """Course field report: one repository, several questions. One check per question's directory, and the agreements are looked for
    upward, as far as the repository's root and no further."""

    def several(self):
        files = {k: v for k, v in GOOD.items() if k in ("AGENTS.md", "AGENTS-DM.md", "CLAUDE.md", ".gitignore", ".github/workflows/check.yml")}
        files.update({"river/" + k: v for k, v in GOOD.items() if k.startswith(("docs/", "src/", "data/", "tests/", "CONTRIBUTING"))})
        return files

    def test_a_project_finds_the_agreements_at_the_root(self):
        repo = make_repo(self.tmp.name, self.several(), ("river/data/raw/a.csv",))
        f = dm.dm_rows(repo / "river")
        self.assertEqual(finding(f, "data working agreement").state, dm.PASS, finding(f, "data working agreement").text)
        self.assertEqual(finding(f, "extends").state, dm.PASS, finding(f, "extends").text)
        self.assertEqual(finding(f, "question").state, dm.PASS, finding(f, "question").text)
        self.assertEqual(finding(f, "raw is read-only").state, dm.PASS, finding(f, "raw is read-only").text)

    def test_it_says_it_was_not_seen_from_inside_the_project(self):
        # an import can stop at the working directory (SE1/A11), so the row must not sound as if a session started there is covered
        repo = make_repo(self.tmp.name, self.several(), ("river/data/raw/a.csv",))
        text = finding(dm.dm_rows(repo / "river"), "data working agreement").text
        self.assertIn("a session started at the root", text)
        self.assertNotIn("a session started at the root", finding(self.doctor(), "data working agreement").text)

    def test_it_stops_at_the_repositorys_root(self):
        above, inner = Path(self.tmp.name), Path(self.tmp.name) / "repo"
        (above / "AGENTS-DM.md").write_text(DM_AGENTS, encoding="utf8")
        (above / "AGENTS.md").write_text(SE_AGENTS, encoding="utf8")
        (above / "CLAUDE.md").write_text("@AGENTS.md\n@AGENTS-DM.md\n", encoding="utf8")
        inner.mkdir()
        repo = make_repo(inner, {"README.md": "# x\n"}, ())
        self.assertEqual(finding(dm.dm_rows(repo), "data working agreement").state, dm.FAIL)

    def test_a_run_at_the_root_points_at_the_projects(self):
        # the course's K2: from the root the doctor said none yet, truly and unhelpfully, about a project that had a question
        repo = make_repo(self.tmp.name, self.several(), ())
        f = finding(dm.dm_rows(repo), "question")
        self.assertEqual(f.state, dm.NONE)
        self.assertIn("--repo river", f.text)

    def test_se1s_rows_are_about_the_repository(self):
        # run on the course's project, SE1's rows failed twice, falsely, looking for agreements in the subdirectory
        repo = make_repo(self.tmp.name, self.several(), ("river/data/raw/a.csv",))
        seen = []

        class Stub:
            @staticmethod
            def doctor(path, online=True, cache_dir=None, today=None):
                seen.append(Path(path).resolve())
                return []
        dm.doctor(repo / "river", online=False, se=Stub)
        self.assertEqual(seen, [repo.resolve()])

    def test_the_guide_says_so(self):
        self.assertIn("--repo", (GUIDE / "AGENTS.md").read_text(encoding="utf8"))
        self.assertIn("--repo", (GUIDE / "templates" / "CONTRIBUTING.md").read_text(encoding="utf8"))


class TestWithSe(unittest.TestCase):
    def test_se_rows_run_when_se_py_is_found(self):
        se, path = dm.find_se(GUIDE)
        if se is None:
            self.skipTest("se.py not found (set SE_GUIDE); SE1's rows not exercised here")
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as cache:
            repo = make_repo(tmp, GOOD, ())
            f = dm.doctor(repo, online=False, cache_dir=cache, se=se)
            names = [x.name for x in f]
            self.assertIn("working set", names)   # SE1's
            self.assertIn("question", names)      # DM1's
            self.assertNotIn("SE1 doctor", names)


if __name__ == "__main__":
    unittest.main(verbosity=1)
