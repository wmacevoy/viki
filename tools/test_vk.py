"""test_vk.py -- vk.py against scratch git repositories (REQ3/SCM-1 to SCM-5).

Each repository is built here with fixed names and dates, so a run repeats
exactly. Prints PASS/FAIL lines in the form core/test/gate.py reads.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
VK = [sys.executable, os.path.join(HERE, "vk.py")]
failed = 0


class Scratch:
    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="vk-test-")
        self.tick = 0
        self.store = os.path.join(self.root, "diary.db")

    def env(self):
        self.tick += 1
        when = f"2026-11-09T10:{self.tick:02d}:00+00:00"
        e = dict(os.environ, HOME=self.root, GIT_CONFIG_NOSYSTEM="1",
                 GIT_AUTHOR_NAME="Tester", GIT_AUTHOR_EMAIL="t@example.invalid",
                 GIT_COMMITTER_NAME="Tester", GIT_COMMITTER_EMAIL="t@example.invalid",
                 GIT_AUTHOR_DATE=when, GIT_COMMITTER_DATE=when)
        return e

    def git(self, repo, *args):
        subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True, env=self.env())

    def repo(self, name):
        d = os.path.join(self.root, name)
        os.makedirs(d)
        self.git(d, "init", "-q", "-b", "main")
        return d

    def write(self, repo, path, text):
        p = os.path.join(repo, path)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)

    def commit(self, repo, msg):
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-q", "-m", msg)

    def vk(self, *args):
        r = subprocess.run(VK + ["--store", self.store, "--plaintext", *args], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def close(self):
        shutil.rmtree(self.root)


def check(cond, what, out=""):
    global failed_now
    if not cond:
        print(f"  {what}")
        if out:
            print("    " + out.strip().replace("\n", "\n    "))
        failed_now = True


def run(rid, name, fn):
    global failed, failed_now
    failed_now = False
    s = Scratch()
    try:
        fn(s)
    except Exception as e:   # a crash is a failure, said plainly
        check(False, f"raised {type(e).__name__}: {e}")
    finally:
        s.close()
    print(f"{'FAIL' if failed_now else 'PASS'} {rid} {name}")
    failed += failed_now


def two_branches(s):
    """main and topic diverge, each changing shared.txt; returns the repo."""
    a = s.repo("alpha")
    s.write(a, "shared.txt", "base line\n")
    s.write(a, "doomed.txt", "deleted later\n")
    s.commit(a, "root")
    s.git(a, "checkout", "-q", "-b", "topic")
    s.write(a, "shared.txt", "topic line\n")
    s.commit(a, "topic change")
    s.git(a, "checkout", "-q", "main")
    s.write(a, "shared.txt", "main line\n")
    os.remove(os.path.join(a, "doomed.txt"))
    s.commit(a, "main change")
    return a


def t_scm1(s):
    a = two_branches(s)
    s.git(a, "tag", "-a", "v1", "-m", "first")
    rc, out = s.vk("ingest-git", a)
    check(rc == 0 and "3 commits read" in out, "first ingest reads 3 commits", out)
    check(" 0 assertions added" not in out, "first ingest adds rows", out)
    rc, out = s.vk("ingest-git", a)
    check(rc == 0 and " 0 assertions added" in out, "an unchanged repository adds nothing", out)
    rc, out = s.vk("grep", "topic line")
    check(rc == 0 and "git:alpha@" in out and ":shared.txt" in out, "a branch's file version is held", out)
    rc, out = s.vk("coverage")
    check("refs/tags/v1" in out, "a tag is read", out)


def t_scm2(s):
    a, b = s.repo("alpha"), s.repo("beta")
    s.write(a, "notes.md", "common phrase in alpha\n")
    s.commit(a, "alpha one")
    s.write(b, "docs/plan.md", "common phrase in beta\nonly-in-beta marker\n")
    s.commit(b, "beta one")
    s.vk("ingest-git", a)
    s.vk("ingest-git", b)
    rc, out = s.vk("grep", "only-in-beta")
    check(rc == 0 and "git:beta@" in out and ":docs/plan.md" in out, "a hit names repo, commit and path", out)
    check("git:alpha@" not in out, "no hit from the other repository", out)
    rc, out = s.vk("grep", "common phrase")
    check(out.count("file  git:") == 2, "one search returns hits from both", out)
    rc, out = s.vk("grep", "beta one")
    check(rc == 0 and "checkin  git:beta@" in out, "a commit message is searchable", out)


def t_scm3(s):
    a = two_branches(s)
    s.vk("ingest-git", a)
    rc, out = s.vk("heads", "alpha")
    check("forked" in out, "two diverged branches are two heads", out)
    s.git(a, "merge", "-q", "topic", "-m", "merge", "-X", "ours")
    s.vk("ingest-git", a)
    rc, out = s.vk("heads", "alpha")
    check(out.startswith("alpha: one head,"), "a merge commit leaves one head", out)   # not "more than one head"
    # the path both branches changed: one current version again, the merge's
    import vkstore as vk
    # The grants say who is entitled; a reader who did not grant the ingester sees none of its rows (R-4a).
    st = vk.Store(s.store, b"vk.ingest.git", vk.ORIGINATE | vk.SUPERSEDE | vk.READ)
    try:
        code, body = st.current(b"alpha/shared.txt")
        check(code == vk.OK and body == b"main line\n", f"shared.txt is one version after the merge (status {code})")
        code, _ = st.current(b"alpha/doomed.txt")
        check(code == vk.OK, f"a deleted path has one current state (status {code})")
    finally:
        st.close()


def t_scm4(s):
    a = s.repo("alpha")
    s.write(a, "x.txt", "present\n")
    s.commit(a, "one")
    s.vk("ingest-git", a)
    rc, out = s.vk("coverage")
    check(rc == 0 and "alpha:" in out and "refs/heads/main" in out, "coverage names the repository and its refs", out)
    rc, out = s.vk("grep", "present", "--repo", "gamma")
    check(rc == 3 and out.startswith("blind"), "a repository not ingested is blind", out)
    rc, out = s.vk("grep", "absent text", "--repo", "alpha")
    check(rc == 1 and "coverage:" in out, "searched and not found is its own answer", out)
    rc, out = s.vk("grep", "present")
    check("coverage: grep, exhaustive" in out and "alpha (1 refs)" in out, "a hit's coverage names what was searched", out)


def t_scm5(s):
    a = s.repo("alpha")
    s.write(a, "keep.txt", "kept\n")
    s.write(a, "data/Holdout/seed.txt", "planted-holdout-secret\n")
    s.write(a, "vendor/lib.c", "planted-vendored-code\n")
    s.write(a, ".vikiignore", "vendor\n")
    s.commit(a, "one")
    rc, out = s.vk("ingest-git", a)
    check(rc == 0 and "1 holdout paths refused" in out, "the holdout path is refused and counted", out)
    check("1 ignored" in out, "the ignore rule is applied and counted", out)
    rc, out = s.vk("grep", "planted-holdout-secret")
    check(rc == 1, "holdout content is not in the diary", out)
    rc, out = s.vk("grep", "planted-vendored-code")
    check(rc == 1, "ignored content is not in the diary", out)
    rc, out = s.vk("grep", "kept")
    check(rc == 0, "everything else is", out)


def main():
    sys.path.insert(0, HERE)
    run("SCM-1", "a repository's history goes in once, every branch and tag", t_scm1)
    run("SCM-2", "one search covers every ingested repository and names each hit", t_scm2)
    run("SCM-3", "heads are current check-ins; a merge heals the fork", t_scm3)
    run("SCM-4", "coverage names repositories and refs; an unknown one is blind", t_scm4)
    run("SCM-5", "holdout paths are refused whatever the rules; ignored paths stay out", t_scm5)
    print(f"{failed} of 5 failed")
    return failed != 0


if __name__ == "__main__":
    sys.exit(main())
