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

    def vk_bare(self, *args):
        """vk.py with exactly these arguments: no --store or --plaintext added."""
        r = subprocess.run(VK + list(args), capture_output=True, text=True)
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
    with open(os.path.join(b, "blob.bin"), "wb") as f:   # bytes with NULs: a body is bytes, not a C string
        f.write(b"\x00\x01binary-marker\x00\xff" * 50)
    s.commit(b, "binary")
    s.vk("ingest-git", b)
    rc, out = s.vk("grep", "binary-marker")
    check(rc == 0 and ":blob.bin" in out, "a body holding NUL bytes is found", out)
    check(any(ln.startswith("    ") and "binary-marker" in ln for ln in out.splitlines()),
          "and its bytes read back whole, not cut at the first NUL", out)
    rc, out = s.vk("find", "binary-marker")
    check(rc == 0 and ":blob.bin" in out, "and is found ranked", out)


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


def t_q4(s):
    a = s.repo("gamma")
    s.write(a, "plan.md", "first draft of the plan\n")
    s.commit(a, "draft")
    s.write(a, "plan.md", "final plan\n")
    s.commit(a, "final")
    s.vk("ingest-git", a)
    rc, out = s.vk("grep", "first draft")
    check(rc == 0 and "shown 0 of 1 matches; 1 superseded versions hidden" in out, "a superseded version is hidden, and counted", out)
    rc, out = s.vk("grep", "first draft", "--all")
    check("[superseded]" in out and ":plan.md" in out, "--all shows it, marked superseded", out)
    rc, out = s.vk("grep", "final plan")
    check("[current]" in out, "the current version is marked current", out)
    b = two_branches(s)
    s.vk("ingest-git", b, "--name", "beta")
    rc, out = s.vk("grep", "line", "--repo", "beta")
    check(out.count("[head 1 of 2 (forked)]") == 2, "each side of a fork is marked a head of it", out)


def t_q6(s):
    rc, out = s.vk("find", "sensor")
    check(rc == 3 and out.startswith("blind: bm25"), "a diary with no index is blind", out)
    a = s.repo("alpha")
    s.write(a, "a.md", "the parking lot sensor went silent\n")
    s.write(a, "b.md", "sensor sensor sensor: the sensor log\n")
    s.write(a, "c.md", "nothing here\n")
    s.commit(a, "notes")
    s.vk("ingest-git", a)
    rc, out = s.vk("find", "sensor")
    files = [ln for ln in out.splitlines() if ln.startswith("file  ")]
    check(rc == 0 and len(files) == 2 and ":b.md" in files[0], "ranked: the dense mention first", out)
    check("ranked, so not-found covers only these" in out, "a ranked answer says what its not-found covers", out)
    rc, out = s.vk("find", "parking nothing")
    check(len([ln for ln in out.splitlines() if ln.startswith("file  ")]) == 2, "words are OR'd", out)
    rc, out = s.vk("find", "sensor", "--k", "1")
    check(len([ln for ln in out.splitlines() if ln.startswith("file  ")]) == 1 and "shown 1 of" in out, "--k truncates and says so", out)


def t_n7(s):
    a = s.repo("alpha")
    s.write(a, "x.txt", "keyed-marker text\n")
    s.commit(a, "one")
    key, other, loose = (os.path.join(s.root, n) for n in ("diary.key", "other.key", "loose.key"))
    rc, out = s.vk_bare("keygen", key)
    check(rc == 0 and (os.stat(key).st_mode & 0o777) == 0o600, "keygen writes a mode-600 key", out)
    rc, out = s.vk_bare("keygen", key)
    check(rc == 2 and "refusing to overwrite" in out, "keygen never overwrites a key", out)
    s.vk_bare("keygen", other)
    diary = os.path.join(s.root, "keyed.db")
    rc, out = s.vk_bare("--store", diary, "--keyfile", key, "ingest-git", a)
    check(rc == 0 and "1 commits read" in out, "a keyed diary is written", out)
    with open(diary, "rb") as f:
        head = f.read(4096)
    check(not head.startswith(b"SQLite format 3") and b"keyed-marker" not in head, "its file is not plaintext")
    with open(s.store, "wb"):
        pass
    s.vk("ingest-git", a)                         # the control: the same repository, --plaintext
    with open(s.store, "rb") as f:
        check(f.read(16) == b"SQLite format 3\x00", "the control diary is plaintext")
    rc, out = s.vk_bare("--store", diary, "--keyfile", key, "grep", "keyed-marker")
    check(rc == 0 and ":x.txt" in out, "the same key reads it back", out)
    rc, out = s.vk_bare("--store", diary, "--keyfile", other, "grep", "keyed-marker")
    check(rc == 2 and "wrong key, or not a diary" in out and "Traceback" not in out, "a wrong key is refused plainly", out)
    rc, out = s.vk_bare("--store", diary, "--plaintext", "grep", "keyed-marker")
    check(rc == 2 and "wrong key, or not a diary" in out, "--plaintext cannot read a keyed diary", out)
    rc, out = s.vk_bare("--store", diary, "grep", "keyed-marker")
    check(rc == 2 and "without a key" in out, "no key and no --plaintext is refused", out)
    with open(loose, "w") as f:
        f.write("ab" * 32 + "\n")
    os.chmod(loose, 0o644)
    fresh = os.path.join(s.root, "fresh.db")
    rc, out = s.vk_bare("--store", fresh, "--keyfile", loose, "coverage")
    check(rc == 2 and "group/world accessible" in out and not os.path.exists(fresh),
          "a key others can read is refused before any diary is made", out)
    os.chmod(loose, 0o600)
    with open(loose, "w") as f:
        f.write("correct horse battery staple\n")
    rc, out = s.vk_bare("--store", fresh, "--keyfile", loose, "coverage")
    check(rc == 2 and "does not hold a raw key" in out and "correct horse" not in out,
          "a passphrase is refused, and not echoed", out)


def t_ol4(s):
    a = s.repo("alpha")
    s.write(a, "notes.md", "# Alpha\nintro line\n# Beta\nthe marker-ol4 line\n")
    s.commit(a, "one")
    s.vk("ingest-git", a)
    import json
    import vkstore as vk
    st = vk.Store(s.store, b"vk.ingest.git", vk.ORIGINATE | vk.SUPERSEDE | vk.READ)
    try:
        (tid, _), = st.grep(b"marker-ol4")[0]
        body = st.get(tid).body
        cut = body.index(b"# Beta")
        outline = json.dumps([{"title": "Alpha", "lo": 0, "hi": cut},
                              {"title": "Beta", "lo": cut, "hi": len(body)}]).encode()
        st.put(b"outline", vk.outline_key(tid, b"vk.ingest.git"), outline, reference=tid, derived_from=[tid])
    finally:
        st.close()
    rc, out = s.vk("grep", "marker-ol4", "--outlines", "vk.ingest.git")
    check(rc == 0 and "in: Beta  bytes" in out and "1 of them searched whole" not in out, "a hit names its section", out)
    rc, out = s.vk("grep", "marker-ol4")
    check(rc == 0 and "in: " not in out, "without --outlines, the whole file", out)
    rc, out = s.vk("find", "marker-ol4", "--outlines", "vk.ingest.git")
    check(rc == 0 and "in: Beta" in out and "searched whole" in out, "find too, and coverage counts the whole", out)


FAKE_JUDGE = r"""
import json, os, re, sys
prompt = sys.stdin.read()
log = os.environ.get("VK_FAKE_LOG")
if log:
    with open(log, "a") as f:
        f.write("CARRY\n" if "Outline of the previous version" in prompt else "FIRST\n")
block = prompt.split("New version:\n", 1)[1] if "New version:\n" in prompt else prompt
lines = [ln for ln in block.splitlines() if re.match(r"^\d+\t", ln)]
nodes = []
for ln in lines:
    n, text = ln.split("\t", 1)
    if text.startswith("#"):
        nodes.append({"title": text.lstrip("# ").strip(), "start": int(n), "children": []})
for i, nd in enumerate(nodes):
    nd["end"] = nodes[i + 1]["start"] - 1 if i + 1 < len(nodes) else int(lines[-1].split("\t")[0])
print("Here is the outline:\n" + json.dumps(nodes))
"""


def t_ol_cmd(s):
    judge = os.path.join(s.root, "judge.py")
    with open(judge, "w") as f:
        f.write(FAKE_JUDGE)
    log = os.path.join(s.root, "judge.log")
    os.environ["VK_FAKE_LOG"] = log
    cmd = f"{sys.executable} {judge}"
    a = s.repo("alpha")
    s.write(a, "notes.md", "# Alpha\nfirst words\n# Beta\nsecond words\n")
    with open(os.path.join(a, "blob.bin"), "wb") as f:
        f.write(b"\x00\x01binary")
    s.commit(a, "one")
    s.write(a, "notes.md", "# Alpha\nfirst words\n# Beta\nsecond words\n# Gamma\nthe marker-gamma words\n")
    s.commit(a, "two")
    s.vk("ingest-git", a)
    rc, out = s.vk("outline", "--judge-cmd", cmd)
    check(rc == 0 and "1 written" in out and "1 not text" in out and "1 judge calls" in out,
          "the current versions are outlined; a binary one is not", out)
    rc, out = s.vk("outline", "--judge-cmd", cmd)
    check(rc == 0 and "1 already outlined" in out and "0 judge calls" in out, "a second run asks nothing", out)
    rc, out = s.vk("grep", "marker-gamma", "--outlines", "vk.judge.sonnet")
    check(rc == 0 and "in: Gamma" in out, "the judge's outline is searched", out)
    # with history, on a fresh diary: the old version first, then the new one carried forward
    s.store = os.path.join(s.root, "history.db")
    open(log, "w").close()
    s.vk("ingest-git", a)
    rc, out = s.vk("outline", "--history", "--judge-cmd", cmd)
    check(rc == 0 and "2 written" in out, "both versions are outlined", out)
    with open(log) as f:
        check(f.read().split() == ["FIRST", "CARRY"], "the first is outlined fresh, the second carried forward")
    import vkstore as vk
    st = vk.Store(s.store, b"vk.ingest.git", vk.READ, others=[(b"vk.judge.sonnet", vk.ORIGINATE | vk.SUPERSEDE | vk.READ)])
    try:
        outlines = [r for r in st.walk() if r.kind == b"outline"]
        check(len(outlines) == 2 and sorted(len(r.derived_from) for r in outlines) == [1, 2],
              "the carried outline names the one it carried (OL-3)")
    finally:
        st.close()
    s.store = os.path.join(s.root, "limit.db")
    s.vk("ingest-git", a)
    rc, out = s.vk("outline", "--history", "--limit", "1", "--judge-cmd", cmd)
    check(rc == 0 and "1 judge calls" in out and "1 left for a later run" in out, "--limit stops and says so", out)


def main():
    sys.path.insert(0, HERE)
    run("SCM-1", "a repository's history goes in once, every branch and tag", t_scm1)
    run("SCM-2", "one search covers every ingested repository and names each hit", t_scm2)
    run("SCM-3", "heads are current check-ins; a merge heals the fork", t_scm3)
    run("SCM-4", "coverage names repositories and refs; an unknown one is blind", t_scm4)
    run("SCM-5", "holdout paths are refused whatever the rules; ignored paths stay out", t_scm5)
    run("Q-4", "each hit says current, superseded, or one head of a fork", t_q4)
    run("Q-6", "find is bm25: ranked, OR'd, blind without an index", t_q6)
    run("N-7", "the diary is keyed with a raw key from a mode-600 file", t_n7)
    run("OL-4", "vk.py shows the section of the chosen outline a hit is in", t_ol4)
    run("OL-3", "vk.py outline asks a judge, carries outlines forward, and writes each once", t_ol_cmd)
    print(f"{failed} of 10 failed")
    return failed != 0


if __name__ == "__main__":
    sys.exit(main())
