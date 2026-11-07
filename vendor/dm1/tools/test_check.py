#!/usr/bin/env python3
"""Tests for tools/check.py (REQ2/CHK-1, REQ1/PIN-1): each breakage is planted in a copy of this repository
and must turn the checker red with its own message (SE1/T3). Reads SE1 from vendor/se1, as the checker itself does. Python 3.9 or later and git; standard library
only. Run: python3 -I tools/test_check.py"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

GUIDE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SE_BLOB = "https://github.com/wmacevoy/software-engineering/blob/"


def replace_once(old, new):
    def edit(text):
        if old not in text:
            raise AssertionError(f"nothing to plant: {old!r} is not in the file")
        return text.replace(old, new, 1)
    return edit


def sub_once(pattern, new):
    def edit(text):
        out, n = re.subn(pattern, new, text, count=1)
        if not n:
            raise AssertionError(f"nothing to plant: {pattern!r} matched nothing")
        return out
    return edit


class Repo(unittest.TestCase):
    """A copy of this repository to plant breakages in, each undone after its test."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.repo = os.path.join(cls.tmp, "repo")
        shutil.copytree(GUIDE, cls.repo, ignore=shutil.ignore_patterns(".git", ".se1", "__pycache__"))

    @classmethod
    def git(cls, *args):
        subprocess.run(["git", "-c", "user.email=t@example.invalid", "-c", "user.name=t", "-c", "commit.gpgsign=false",
                        "-C", cls.repo, *args], capture_output=True, check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.saved = {}

    def tearDown(self):
        for name, text in self.saved.items():
            self.write(name, text)

    def read(self, name):
        with open(os.path.join(self.repo, name), encoding="utf-8", newline="") as fh:
            return fh.read()

    def write(self, name, text):
        with open(os.path.join(self.repo, name), "w", encoding="utf-8", newline="") as fh:
            fh.write(text)

    def plant(self, name, edit):
        text = self.read(name)
        self.saved.setdefault(name, text)
        self.write(name, edit(text))

    def check(self, *args):
        return subprocess.run([sys.executable, "-I", os.path.join(self.repo, "tools", "check.py"), *args], cwd=self.repo,
                              capture_output=True, encoding="utf-8")

    def red(self, message):
        r = self.check()
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(message, r.stdout)


class TestChecker(Repo):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # A patch release's rule reads its minor's tag, so the copy needs a history with that tag.
        with open(os.path.join(cls.repo, "CHANGELOG.md"), encoding="utf-8", newline="") as fh:
            newest_minor = re.search(r"(?m)^## (v\d+\.\d+)", fh.read()).group(1)
        cls.git("init", "-q")
        cls.git("add", "-A")
        cls.git("commit", "-qm", "base")
        cls.git("tag", newest_minor)

    def test_the_repository_as_it_stands_passes(self):
        r = self.check()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertRegex(r.stdout, r", 0 problems\n$")

    def test_missing_file(self):
        self.plant("README.md", replace_once("(CHECKLISTS.md)", "(NOPE.md)"))
        self.red("missing file   README.md")

    def test_missing_anchor(self):
        self.plant("README.md", replace_once("guide/01-purpose.md#p1)", "guide/01-purpose.md#zz)"))
        self.red("missing anchor README.md")

    def test_unversioned_link(self):
        self.plant("CHECKLISTS.md", replace_once("[DM1/R1]", "[R1]"))
        self.red("unversioned    CHECKLISTS.md")

    def test_unversioned_bare_citation(self):
        self.plant("AGENTS.md", replace_once("(DM1/F5, DM1/A4)", "(F5, DM1/A4)"))
        self.red("unversioned    AGENTS.md")

    def test_wrong_version(self):
        self.plant("CHECKLISTS.md", replace_once("[DM1/R1]", "[DM2/R1]"))
        self.red("wrong version  CHECKLISTS.md")

    def test_unknown_dm1_id(self):
        self.plant("CHECKLISTS.md", replace_once("[DM1/R1]", "[DM1/R99]"))
        self.red("unknown ID     CHECKLISTS.md")

    def test_citation_pointing_at_the_wrong_file(self):
        self.plant("CHECKLISTS.md", replace_once("[DM1/R1](guide/04-requirements.md#r1)", "[DM1/R1](guide/05-design.md#d1)"))
        self.red("wrong file     CHECKLISTS.md")

    def test_unknown_ref_of_this_guide(self):
        self.plant("README.md", replace_once("## Contents", "[x](https://github.com/wmacevoy/data-mining/blob/nope/README.md)\n\n## Contents"))
        self.red("unknown ref    README.md")

    def test_principle_with_no_basis(self):
        self.plant("guide/01-purpose.md", sub_once(r"\n\*(New|Reframes [^\n]*)\.\*\n", "\n\n"))
        self.red("no basis       guide/01-purpose.md")

    def test_reframes_with_no_link(self):
        self.plant("guide/02-unknowing.md", sub_once(r"\*Reframes [^\n]*\.\*", "*Reframes nothing.*"))
        self.red("no SE1 link    guide/02-unknowing.md")

    def test_anchor_that_disagrees_with_its_id(self):
        self.plant("guide/01-purpose.md", replace_once('<a id="p1">', '<a id="p01">'))
        self.red("anchor         guide/01-purpose.md")

    def test_id_defined_twice(self):
        self.plant("guide/01-purpose.md", replace_once("</a>DM1/P2.", "</a>DM1/P1."))
        self.red("P1 defined twice")

    def test_se1_link_at_another_release(self):
        # REQ1/PIN-1
        self.plant("guide/01-purpose.md", sub_once(r"(\[SE1/[A-Z]\d+\]\([^)]*blob/)v\d+\.\d+/", r"\1v1.0/"))
        self.red("SE1 link       guide/01-purpose.md")

    def test_unknown_se1_id(self):
        self.plant("guide/01-purpose.md", sub_once(r"\[SE1/([A-Z])\d+\]", r"[SE1/\g<1>99]"))
        self.red("unknown SE1 ID guide/01-purpose.md")

    def test_unknown_se1_id_in_a_bare_citation(self):
        self.plant("AGENTS.md", replace_once("(SE1/W12)", "(SE1/W99)"))
        self.red("unknown SE1 ID AGENTS.md")

    def test_readme_index_out_of_date(self):
        self.plant("README.md", replace_once("[**DM1/U11**](guide/02-unknowing.md#u11) Whether", "[**DM1/U11**](guide/02-unknowing.md#u11) If"))
        self.red("README index out of date")

    def test_readme_map_out_of_date(self):
        self.plant("README.md", sub_once(r"- \[SE1/W13\][^\n]*\n", ""))
        self.red("README SE1 map out of date")

    def test_title_number(self):
        self.plant("guide/01-purpose.md", replace_once("# 1. ", "# 3. "))
        self.red("title number   guide/01-purpose.md")

    def test_chapter_label(self):
        self.plant("guide/02-unknowing.md", replace_once("[3. Feedback]", "[3. Feedbacks]"))
        self.red("chapter label  guide/02-unknowing.md")

    def test_llms_missing_file(self):
        self.plant("llms.txt", replace_once("main/ADOPT.md", "main/NOPE.md"))
        self.red("missing file   llms.txt")

    def test_llms_se1_not_pinned(self):
        # REQ1/PIN-1
        self.plant("llms.txt", sub_once(r"software-engineering/v\d+\.\d+/", "software-engineering/main/"))
        self.red("SE1 not pinned llms.txt")

    def test_llms_not_a_raw_link(self):
        self.plant("llms.txt", replace_once("https://raw.githubusercontent.com/wmacevoy/data-mining/main/ADOPT.md", "https://example.com/x"))
        self.red("not a raw link llms.txt")

    def test_principle_count(self):
        self.plant("CHANGELOG.md", sub_once(r"· (\d+) principles", "· 1 principles"))
        self.red("principle count CHANGELOG.md says 1")

    def test_changelog_without_a_release_is_reported_not_a_crash(self):
        self.plant("CHANGELOG.md", lambda text: re.sub(r"(?m)^## v\d+(?:\.\d+)+ — (?:unreleased|\d{4}-\d{2}-\d{2})$", "## soon", text))
        self.red('CHANGELOG.md has no "## vX.Y')

    def test_agents_label_behind_the_newest_release(self):
        self.plant("AGENTS.md", sub_once(r"from \*\*v\d+\.\d+\*\*", "from **v0.9**"))
        self.red("stale release  AGENTS.md: says it is from v0.9")

    def test_copy_linking_to_another_ref(self):
        self.plant("AGENTS.md", sub_once(r"data-mining/blob/v\d+\.\d+/CHANGELOG\.md", "data-mining/blob/v9/CHANGELOG.md"))
        self.red("stale release  AGENTS.md: links to v9")

    def test_copy_citing_an_id_its_release_lacks(self):
        self.plant("AGENTS.md", replace_once("(SE1/W12)", "(DM1/U99)"))
        self.red("ahead of label AGENTS.md: cites DM1/U99")

    def test_a_handoff_note_keeps_its_links(self):
        # REQ1/PIN-1: a finished note names the release it was written against, on purpose.
        self.plant("archive/HANDOFF-migrate-to-SE1-v1.3.md", replace_once("# Handoff", f"[SE1/U7]({SE_BLOB}v1.0/guide/02-unknowing.md#u7)\n\n# Handoff"))
        self.assertEqual(self.check().returncode, 0)

    def test_print_gives_what_readme_must_contain(self):
        r = self.check("--print")
        self.assertIn("- [**DM1/U10**](guide/02-unknowing.md#u10)", r.stdout)
        self.assertIn("- [SE1/W13](", r.stdout)

    def test_index_escapes_a_pipe_in_a_title(self):
        # The site's renderer reads an unescaped | in a list item as a table, as it did DM1/U8's index line.
        r = self.check("--print")
        self.assertIn("People ask Prob(result \\| me)", r.stdout)


class TestVendoredSe1(Repo):
    def test_no_vendored_se1_is_a_problem(self):
        readme = os.path.join(self.repo, "vendor", "se1", "README.md")
        os.rename(readme, readme + ".gone")
        try:
            r = self.check()
        finally:
            os.rename(readme + ".gone", readme)
        self.assertEqual(r.returncode, 1)
        self.assertIn("vendored SE1   vendor/se1 has no README.md", r.stdout)
        self.assertIn("NOT checked (no vendor/se1)", r.stdout)

    def test_vendored_at_another_release_is_a_problem(self):
        # REQ1/PIN-1: passing at another release says nothing about the pinned one.
        self.plant("vendor/se1/AGENTS.md", sub_once(r"This copy is from \*\*v[0-9.]+\*\*", "This copy is from **v1.0**"))
        self.red("vendored SE1   vendor/se1 is v1.0")

    def test_an_edited_copy_is_a_problem(self):
        self.plant("docs/SE1-AGENTS.md", replace_once("# AGENTS.md", "# AGENTS.md, edited"))
        self.red("vendored SE1   docs/SE1-AGENTS.md differs")

    def test_an_edited_se_py_is_a_problem(self):
        self.plant("se.py", replace_once("import ", "import  "))
        self.red("vendored SE1   se.py differs")


class TestPatchRelease(Repo):
    """The rules that read this repository's own tags: v1.0 tagged, then v1.0.1 on top."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.git("init", "-q")
        path = os.path.join(cls.repo, "CHANGELOG.md")
        with open(path, encoding="utf-8", newline="") as fh:
            text = fh.read()
        with open(path, "w", encoding="utf-8", newline="") as fh:
            text = re.sub(r"(?ms)\A(.*?)^## v.*?(?=^## v1\.0 — 2026-11-03$)", r"\1## v1.0.1 — 2026-11-04\n\n", text)
            # The fixture's newest section states the count the chapters define today, whatever it was at v1.0.
            count = sum(open(os.path.join(cls.repo, "guide", f), encoding="utf-8").read().count("</a>DM1/") for f in os.listdir(os.path.join(cls.repo, "guide")))
            fh.write(re.sub(r"· [0-9]+ principles", f"· {count} principles", text, count=1))
        # Whatever the guide's newest release is, this fixture is v1.0 with v1.0.1 on top.
        for name in ["AGENTS.md"] + ["templates/" + t for t in os.listdir(os.path.join(cls.repo, "templates"))]:
            path = os.path.join(cls.repo, name)
            with open(path, encoding="utf-8", newline="") as fh:
                text = fh.read()
            text = re.sub(r"(wmacevoy/data-mining/(?:blob|tree))/v\d+\.\d+", r"\1/v1.0", text)
            text = re.sub(r"from \*\*v\d+\.\d+\*\*", "from **v1.0**", text, count=1)
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(text)
        cls.git("add", "-A")
        cls.git("commit", "-qm", "v1.0")
        cls.git("tag", "v1.0")

    def test_a_patch_that_changes_nothing_guarded_passes(self):
        r = self.check()
        self.assertEqual(r.returncode, 0, r.stdout)

    def test_a_patch_may_not_change_a_principle(self):
        self.plant("guide/01-purpose.md", replace_once("\n> **Ask:**", "\nChanged.\n\n> **Ask:**"))
        self.red("patch release  v1.0.1 changed P")

    def test_a_patch_may_not_change_a_copy(self):
        self.plant("AGENTS.md", lambda t: t + "\nA new rule.\n")
        self.red("patch release  v1.0.1 changed AGENTS.md")

    def test_a_patch_needs_its_minors_tag(self):
        self.git("tag", "-d", "v1.0")
        try:
            self.red("patch release  v1.0.1 needs its minor's tag v1.0")
        finally:
            self.git("tag", "v1.0")


class TestPortability(unittest.TestCase):
    """REQ1/PORT-1: developing this guide works on Windows, macOS, and Linux with Python and git alone."""

    def read(self, name):
        with open(os.path.join(GUIDE, name), encoding="utf-8") as fh:
            return fh.read()

    def test_ci_runs_every_part_of_the_full_check_on_all_three(self):
        workflow = self.read(".github/workflows/check.yml")
        for runner in ("ubuntu-latest", "macos-latest", "windows-latest"):
            self.assertIn(runner, workflow)
        full_check = re.search(r"\*\*The full check:\*\* `([^`]+)`", self.read("CONTRIBUTING.md")).group(1)
        parts = [part.strip() for part in full_check.split("&&")]
        self.assertGreater(len(parts), 1)
        for part in parts:
            self.assertIn(f"run: {part}", workflow)

    def test_the_tools_start_no_program_but_git(self):
        for name in ("dm.py", "tools/check.py", "tools/test_check.py", "tools/test_dm.py"):
            programs = set(re.findall(r"""\.?run\(\[\s*"([^"]+)\"""", self.read(name)))
            self.assertLessEqual(programs, {"git"}, name)


if __name__ == "__main__":
    unittest.main()
