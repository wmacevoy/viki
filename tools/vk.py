#!/usr/bin/env python3
"""vk.py -- a host over viki_store.h for a sprawled project (REQ3/SCM, DOGFOOD_v3.md).

  vk.py --store DIARY --plaintext ingest-git REPO_DIR [--name NAME]
  vk.py --store DIARY --plaintext grep TEXT [--repo NAME]
  vk.py --store DIARY --plaintext coverage
  vk.py --store DIARY --plaintext heads NAME

Exit status: 0 found or done, 1 searched and not found, 3 blind (Q-2), 2 error.

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
import datetime
import fnmatch
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vkstore as vk  # noqa: E402

PRINCIPAL = b"vk.ingest.git"
RIGHTS = vk.ORIGINATE | vk.SUPERSEDE | vk.READ


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


def snippet(body, text):
    at = body.find(text)
    start = body.rfind(b"\n", 0, at) + 1
    end = body.find(b"\n", at)
    line = body[start:end if end >= 0 else len(body)]
    return line[:160].decode("utf-8", "replace").strip()


def cmd_grep(store, text, repo):
    seen = ingested(store)
    if repo is not None and repo not in seen:
        print(f"blind: repository {repo!r} was not ingested; ingested: {', '.join(sorted(seen)) or 'none'}")
        return 3
    ids, cov = store.grep(text.encode())
    if ids is None:
        print("blind: grep failed its self-check (Q-3)")
        return 3
    hits = 0
    for i in ids:
        r = store.get(i)
        if r is None or r.kind == b"ingest":
            continue
        akey = r.akey.decode("utf-8", "replace")
        if repo is not None and not (akey == repo or akey.startswith(repo + "/")):
            continue
        hits += 1
        print(f"{r.kind.decode()}  {r.reference.decode('utf-8', 'replace') or akey}  {i.hex()[:12]}")
        print(f"    {snippet(r.body, text.encode())}")
    scope = [repo] if repo is not None else sorted(seen)
    print(f"coverage: {cov['method']}, exhaustive, {cov['searched']} assertions; repositories: "
          + ", ".join(f"{n} ({len(seen[n]['refs'])} refs)" for n in scope))
    return 0 if hits else 1


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
    ap.add_argument("--store", required=True)
    ap.add_argument("--plaintext", action="store_true",
                    help="required: this build has no encryption, and a default nobody chose is refused")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("ingest-git")
    g.add_argument("dir")
    g.add_argument("--name")
    s = sub.add_parser("grep")
    s.add_argument("text")
    s.add_argument("--repo")
    sub.add_parser("coverage")
    h = sub.add_parser("heads")
    h.add_argument("name")
    args = ap.parse_args(argv)
    if not args.plaintext:
        print("vk: refusing to open an unencrypted diary without --plaintext", file=sys.stderr)
        return 2
    store = vk.Store(args.store, PRINCIPAL, RIGHTS)
    try:
        if args.cmd == "ingest-git":
            name = args.name or os.path.basename(os.path.abspath(args.dir))
            stats, rec = ingest_git(store, args.dir, name)
            print(f"{name}: {stats['commits']} commits read, {stats['added']} assertions added, "
                  f"{rec['refused_holdout']} holdout paths refused, {rec['ignored']} ignored")
            return 0
        if args.cmd == "grep":
            return cmd_grep(store, args.text, args.repo)
        if args.cmd == "coverage":
            return cmd_coverage(store)
        return cmd_heads(store, args.name)
    except (vk.VkError, subprocess.CalledProcessError) as e:
        print(f"vk: {e}", file=sys.stderr)
        return 2
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
