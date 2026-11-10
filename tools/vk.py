#!/usr/bin/env python3
"""vk.py -- a host over viki_store.h for a sprawled project (REQ3/SCM, DOGFOOD_v3.md).

  vk.py keygen KEYFILE
  vk.py --store DIARY --keyfile KEYFILE ingest-git REPO_DIR [--name NAME]
  (every command takes --keyfile KEYFILE, or --plaintext to say an unencrypted
  diary was meant; one of the two is required)
  vk.py --store DIARY --plaintext ingest-git REPO_DIR [--name NAME]
  vk.py --store DIARY --plaintext grep TEXT [--repo NAME] [--all] [--k N]
  vk.py --store DIARY --plaintext find WORDS [--repo NAME] [--all] [--k N]
  vk.py --store DIARY --plaintext outline [--repo NAME] [--judge sonnet|haiku] [--history] [--limit N]
  vk.py --store DIARY --keyfile KEY ingest-transcript TRANSCRIPT.jsonl [--session ID] [--hook]
  vk.py --store DIARY --keyfile KEY forget-chat SESSION [--from N] [--to M]
  vk.py --store DIARY --plaintext coverage
  vk.py --store DIARY --plaintext heads NAME

Exit status: 0 found or done, 1 searched and not found, 3 blind (Q-2), 2 error.
grep is exact bytes and exhaustive; find is bm25, ranked, best first. Each hit
is marked current, superseded, or one head of a fork (Q-4), and superseded
versions are hidden unless --all, with the count hidden said.

Git keeps the working trees; the diary mirrors them. The mapping:
  file      akey "<repo>/<path>", body the bytes. A new version supersedes
            the version(s) of that path in the commit's parents.
  file.gone the same akey with no body: the path was deleted.
  checkin   akey "<repo>": supersedes its parents' check-ins, so the heads of
            a repository are its current check-ins, and two branches are a fork.
  label     akey "<repo>/<ref>": the commit a ref pointed at when read.
  ingest    akey "ingest/<repo>": what was read, at which refs, under which rules.
Every row is authored by the host's principal: git's author is a claim, kept
in the check-in's body, not in the frame.
"""
import argparse
import collections
import datetime
import fnmatch
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vkstore as vk  # noqa: E402

PRINCIPAL = b"vk.ingest.git"
RIGHTS = vk.ORIGINATE | vk.SUPERSEDE | vk.READ
# The judges whose outlines this host writes and reads (OL-2): one principal per model, so a reader
# chooses by author and two models' outlines stand side by side.
JUDGES = {"sonnet": ("claude-sonnet-5-5", b"vk.judge.sonnet"),
          "haiku": ("claude-haiku-5-5", b"vk.judge.haiku")}
GRANTS = [(principal, RIGHTS) for _, principal in JUDGES.values()]
# Conversations (SCM-6, SCM-7) are written by their own principal, which may also forget them.
CHAT = b"vk.ingest.chat"
CHAT_RIGHTS = RIGHTS | vk.ERASE
GRANTS.append((CHAT, CHAT_RIGHTS))
# The librarian (Warren, 2026-11-10): a model that writes questions and answers about each file, so a search
# in a reader's own words finds it. One principal per model, as the judges.
LIBRARIANS = {"haiku": ("claude-haiku-5-5", b"vk.librarian.haiku"),
              "sonnet": ("claude-sonnet-5-5", b"vk.librarian.sonnet")}
GRANTS += [(principal, RIGHTS) for _, principal in LIBRARIANS.values()]


# ---- the host's clock and the world (SHAPE-6: core reads neither) ----

def now_utc():
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True).stdout


class Blobs:
    """One `git cat-file --batch` process for every blob of a repository."""

    def __init__(self, repo):
        self.p = subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def read(self, sha):
        self.p.stdin.write(sha.encode() + b"\n")
        self.p.stdin.flush()
        header = self.p.stdout.readline().split()
        if len(header) != 3:
            raise vk.VkError(f"cat-file: {sha}: {header!r}")
        data = self.p.stdout.read(int(header[2]))
        self.p.stdout.read(1)   # the newline after the content
        return data

    def close(self):
        self.p.stdin.close()
        self.p.wait()


# ---- the key (N-7) ----

class KeyError_(Exception):
    pass


def read_keyfile(path):
    """The diary key as SQLCipher's raw form, x'<64 hex>', in a bytearray the
    store zeroes once applied. Refused, the way ssh refuses a private key,
    when anyone but the owner can read or write the file. Only a raw key is
    taken: the database key is full-entropy and gets no KDF (N-7), and a
    password is a different input on a different path (N-8, not built)."""
    try:
        st = os.stat(path)
    except OSError as e:
        raise KeyError_(f"cannot read {path}: {e.strerror}")
    if st.st_mode & 0o077:
        raise KeyError_(f"{path} is group/world accessible (mode {st.st_mode & 0o7777:04o}): refusing. chmod 600 {path}")
    with open(path, "rb") as f:
        raw = bytearray(f.read(1024))
    while raw and raw[-1:] in (b"\n", b"\r"):
        del raw[-1]
    ok = len(raw) == 64 and all(c in b"0123456789abcdefABCDEF" for c in raw)
    if not ok:
        raw[:] = bytes(len(raw))
        raise KeyError_(f"{path} does not hold a raw key (64 hex characters). A passphrase is not a "
                        "database key (N-7, N-8); make one with: vk.py keygen PATH")
    key = bytearray(b"x'") + raw + bytearray(b"'")
    raw[:] = bytes(len(raw))
    return key


def keygen(path):
    """A new full-entropy key, in a file created mode 600, never readable by others for a moment."""
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise KeyError_(f"{path} exists: refusing to overwrite a key")
    with os.fdopen(fd, "w") as f:
        f.write(os.urandom(32).hex() + "\n")


# ---- which paths go in (pure) ----

def is_holdout(path):
    """SCM-5: a component naming a holdout is refused, whatever the rules say."""
    return any("holdout" in part.lower() for part in path.split("/"))


def read_rules(repo):
    try:
        with open(os.path.join(repo, ".vikiignore")) as f:
            lines = [ln.strip() for ln in f]
    except FileNotFoundError:
        return []
    return [ln for ln in lines if ln and not ln.startswith("#")]


def ignored(path, rules):
    """The .vikiignore rule: no `/` matches any component; with `/`, the path or below."""
    parts = path.split("/")
    for rule in rules:
        if "/" in rule.strip("/"):
            r = rule.strip("/")
            if fnmatch.fnmatch(path, r) or path.startswith(r + "/"):
                return True
        elif any(fnmatch.fnmatch(part, rule.strip("/")) for part in parts):
            return True
    return False


def rank_of(epoch_seconds):
    return int(epoch_seconds) * 1000   # A-2b: milliseconds


# ---- ingest (SCM-1, SCM-3, SCM-4, SCM-5) ----

def commits(repo):
    """Parents before children, every branch, tag and remote; not the stash."""
    fmt = "%H%x00%P%x00%an <%ae>%x00%aI%x00%cI%x00%ct%x00%B%x1e"
    out = git(repo, "log", "--topo-order", "--reverse", "--branches", "--tags", "--remotes",
              f"--format={fmt}")
    for rec in out.split(b"\x1e"):
        rec = rec.lstrip(b"\n")
        if not rec:
            continue
        sha, parents, author, adate, cdate, ctime, msg = rec.split(b"\x00", 6)
        yield {"sha": sha.decode(), "parents": parents.decode().split(), "author": author.decode("utf-8", "replace"),
               "authored": adate.decode(), "committed": cdate.decode(), "ctime": int(ctime),
               "message": msg.decode("utf-8", "replace")}


def tree(repo, sha):
    """path -> blob sha, for blobs only; the number of submodules skipped."""
    out, n_sub = {}, 0
    for ent in git(repo, "ls-tree", "-r", "-z", "--full-tree", sha).split(b"\x00"):
        if not ent:
            continue
        meta, path = ent.split(b"\t", 1)
        mode, typ, obj = meta.split()
        if typ == b"blob":
            out[path.decode("utf-8", "surrogateescape")] = obj.decode()
        else:
            n_sub += 1
    return out, n_sub


def refs(repo):
    out = {}
    fmt = "%(refname)%00%(objectname)%00%(*objectname)"
    for line in git(repo, "for-each-ref", f"--format={fmt}", "refs/heads", "refs/tags", "refs/remotes").splitlines():
        name, obj, peeled = line.decode().split("\x00")
        out[name] = peeled or obj   # an annotated tag points at its commit
    return out


def ingest_git(store, repo, name):
    rules = read_rules(repo)
    blobs = Blobs(repo)
    maps, checkins, remaining = {}, {}, {}
    stats = {"commits": 0, "added": 0, "refused_holdout": set(), "ignored": set(), "submodules": 0}
    history = list(commits(repo))
    for c in history:
        for p in c["parents"]:
            remaining[p] = remaining.get(p, 0) + 1
    try:
        for c in history:
            store.exec("BEGIN")
            ingest_commit(store, repo, name, c, blobs, rules, maps, checkins, stats)
            store.exec("COMMIT")
            for p in c["parents"]:   # a parent's map is needed only until its last child
                remaining[p] -= 1
                if remaining[p] == 0:
                    maps.pop(p, None)
    finally:
        blobs.close()
    head_refs = refs(repo)
    for ref, sha in sorted(head_refs.items()):
        if sha not in checkins:
            continue   # a tag on a tree or blob: not a check-in
        akey = f"{name}/{ref}".encode()
        st, body = store.current(akey)
        if st == vk.OK and body == sha.encode():
            continue
        t = now_utc()
        _, added = store.put(b"label", akey, sha.encode(), ts=t.isoformat().encode(), rank=rank_of(t.timestamp()),
                             derived_from=[checkins[sha]], chain=True)
        stats["added"] += added
    record = {"repo": name, "dir": os.path.abspath(repo), "refs": head_refs, "rules": rules,
              "refused_holdout": len(stats["refused_holdout"]), "ignored": len(stats["ignored"]),
              "submodules_skipped": stats["submodules"], "commits": len(history)}
    body = json.dumps(record, sort_keys=True).encode()
    akey = f"ingest/{name}".encode()
    st, held = store.current(akey)
    if not (st == vk.OK and held == body):
        t = now_utc()
        store.put(b"ingest", akey, body, ts=t.isoformat().encode(), rank=rank_of(t.timestamp()), chain=True)
    names = sorted(set(ingested(store)) | {name})
    st, held = store.current(INDEX)
    index = json.dumps(names).encode()
    if not (st == vk.OK and held == index):
        t = now_utc()
        store.put(b"ingest", INDEX, index, ts=t.isoformat().encode(), rank=rank_of(t.timestamp()), chain=True)
    return stats, record


def ingest_commit(store, repo, name, c, blobs, rules, maps, checkins, stats):
    files, n_sub = tree(repo, c["sha"])
    stats["submodules"] = max(stats["submodules"], n_sub)
    kept = {}
    for path, blob in files.items():
        if is_holdout(path):
            stats["refused_holdout"].add(path)
        elif ignored(path, rules):
            stats["ignored"].add(path)
        else:
            kept[path] = blob
    parents = [maps[p] for p in c["parents"]]
    paths = set(kept)
    for m in parents:
        paths.update(m)
    ts, rank = c["committed"].encode(), rank_of(c["ctime"])
    new_map, changes = {}, {}
    for path in sorted(paths):
        blob = kept.get(path)
        before = {m[path] for m in parents if path in m}      # (version id, blob or None)
        ids = sorted({v for v, _ in before})
        if len(before) == 1 and next(iter(before))[1] == blob:
            new_map[path] = next(iter(before))                 # unchanged from every parent
            continue
        if not before and blob is None:
            continue
        kind = b"file" if blob is not None else b"file.gone"
        body = blobs.read(blob) if blob is not None else b""
        vid, added = store.put(kind, f"{name}/{path}".encode(), body, ts=ts, rank=rank,
                               reference=f"git:{name}@{c['sha']}:{path}".encode(), supersedes=ids)
        stats["added"] += added
        new_map[path] = (vid, blob)
        changes[path] = vid.hex() if blob is not None else None
    body = json.dumps({"git": c["sha"], "parents": c["parents"], "author": c["author"],
                       "authored": c["authored"], "message": c["message"], "changes": changes},
                      sort_keys=True).encode()
    cid, added = store.put(b"checkin", name.encode(), body, ts=ts, rank=rank,
                           reference=f"git:{name}@{c['sha']}".encode(),
                           supersedes=sorted(checkins[p] for p in c["parents"]))
    stats["added"] += added
    stats["commits"] += 1
    maps[c["sha"]] = new_map
    checkins[c["sha"]] = cid


# ---- outlines (OL), asked of a judge outside viki (SHAPE-7) ----

# The protocol measured in test/chunking/outline.py (FINDINGS 2026-11-10): a first outline by concept,
# not size; after that, the previous version and its outline, with a preference to keep it.
FIRST = """Below is one version of the file {path}, with each line numbered.

Outline it the way a reader would understand it: the concepts it contains, nested as deeply as the
content's own structure goes. For code, that might be groups of related functions or types, then
each function or type, then its distinct parts if it has any. For prose, sections, then
sub-sections, then topics. Every node is one concept. Do not split or merge anything because of its
length.

Answer with only JSON: a list of top-level nodes, each
{{"title": "<a short name for the concept>", "start": <first line>, "end": <last line>, "children": [<nodes>]}}.
Children lie inside their parent and do not overlap each other. Lines that belong to no concept, such
as blank lines between them, may be left out.

{numbered}"""

CARRY = """Below are two versions of the file {path}, each with numbered lines, and the outline of the
previous version. Write the outline of the new version.

Prefer to leave the outline the same: keep each node's title, its place in the outline and its
children, and move its line numbers to where that text now is. Change the outline only to reflect a
change in structure (a concept added, removed, moved, split or merged) or text rewritten whole cloth.
Do not split or merge anything because of its length.

Answer with only JSON: the whole outline of the new version, a list of top-level nodes, each
{{"title": "<a short name for the concept>", "start": <first line>, "end": <last line>, "children": [<nodes>]}}.
Line numbers are the new version's.

Outline of the previous version:
{outline}

Previous version:
{prev}

New version:
{new}"""



def text_lines(body):
    """The body's lines with their endings, or None if it is not text."""
    if b"\0" in body:
        return None
    try:
        body.decode("utf-8")
    except UnicodeDecodeError:
        return None
    return body.splitlines(keepends=True)


def numbered(lines):
    return "".join(f"{i+1}\t{ln.decode('utf-8')}" for i, ln in enumerate(lines))


def line_tree(answer, n_lines):
    """The judge's outline as nested (title, start, end, children) in lines, clipped to the file and
    to each parent, siblings that overlap dropped. None if no JSON list is in the answer."""
    m = re.search(r"\[.*\]", answer, re.S)
    if not m:
        return None
    try:
        raw = json.loads(m.group(0))
    except json.JSONDecodeError:
        return None

    def clean(nodes, lo, hi):
        out, last = [], lo - 1
        good = [n for n in nodes if isinstance(n, dict)]
        def first(n):
            try:
                return int(n.get("start", 0))
            except (TypeError, ValueError):
                return 0
        for n in sorted(good, key=first):
            try:
                s, e = int(n["start"]), int(n["end"])
            except (KeyError, TypeError, ValueError):
                continue
            s, e = max(s, lo, last + 1), min(e, hi)
            if s > e:
                continue
            kids = n.get("children") if isinstance(n.get("children"), list) else []
            out.append((str(n.get("title", "")).strip() or "(untitled)", s, e, clean(kids, s, e)))
            last = e
        return out
    return clean(raw if isinstance(raw, list) else [], 1, n_lines)


def to_bytes(tree, lines):
    """Line ranges (1-based, inclusive) as OL-1 byte ranges (lo, hi exclusive)."""
    starts = [0]
    for ln in lines:
        starts.append(starts[-1] + len(ln))
    return [{"title": t, "lo": starts[s - 1], "hi": starts[e], "children": to_bytes(k, lines)}
            for t, s, e, k in tree]


def to_lines(nodes, lines):
    """A stored outline back in lines, for the judge."""
    starts = [0]
    for ln in lines:
        starts.append(starts[-1] + len(ln))

    def line_of(off, end):
        import bisect
        i = bisect.bisect_right(starts, off) if not end else bisect.bisect_left(starts, off)
        return max(1, min(len(lines), i))
    return [{"title": n["title"], "start": line_of(n["lo"], False), "end": line_of(n["hi"], True),
             "children": to_lines(n.get("children") or [], lines)} for n in nodes]


def ask_judge(cmd, prompt):
    import shlex
    import tempfile
    with tempfile.TemporaryDirectory(prefix="vk-judge-") as empty:   # the judge sees only the prompt
        r = subprocess.run(shlex.split(cmd), input=prompt, capture_output=True, text=True, cwd=empty,
                           timeout=1200)
    if r.returncode:
        raise vk.VkError(f"judge failed ({r.returncode}): {r.stderr.strip()[:200]}")
    return r.stdout


def outline_answer(judge, ask, r, prev):
    """Ask the judge for r's outline: (tree in lines, lines), or why not. Calls nothing in the store, so
    several run at once. A file is never cut: how a large document divides is the judge's to say (Warren,
    2026-11-10: "do not split large files; that's the librarians task")."""
    lines = text_lines(r.body)
    if lines is None:
        return "not text"
    if not lines:
        return "empty"
    path = r.akey.decode("utf-8", "replace")
    if prev is not None:
        plines = text_lines(prev[0].body) or []
        prompt = CARRY.format(path=path, outline=json.dumps(to_lines(prev[1], plines), indent=1),
                              prev=numbered(plines), new=numbered(lines))
    else:
        prompt = FIRST.format(path=path, numbered=numbered(lines))
    tree = line_tree(ask(prompt), len(lines))
    return (tree, lines) if tree is not None else "unparseable"


def outline_write(store, judge, r, prev, got):
    """Write an answer from outline_answer: 'written', or why not."""
    if isinstance(got, str):
        return got
    tree, lines = got
    key = vk.outline_key(r.id, judge)
    body = json.dumps(to_bytes(tree, lines), ensure_ascii=False).encode()
    derived = [r.id] + ([prev[2]] if prev is not None else [])
    t = now_utc()
    try:
        store.put(b"outline", key, body, ts=t.isoformat().encode(), rank=rank_of(t.timestamp()),
                  reference=r.id, derived_from=derived)
    except vk.VkError as e:
        return f"refused: {e}"
    return "written"


def previous_outline(store, judge, r):
    """The judge's outline of a version r supersedes: (that version, its nodes, the outline's id)."""
    for pid in r.supersedes:
        p = store.get(pid)
        if p is None or p.kind != b"file":
            continue
        key = vk.outline_key(pid, judge)
        st, body = store.current(key)
        if st == vk.OK and body:
            return (p, json.loads(body), store.head_id(key))
    return None


def file_versions(store, repo, history):
    """File versions to outline: the current ones, or with history every version, in arrival order,
    which puts a version after the one it supersedes, so that one is outlined first."""
    prefix = repo.encode() + b"/" if repo is not None else b""
    out = []
    for r in store.walk():
        if r.kind != b"file" or r.erased or not r.akey.startswith(prefix):
            continue
        if not history and not store.standing(r.akey, r.id)[0]:
            continue
        out.append(r)
    return out


def skipped(akey, skip):
    """akey starts with one of the skip prefixes (a file or a directory, as 'repo/path')."""
    a = akey.decode("utf-8", "replace") if isinstance(akey, bytes) else akey
    return any(a == s or a.startswith(s.rstrip("/") + "/") for s in skip)


def cmd_outline(store, repo, judge_name, history, limit, cmd, skip=(), jobs=4):
    model, judge = JUDGES[judge_name]
    cmd = cmd or f"claude -p --model {model} --tools \"\" --no-session-persistence --output-format text"
    tally, due = collections.Counter(), []
    for r in file_versions(store, repo, history):
        if skipped(r.akey, skip):
            continue
        st, _ = store.current(vk.outline_key(r.id, judge))
        if st in (vk.OK, vk.FORKED):
            tally["already outlined"] += 1
            continue
        lines = text_lines(r.body)
        if not lines:   # nothing to ask: these do not count against --limit
            tally["not text" if lines is None else "empty"] += 1
            continue
        if limit is not None and len(due) >= limit:
            tally["left for a later run"] += 1
            continue
        due.append(r)
    calls = 0

    def counted(prompt):   # parts make more calls than files; this counts calls
        nonlocal calls
        calls += 1
        return ask_judge(cmd, prompt)

    def answer(r, prev):
        try:
            return outline_answer(judge, counted, r, prev)
        except (vk.VkError, subprocess.TimeoutExpired) as e:
            return f"judge failed: {e}"

    def record(r, prev, got):
        why = outline_write(store, judge, r, prev, got)
        if why != "written" and why not in ("not text", "empty"):
            print(f"  {r.akey.decode('utf-8', 'replace')}: {why}", file=sys.stderr)
        tally["refused" if why.startswith("refused") else "judge failed" if why.startswith("judge failed") else why] += 1

    if history or jobs <= 1:   # carrying forward needs each previous outline written first
        for r in due:
            prev = previous_outline(store, judge, r)
            record(r, prev, answer(r, prev))
    else:
        import concurrent.futures
        pre = [(r, previous_outline(store, judge, r)) for r in due]
        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            got = list(pool.map(lambda rp: answer(*rp), pre))
        for (r, prev), g in zip(pre, got):
            record(r, prev, g)
    print(f"outlines by {judge.decode()}: " + ", ".join(f"{n} {k}" for k, n in sorted(tally.items())) +
          f"; {calls} judge calls")
    return 0


# ---- conversations (SCM-6, SCM-7) ----

def chat_records(path):
    """The messages of a Claude Code transcript (JSONL), in order: the user and assistant records.
    Everything else in the file (attachments, modes, request metadata) is not a message."""
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("type") in ("user", "assistant") and isinstance(rec.get("message"), dict):
                yield rec


def render(rec):
    """A message as readable text, and how many thinking blocks it had with no readable text."""
    role = rec["message"].get("role") or rec["type"]
    content = rec["message"].get("content")
    parts, unreadable = [], 0
    if isinstance(content, str):
        parts.append(f"[{role}]\n{content}")
    for b in content if isinstance(content, list) else []:
        if not isinstance(b, dict):
            continue
        kind = b.get("type")
        if kind == "text":
            parts.append(f"[{role}]\n{b.get('text', '')}")
        elif kind == "thinking":
            if (b.get("thinking") or "").strip():
                parts.append(f"[thinking]\n{b['thinking']}")
            else:
                unreadable += 1       # kept by the transcript only as a signature (SCM-7)
        elif kind == "tool_use":
            parts.append(f"[tool_use {b.get('name', '?')}]\n" + json.dumps(b.get("input"), ensure_ascii=False, indent=1))
        elif kind == "tool_result":
            c = b.get("content")
            if isinstance(c, list):
                c = "\n".join(x.get("text", "[" + str(x.get("type")) + "]") for x in c if isinstance(x, dict))
            parts.append(f"[tool_result]\n{c if c is not None else ''}")
        else:
            parts.append(f"[{kind}]")
    side = " (sidechain)" if rec.get("isSidechain") else ""
    head = f"{rec.get('timestamp', '')} {role}{side}\n"
    return (head + "\n\n".join(parts)).encode("utf-8", "replace"), unreadable


def rank_of_iso(ts):
    try:
        return rank_of(datetime.datetime.fromisoformat(ts.replace("Z", "+00:00")).timestamp())
    except (ValueError, AttributeError):
        return 0


def ingest_transcript(store, path, session=None):
    """Append the messages not yet ingested to the session's log. How many were consumed is kept in
    its own chain, so a message that was forgotten is not ingested again."""
    recs = list(chat_records(path))
    session = session or next((r.get("sessionId") for r in recs if r.get("sessionId")), None) \
        or os.path.splitext(os.path.basename(path))[0]
    log, cursor = f"chat/{session}".encode(), f"chat-ingest/{session}".encode()
    st, held = store.current(cursor)
    done = json.loads(held)["records"] if st == vk.OK else 0
    if done > len(recs):
        raise vk.VkError(f"{path} has {len(recs)} messages but {done} were ingested: it is not append-only")
    added = unreadable = 0
    store.exec("BEGIN")
    try:
        for rec in recs[done:]:
            text, u = render(rec)
            store.append(log, b"chat", f"chat/{session}/{rec.get('uuid', done + added)}".encode(), text,
                         ts=str(rec.get("timestamp", "")).encode(), rank=rank_of_iso(rec.get("timestamp")),
                         reference=f"claude-code:{session}:{rec.get('uuid', '')}".encode())
            added += 1
            unreadable += u
        if added:
            t = now_utc()
            body = json.dumps({"session": session, "path": os.path.abspath(path), "records": len(recs)},
                              sort_keys=True).encode()
            store.put(b"ingest", cursor, body, ts=t.isoformat().encode(), rank=rank_of(t.timestamp()), chain=True)
        store.exec("COMMIT")
    except BaseException:
        store.exec("ROLLBACK")
        raise
    return session, added, len(recs), unreadable


def cmd_forget_chat(store, session, frm, to):
    log = f"chat/{session}".encode()
    span = store.log_span(log)
    if span is None:
        print(f"chat/{session}: nothing held")
        return 1
    frm, to = frm or span[0], to or span[1]
    t = now_utc()
    store.forget(log, frm, to, ts=t.isoformat().encode(), rank=rank_of(t.timestamp()))
    print(f"chat/{session}: messages {frm}-{to} forgotten by one range tombstone")
    return 0


# ---- reading ----

INDEX = b"ingest/index"


def ingested(store):
    """repo name -> its ingest record (SCM-4). The index chain names them."""
    st, body = store.current(INDEX)
    if st != vk.OK:
        return {}
    out = {}
    for name in json.loads(body):
        st, rec = store.current(f"ingest/{name}".encode())
        if st == vk.OK:
            out[name] = json.loads(rec)
    return out


def snippet(body, words):
    """The first line holding any of the words, compared without case."""
    low = body.lower()
    hits = [i for i in (low.find(w.lower()) for w in words) if i >= 0]
    at = min(hits) if hits else 0
    start = body.rfind(b"\n", 0, at) + 1
    end = body.find(b"\n", at)
    line = body[start:end if end >= 0 else len(body)]
    return line[:160].decode("utf-8", "replace").strip()


def standing_of(store, r):
    """Q-4, in words: current, superseded, or one head of a fork."""
    head, n = store.standing(r.akey, r.id)
    if not head:
        return "superseded"
    return "current" if n == 1 else f"head 1 of {n} (forked)"


def store_outlines(store):
    return getattr(store, "_outlines", b"")


def cmd_search(store, text, repo, method, show_all, k):
    seen = ingested(store)
    if repo is not None and repo not in seen:
        print(f"blind: repository {repo!r} was not ingested; ingested: {', '.join(sorted(seen)) or 'none'}")
        return 3
    method = "grep" if method == "grep" else "bm25"
    ids, cov = store.grep(text.encode()) if method == "grep" else store.bm25(text.encode())
    if ids is None:
        why = "it failed its self-check (Q-3)" if method == "grep" else "the diary has no bm25 index yet, or it failed its self-check (Q-3)"
        print(f"blind: {method}: {why}")
        return 3
    words = [text.encode()] if method == "grep" else [w for w in text.encode().split() if w]
    shown = matched = hidden = 0
    for i, node in ids:
        r = store.get(i)
        if r is None or r.kind in (b"ingest", b"label"):
            continue
        akey = r.akey.decode("utf-8", "replace")
        if repo is not None and not (akey == repo or akey.startswith(repo + "/")):
            continue
        matched += 1
        where = standing_of(store, r)
        if where == "superseded" and not show_all:
            hidden += 1
            continue
        if k is not None and shown >= k:
            continue
        shown += 1
        print(f"{r.kind.decode()}  {r.reference.decode('utf-8', 'replace') or akey}  {i.hex()[:12]}  [{where}]")
        body = r.body
        if node:   # OL-4: the smallest node of the chosen outline holding the match
            path, lo, hi, _ = node
            print(f"    in: {' > '.join(path) or '(outside every section)'}  bytes {lo}-{min(hi, len(body))}")
            body = body[lo:hi]
        print(f"    {snippet(body, words)}")
    scope = [repo] if repo is not None else sorted(seen)
    reach = "exhaustive" if cov["exhaustive"] else f"ranked, so not-found covers only these {matched} matches"
    whole = f", {cov['whole']} of them searched whole" if store_outlines(store) else ""
    print(f"coverage: {cov['method']}, {reach}, over {cov['searched']} assertions{whole}; repositories: "
          + ", ".join(f"{n} ({len(seen[n]['refs'])} refs)" for n in scope))
    print(f"shown {shown} of {matched} matches; {hidden} superseded versions hidden"
          + ("" if show_all else " (--all shows them)"))
    return 0 if matched else 1


# ---- vectors (Q-6) and the librarian (LIB) ----

# The search unit is the outline node (OL-4): a judge's concepts, not a size. A file with no outline by the
# chosen judge is one unit, whole. The chunking is part of the model's name (Q-6).
MODEL_DIR = os.environ.get("VIKI_MODEL_DIR", "/opt/viki-bin/model")
VEC_CHARS = 2000   # (invented) the most node text embedded; the model reads at most 256 wordpieces anyway


def is_text(body):
    try:
        body.decode("utf-8")
        return b"\0" not in body
    except UnicodeDecodeError:
        return False


def text_heads(store, kinds, repo=None, skip=()):
    """The current, readable text bodies of these kinds (a fork's heads all count)."""
    for r in store.walk():
        if r.kind not in kinds or not r.body or not is_text(r.body):
            continue
        akey = r.akey.decode("utf-8", "replace")
        if repo is not None and not (akey.startswith(repo + "/") or akey.startswith(f"qa/{repo}/")):
            continue
        if skip and skipped(akey.split("#", 1)[0].removeprefix("qa/"), skip):
            continue
        if store.standing(r.akey, r.id)[0]:
            yield r


def outline_nodes(store, judge, r):
    """The nodes of judge's current outline of file version r, the root (depth 0) first: dicts of path
    (titles from the root down), lo, hi, depth, and own (its byte segments less its children's). One root
    node holding everything if there is no outline."""
    root = {"path": [], "lo": 0, "hi": len(r.body), "depth": 0}
    st, body = store.current(vk.outline_key(r.id, judge))
    tops = json.loads(body) if st == vk.OK and body else []
    out = [root]

    def walk(nodes, path, depth):
        for n in nodes:
            d = {"path": path + [n["title"]], "lo": n["lo"], "hi": n["hi"], "depth": depth}
            out.append(d)
            d["kids"] = n.get("children") or []
            walk(d["kids"], d["path"], depth + 1)
    walk(tops, [], 1)
    root["kids"] = tops
    for d in out:
        segs, at = [], d["lo"]
        for k in sorted(d.pop("kids"), key=lambda k: k["lo"]):
            if k["lo"] > at:
                segs.append((at, k["lo"]))
            at = max(at, k["hi"])
        if at < d["hi"]:
            segs.append((at, d["hi"]))
        d["own"] = segs
    return out


def own_text(body, node):
    return b"".join(body[a:b] for a, b in node["own"]).decode("utf-8", "replace")


def node_text(body, node):
    """What is embedded for a node: its titles, then its own text."""
    return (" > ".join(node["path"]) + "\n" + own_text(body, node))[:VEC_CHARS]


def vector_model(emb, judge):
    """Nodes by judge's outline; a librarian's entry as one vector per question and one for its summary
    (embedded whole, an entry's questions were diluted by its summary, or cut at 256 wordpieces)."""
    return f"{emb.id}/outline:{judge.decode()}/per-question".encode()


def entry_lines(body):
    """(lo, hi) of each 'Q:' and 'Summary:' line of an entry."""
    out, at = [], 0
    for line in body.split(b"\n"):
        if line.startswith(b"Q: ") or line.startswith(b"Summary: "):
            out.append((at, at + len(line)))
        at += len(line) + 1
    return out


def cmd_embed(store, repo, judge, skip=(), librarian=LIBRARIANS["sonnet"][1]):
    """Vectors of every outline node of the current files, and of librarian's entries (no one else's)."""
    import vkembed
    emb = vkembed.Embedder(MODEL_DIR)
    model, n, rows, empty = vector_model(emb, judge), 0, 0, 0
    store.exec("BEGIN")
    try:
        for r in text_heads(store, {b"file", b"qa"}, repo, skip):
            if r.kind == b"qa" and r.author != librarian:
                continue
            if r.kind == b"qa":
                for lo, hi in entry_lines(r.body):
                    text = r.body[lo:hi].decode("utf-8", "replace").split(": ", 1)[1]
                    store.vector_put(r.id, model, lo, hi, emb.embed(text))
                    n += 1
            else:
                for node in outline_nodes(store, judge, r):
                    text = own_text(r.body, node)
                    if not text.strip():
                        empty += 1   # a node all of whose text is in its children: they carry it
                        continue
                    store.vector_put(r.id, model, node["lo"], node["hi"], emb.embed(node_text(r.body, node)))
                    n += 1
            rows += 1
        store.exec("COMMIT")
    except BaseException:
        store.exec("ROLLBACK")
        raise
    print(f"{n} vectors of {rows} bodies under {model.decode()}; {empty} nodes with no text of their own")
    return 0


LIBRARIAN_PROMPT = """You are the librarian of a software project's repository. Below is one file, with numbered lines,
and the numbered nodes of its outline (each a concept, with the lines it spans). For each node listed, write:
- "summary": two or three sentences on what that concept says, decides or warns about, naming its identifiers;
- "questions": three to six questions a developer would ask that this node answers. Word most of them as someone
  who has not read the file would ask, in their own terms (for example "how do I stop every machine redoing the
  same work?"), and some with the file's own names (for example "where is the embedding defined?").
Answer with only JSON: {{"<node number>": {{"summary": "...", "questions": ["...", "..."]}}, ...}}

File {path}:
{numbered}

Outline nodes:
{nodes}
"""
NODES_PER_CALL = 60   # (invented) a call answers for at most this many nodes; the file is always sent whole


def entry_body(path, node, lines_of, e):
    a, b = lines_of(node)
    qs = [" ".join(str(q).split()) for q in e.get("questions") or [] if str(q).strip()]
    return "\n".join([f"{path} lines {a}-{b}: {' > '.join(node['path']) or '(outside every section)'}",
                      f"Summary: {' '.join(str(e.get('summary', '')).split())}"] + [f"Q: {q}" for q in qs]).encode()


def has_entry(store, r, node):
    """The node already has an entry made from this version of its file."""
    head = store.head_id(b"qa/" + r.akey + f"#{node['lo']}-{node['hi']}".encode())
    return head is not None and r.id in (store.get(head).derived_from or [])


def cmd_librarian(store, repo, name, judge, limit, cmd, skip=(), jobs=4):
    """LIB-1: for each current file outlined by judge, one entry per node, written by the librarian model."""
    model, principal = LIBRARIANS[name]
    cmd = cmd or f"claude -p --model {model} --tools \"\" --no-session-persistence --output-format text"
    todo, no_outline = [], 0
    for r in text_heads(store, {b"file"}, repo, skip):
        okey = vk.outline_key(r.id, judge)
        oid = store.head_id(okey)
        if oid is None:
            no_outline += 1
            continue
        nodes = [n for n in outline_nodes(store, judge, r) if own_text(r.body, n).strip()]
        due = [n for n in nodes if not has_entry(store, r, n)]
        if due:
            todo.append((r, oid, due))
    if limit is not None:
        todo = todo[:limit]
    jobs_list = []
    for r, oid, due in todo:
        lines = text_lines(r.body)
        starts = [0]
        for ln in lines:
            starts.append(starts[-1] + len(ln))
        import bisect

        def lines_of(n, starts=starts):
            return bisect.bisect_right(starts, n["lo"]), max(1, bisect.bisect_left(starts, n["hi"]))
        for i in range(0, len(due), NODES_PER_CALL):
            jobs_list.append((r, oid, due[i:i + NODES_PER_CALL], lines, lines_of))

    def ask(job):
        r, oid, group, lines, lines_of = job
        listing = "\n".join(f"{k}. {' > '.join(n['path']) or '(text outside every section)'} (lines {lines_of(n)[0]}-"
                            f"{lines_of(n)[1]})" for k, n in enumerate(group, 1))
        prompt = LIBRARIAN_PROMPT.format(path=r.akey.decode(), numbered=numbered(lines), nodes=listing)
        try:
            return job, ask_judge(cmd, prompt)
        except (vk.VkError, subprocess.TimeoutExpired) as e:
            print(f"  {r.akey.decode()}: the librarian failed: {e}", file=sys.stderr)
            return job, ""
    import concurrent.futures
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        answers = list(pool.map(ask, jobs_list))
    written = missing = 0
    for (r, oid, group, lines, lines_of), answer in answers:
        m = re.search(r"\{.*\}", answer, re.S)
        try:
            got = json.loads(m.group(0)) if m else {}
        except json.JSONDecodeError:
            got = {}
        for k, n in enumerate(group, 1):
            e = got.get(str(k))
            if not isinstance(e, dict):
                missing += 1
                continue
            t = now_utc()
            store.put(b"qa", b"qa/" + r.akey + f"#{n['lo']}-{n['hi']}".encode(), entry_body(r.akey.decode(), n, lines_of, e),
                      ts=t.isoformat().encode(), rank=rank_of(t.timestamp()), reference=r.id.hex().encode(),
                      derived_from=[r.id, oid], chain=True)
            written += 1
    print(f"librarian {principal.decode()}: {written} node entries written, {missing} nodes unanswered, "
          f"{len(jobs_list)} calls over {len(todo)} files; {no_outline} files have no outline by {judge.decode()}")
    return 0


def node_key(store, id_, node, cache):
    """A hit as (file akey, lo, hi): the node it stands for. A librarian's entry stands for the node it
    describes; a hit with no node, a file with no outline, stands for the whole file. None if it is not a
    current file or an entry."""
    r = cache.get(id_) or cache.setdefault(id_, store.get(id_))
    if r is None:
        return None
    if r.kind == b"qa":
        path, span = r.akey[3:].decode().rsplit("#", 1)
        lo, hi = span.split("-")
        return (path, int(lo), int(hi))
    if r.kind != b"file" or not store.standing(r.akey, r.id)[0]:
        return None
    if node:
        return (r.akey.decode(), node[1], node[2])
    return (r.akey.decode(), 0, len(r.body))


def ranked(store, emb, text, method, cache, librarian=LIBRARIANS["sonnet"][1]):
    """The nodes one method returns for text, best first, each once, counting only librarian's entries
    (None: no entries at all). None if the method is blind (Q-2)."""
    hits, _ = store.bm25(text.encode()) if method == "bm25" else store.vector(emb.embed(text))
    if hits is None:
        return None
    out, seen = [], set()
    for id_, node in hits:
        r = cache.get(id_) or cache.setdefault(id_, store.get(id_))
        if r is not None and r.kind == b"qa" and r.author != librarian:
            continue
        k = node_key(store, id_, node, cache)
        if k and k not in seen:
            seen.add(k)
            out.append(k)
    return out


def fuse(lists, k=60):
    """Reciprocal-rank fusion by node, as src/ fused its legs (k=60)."""
    score = {}
    for lst in lists:
        for rank, key in enumerate(lst, 1):
            score[key] = score.get(key, 0) + 1 / (k + rank)
    return sorted(score, key=lambda key: -score[key])


def cmd_ask(store, text, k, judge, with_qa=True):
    import vkembed
    emb = vkembed.Embedder(MODEL_DIR)
    store.use_vectors(vector_model(emb, judge))
    store.use_outlines(judge)
    cache = {}
    lib = LIBRARIANS["sonnet"][1] if with_qa else None
    legs = {m: ranked(store, emb, text, m, cache, lib) for m in ("bm25", "vector")}
    blind = [m for m, lst in legs.items() if lst is None]
    for i, (f, lo, hi) in enumerate(fuse([lst for lst in legs.values() if lst])[:k], 1):
        print(f"[{i}] {f}  bytes {lo}-{hi}")
    print("legs: " + ", ".join(f"{m} {len(lst)} nodes" for m, lst in legs.items() if lst is not None)
          + (f"; blind: {', '.join(blind)} (vector: run embed)" if blind else ""))
    return 0 if any(legs.values()) else (3 if blind else 1)


def cmd_coverage(store):
    seen = ingested(store)
    if not seen:
        print("nothing ingested")
        return 1
    for name, rec in sorted(seen.items()):
        print(f"{name}: {rec['dir']}, {rec['commits']} commits, {len(rec['refs'])} refs, "
              f"{rec['refused_holdout']} holdout paths refused, {rec['ignored']} ignored, rules {rec['rules']}")
        for ref, sha in sorted(rec["refs"].items()):
            print(f"    {ref} {sha[:12]}")
    return 0


def cmd_heads(store, name):
    st, body = store.current(name.encode())
    if st == vk.NOTFOUND:
        print(f"blind: repository {name!r} was not ingested")
        return 3
    if st == vk.FORKED:
        print(f"{name}: forked -- more than one head")
        return 0
    print(f"{name}: one head, {json.loads(body)['git']}")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(prog="vk.py", description=__doc__.split("\n")[0])
    ap.add_argument("--store")
    ap.add_argument("--keyfile", help="the diary's raw key (mode 600); make one with keygen")
    ap.add_argument("--plaintext", action="store_true",
                    help="say an UNENCRYPTED diary was meant: a default nobody chose is refused")
    sub = ap.add_subparsers(dest="cmd", required=True)
    kg = sub.add_parser("keygen", help="write a new raw key to a new file, mode 600")
    kg.add_argument("path")
    g = sub.add_parser("ingest-git")
    g.add_argument("dir")
    g.add_argument("--name")
    for name, help_ in (("grep", "exact bytes, exhaustive"), ("find", "bm25, ranked")):
        s = sub.add_parser(name, help=help_)
        s.add_argument("text")
        s.add_argument("--repo")
        s.add_argument("--all", action="store_true", help="also show superseded versions")
        s.add_argument("--k", type=int, default=None if name == "grep" else 10)
        s.add_argument("--outlines", metavar="AUTHOR", help="return the nodes of this author's outlines (OL-4)")
    ol = sub.add_parser("outline", help="ask a judge for outlines of file versions (OL)")
    ol.add_argument("--repo")
    ol.add_argument("--judge", choices=sorted(JUDGES), default="sonnet")
    ol.add_argument("--history", action="store_true", help="every version, oldest first, not only current ones")
    ol.add_argument("--limit", type=int, help="at most this many judge calls this run")
    ol.add_argument("--judge-cmd", help="the command that answers a prompt on stdin (default: the claude CLI)")
    ol.add_argument("--skip", action="append", default=[], metavar="PATH", help="a file or directory, as repo/path")
    ol.add_argument("--jobs", type=int, default=4, help="judge calls in parallel (with --history, one at a time)")
    it = sub.add_parser("ingest-transcript", help="a conversation's messages into its log (SCM-7)")
    it.add_argument("path", nargs="?")
    it.add_argument("--session")
    it.add_argument("--hook", action="store_true", help="read transcript_path and session_id from a hook's JSON on stdin")
    fc = sub.add_parser("forget-chat", help="forget a session's messages with one range tombstone")
    fc.add_argument("session")
    fc.add_argument("--from", dest="frm", type=int)
    fc.add_argument("--to", type=int)
    em = sub.add_parser("embed", help="vectors of each outline node of the current files, and of the librarian's entries (Q-6)")
    em.add_argument("--repo")
    em.add_argument("--judge", choices=sorted(JUDGES), default="sonnet", help="whose outlines set the units")
    em.add_argument("--skip", action="append", default=[], metavar="PATH")
    lb = sub.add_parser("librarian", help="a model writes a summary and questions for every outline node (LIB-1)")
    lb.add_argument("--repo")
    lb.add_argument("--model", choices=sorted(LIBRARIANS), default="sonnet")
    lb.add_argument("--judge", choices=sorted(JUDGES), default="sonnet", help="whose outlines it works on")
    lb.add_argument("--limit", type=int, help="at most this many files this run")
    lb.add_argument("--judge-cmd", help="the command that answers a prompt on stdin (default: the claude CLI)")
    lb.add_argument("--skip", action="append", default=[], metavar="PATH", help="a file or directory the librarian must not read")
    lb.add_argument("--jobs", type=int, default=4, help="calls in parallel")
    ak = sub.add_parser("ask", help="bm25 and vector over outline nodes and the librarian's entries, fused by node")
    ak.add_argument("text")
    ak.add_argument("--k", type=int, default=10)
    ak.add_argument("--judge", choices=sorted(JUDGES), default="sonnet")
    ak.add_argument("--no-entries", action="store_true", help="leave the librarian's entries out")
    sub.add_parser("coverage")
    h = sub.add_parser("heads")
    h.add_argument("name")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "keygen":
            keygen(args.path)
            print(f"wrote a new key to {args.path} (mode 600)")
            return 0
        if not args.store:
            raise KeyError_("--store DIARY is required")
        if args.keyfile and args.plaintext:
            raise KeyError_("--keyfile and --plaintext contradict each other")
        if not args.keyfile and not args.plaintext:
            raise KeyError_(f"refusing to open {args.store} without a key: --keyfile PATH, "
                            "or --plaintext to say an unencrypted diary was meant")
        key = read_keyfile(args.keyfile) if args.keyfile else None
        if args.cmd in ("ingest-transcript", "forget-chat"):
            store = vk.Store(args.store, CHAT, CHAT_RIGHTS, key=key,
                             others=[(PRINCIPAL, RIGHTS)] + [g for g in GRANTS if g[0] != CHAT])
        elif args.cmd == "librarian":
            lib = LIBRARIANS[args.model][1]
            store = vk.Store(args.store, lib, RIGHTS, key=key,
                             others=[(PRINCIPAL, RIGHTS)] + [g for g in GRANTS if g[0] != lib])
        elif args.cmd == "outline":   # the judge writes as itself; the ingester and the judges are entitled
            judge = JUDGES[args.judge][1]
            others = [(PRINCIPAL, RIGHTS)] + [g for g in GRANTS if g[0] != judge]
            store = vk.Store(args.store, judge, RIGHTS, key=key, others=others)
        else:
            store = vk.Store(args.store, PRINCIPAL, RIGHTS, key=key, others=GRANTS)
    except (KeyError_, vk.VkError) as e:
        print(f"vk: {e}", file=sys.stderr)
        return 2
    try:
        if args.cmd == "ingest-git":
            name = args.name or os.path.basename(os.path.abspath(args.dir))
            store.bm25_enable()   # this diary is for searching: keep the ranked index (Q-6)
            stats, rec = ingest_git(store, args.dir, name)
            print(f"{name}: {stats['commits']} commits read, {stats['added']} assertions added, "
                  f"{rec['refused_holdout']} holdout paths refused, {rec['ignored']} ignored")
            return 0
        if args.cmd in ("grep", "find"):
            if args.outlines:
                store.use_outlines(args.outlines.encode())
            return cmd_search(store, args.text, args.repo, args.cmd, args.all, args.k)
        if args.cmd == "coverage":
            return cmd_coverage(store)
        if args.cmd == "embed":
            return cmd_embed(store, args.repo, JUDGES[args.judge][1], args.skip)
        if args.cmd == "librarian":
            return cmd_librarian(store, args.repo, args.model, JUDGES[args.judge][1], args.limit, args.judge_cmd,
                                 args.skip, args.jobs)
        if args.cmd == "ask":
            return cmd_ask(store, args.text, args.k, JUDGES[args.judge][1], not args.no_entries)
        if args.cmd == "outline":
            return cmd_outline(store, args.repo, args.judge, args.history, args.limit, args.judge_cmd, args.skip,
                               args.jobs)
        if args.cmd == "ingest-transcript":
            path, session = args.path, args.session
            if args.hook:
                hook = json.load(sys.stdin)
                path, session = hook.get("transcript_path"), session or hook.get("session_id")
            if not path:
                raise vk.VkError("no transcript: give a path, or --hook with transcript_path on stdin")
            store.bm25_enable()
            session, added, total, unreadable = ingest_transcript(store, path, session)
            print(f"chat/{session}: {added} messages added, {total} in the transcript, "
                  f"{unreadable} thinking blocks with no readable text not stored")
            return 0
        if args.cmd == "forget-chat":
            return cmd_forget_chat(store, args.session, args.frm, args.to)
        return cmd_heads(store, args.name)
    except (vk.VkError, subprocess.CalledProcessError, ValueError) as e:   # ValueError: a model that fails its pin
        print(f"vk: {e}", file=sys.stderr)
        return 2
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
