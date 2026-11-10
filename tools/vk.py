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

MAX_BYTES = 60000   # two versions of this must fit one call (Not yet answered 14)


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


def outline_one(store, judge, cmd, r, prev):
    """Ask for and write the outline of file version r. prev: (record, its outline nodes) to carry
    forward, or None. Returns 'written', 'exists', or why it was skipped."""
    key = vk.outline_key(r.id, judge)
    lines = text_lines(r.body)
    if lines is None:
        return "not text"
    if not lines:
        return "empty"
    path = r.akey.decode("utf-8", "replace")
    if prev is not None:
        plines = text_lines(prev[0].body) or []
        if len(r.body) + len(prev[0].body) > MAX_BYTES:
            return "too large"
        prompt = CARRY.format(path=path, outline=json.dumps(to_lines(prev[1], plines), indent=1),
                              prev=numbered(plines), new=numbered(lines))
    else:
        if len(r.body) > MAX_BYTES:
            return "too large"
        prompt = FIRST.format(path=path, numbered=numbered(lines))
    tree = line_tree(ask_judge(cmd, prompt), len(lines))
    if tree is None:
        return "unparseable"
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


def cmd_outline(store, repo, judge_name, history, limit, cmd):
    model, judge = JUDGES[judge_name]
    cmd = cmd or f"claude -p --model {model} --tools \"\" --no-session-persistence --output-format text"
    tally, asked = collections.Counter(), 0
    for r in file_versions(store, repo, history):
        st, _ = store.current(vk.outline_key(r.id, judge))
        if st in (vk.OK, vk.FORKED):
            tally["already outlined"] += 1
            continue
        if limit is not None and asked >= limit:
            tally["left for a later run"] += 1
            continue
        why = outline_one(store, judge, cmd, r, previous_outline(store, judge, r))
        if why in ("written", "unparseable") or why.startswith("refused"):
            asked += 1
        if why != "written" and why not in ("not text", "empty"):
            print(f"  {r.akey.decode('utf-8', 'replace')}: {why}", file=sys.stderr)
        tally["refused" if why.startswith("refused") else why] += 1
    print(f"outlines by {judge.decode()}: " + ", ".join(f"{n} {k}" for k, n in sorted(tally.items())) +
          f"; {asked} judge calls")
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
    it = sub.add_parser("ingest-transcript", help="a conversation's messages into its log (SCM-7)")
    it.add_argument("path", nargs="?")
    it.add_argument("--session")
    it.add_argument("--hook", action="store_true", help="read transcript_path and session_id from a hook's JSON on stdin")
    fc = sub.add_parser("forget-chat", help="forget a session's messages with one range tombstone")
    fc.add_argument("session")
    fc.add_argument("--from", dest="frm", type=int)
    fc.add_argument("--to", type=int)
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
        if args.cmd == "outline":
            return cmd_outline(store, args.repo, args.judge, args.history, args.limit, args.judge_cmd)
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
    except (vk.VkError, subprocess.CalledProcessError) as e:
        print(f"vk: {e}", file=sys.stderr)
        return 2
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
