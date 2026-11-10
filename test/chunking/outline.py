"""outline.py DIARY OUT -- chunks as every level of a judged outline, not a size.

Warren, 2026-11-10: "instead of a size, can you ask for an outline? chunks can be
every outline level. i'm not sure size is important --- concept is."

The same versions as haiku.py (the last 12 of three files). Each goes to one
claude-haiku-5-5 call that sees only that version and is asked for an outline of
concepts, nested as deep as the content goes, with no size guidance. Answers are
cached in OUT, like haiku.py's.

Storage is a tree, because nested chunks overlap. A leaf stores its bytes. A
parent stores its children's hashes and the text between them, so an unchanged
section is one shared hash. Text no node covers becomes a gap leaf of the
parent around it. Cost: distinct leaf and gap bytes + 32 bytes per child
reference + 48 bytes per distinct node + 32 per version for its root.

Also measured:
  concepts kept  of each version's node titles, the fraction also in the previous
                 version (do the concepts persist?)
  node reuse     of each version's nodes, the fraction whose bytes were already stored

Written before the run, what counts against "concept chunks dedupe as well as
size-cut ones": stored bytes above CDC-lines' on two of the three files.
"""
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dedupe as D  # noqa: E402
import haiku as H   # noqa: E402

PROMPT = """Below is one version of the file {path}, with each line numbered.

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


def ask(path, body, out_file, empty_dir):
    if os.path.exists(out_file):
        return open(out_file).read()
    lines = D.lines_of(body)
    numbered = "".join(f"{i+1}\t{ln.decode('utf-8', 'replace')}" for i, ln in enumerate(lines))
    r = subprocess.run(["claude", "-p", "--model", H.MODEL, "--tools", "", "--no-session-persistence",
                        "--output-format", "text"],
                       input=PROMPT.format(path=path, numbered=numbered), capture_output=True, text=True,
                       cwd=empty_dir, timeout=900)
    with open(out_file, "w") as f:
        f.write(r.stdout)
    if r.returncode:
        with open(out_file + ".err", "w") as f:
            f.write(r.stderr)
    return r.stdout


def parse(answer, n_lines):
    """The outline as nested (title, start, end, children), clipped to the file and to the
    parent, overlaps between siblings dropped. None if no JSON list is found."""
    m = re.search(r"\[.*\]", answer, re.S)
    if not m:
        return None
    try:
        raw = json.loads(m.group(0))
    except json.JSONDecodeError:
        return None

    def clean(nodes, lo, hi):
        out, last = [], lo - 1
        for n in sorted((n for n in nodes if isinstance(n, dict)), key=lambda n: int(n.get("start", 0) or 0)):
            try:
                s, e = int(n["start"]), int(n["end"])
            except (KeyError, TypeError, ValueError):
                continue
            s, e = max(s, lo, last + 1), min(e, hi)
            if s > e:
                continue
            out.append((str(n.get("title", "")).strip(), s, e, clean(n.get("children") or [], s, e)))
            last = e
        return out
    return clean(raw, 1, n_lines)


def tree_nodes(lines, nodes, lo, hi):
    """Hash of the span lo..hi (1-based, inclusive) given its child nodes, recording every
    node into a list of (hash, own bytes, n child refs, title, depth). Returns the span's hash."""
    rec = []

    def build(nodes, lo, hi, title, depth):
        parts, refs = [], 0
        own = 0
        pos = lo
        kid_hashes = []
        for t, s, e, kids in nodes:
            if s > pos:                      # a gap: text no child covers
                gap = b"".join(lines[pos - 1:s - 1])
                g = hashlib.sha256(b"leaf" + gap).digest()
                rec.append((g, len(gap), 0, "", depth + 1))
                kid_hashes.append(g)
            kid_hashes.append(build(kids, s, e, t, depth + 1))
            pos = e + 1
        if not nodes:                        # a leaf: its own bytes
            data = b"".join(lines[lo - 1:hi])
            h = hashlib.sha256(b"leaf" + data).digest()
            rec.append((h, len(data), 0, title, depth))
            return h
        if pos <= hi:
            gap = b"".join(lines[pos - 1:hi])
            g = hashlib.sha256(b"leaf" + gap).digest()
            rec.append((g, len(gap), 0, "", depth + 1))
            kid_hashes.append(g)
        h = hashlib.sha256(b"node" + b"".join(kid_hashes)).digest()
        rec.append((h, own, len(kid_hashes), title, depth))
        return h

    root = build(nodes, lo, hi, "", 0)
    return root, rec


def main(db_path, out):
    os.makedirs(out, exist_ok=True)
    empty = os.path.join(out, "empty")
    os.makedirs(empty, exist_ok=True)
    db = sqlite3.connect(db_path)
    jobs, bodies = [], {}
    for path in H.FILES:
        vs = H.versions(db, path)
        bodies[path] = vs
        d = os.path.join(out, path.replace("/", "_"))
        os.makedirs(d, exist_ok=True)
        for i, b in enumerate(vs):
            jobs.append((path, b, os.path.join(d, f"{i:02d}.txt")))
    with ThreadPoolExecutor(4) as ex:
        answers = list(ex.map(lambda j: ask(j[0], j[1], j[2], empty), jobs))
    k = 0
    for path in H.FILES:
        vs = bodies[path]
        seen, refs, bad = {}, 0, 0
        kept_num = kept_den = reuse_num = reuse_den = 0
        depths, leaf_lines, n_nodes = [], [], []
        prev_titles = None
        for i, b in enumerate(vs):
            lines = D.lines_of(b)
            tree = parse(answers[k], len(lines))
            k += 1
            if tree is None:
                bad += 1
                tree = []
            root, rec = tree_nodes(lines, tree, 1, len(lines))
            titles = {t for _, _, _, t, _ in rec if t}
            if prev_titles is not None:
                kept_den += len(titles)
                kept_num += len(titles & prev_titles)
            prev_titles = titles
            n_nodes.append(sum(1 for r in rec if r[3]))
            for h, nbytes, nrefs, t, depth in rec:
                if i:
                    reuse_den += 1
                    reuse_num += h in seen
                if h not in seen:
                    seen[h] = (nbytes, nrefs)
                depths.append(depth)
                if nrefs == 0 and t:
                    leaf_lines.append(nbytes)
            refs += 1                         # the version's root
        stored = sum(nb + D.REF * nr + D.ROW for nb, nr in seen.values()) + D.REF * refs
        base = sum({hashlib.sha256(b).digest(): len(b) for b in vs}.values())
        leaf_lines.sort()
        print(f"\n## {path}: {len(vs)} versions, {bad} unparseable answers")
        print(f"outline: {sum(n_nodes)/len(vs):.0f} titled nodes per version, deepest level {max(depths)}, "
              f"median titled leaf {leaf_lines[len(leaf_lines)//2] if leaf_lines else 0} bytes")
        print(f"stored {stored/1e3:.0f} KB ({stored/base:.2f} of whole); node reuse {reuse_num/max(1, reuse_den):.2f}; "
              f"concepts kept from the previous version {kept_num/max(1, kept_den):.2f}")
    print()
    print("for comparison, from haiku.py on the same versions: CDC lines 74 / 92 / 155 KB, "
          "Haiku size-hinted carried forward 74 / 107 / 102 KB")


def show(db_path, out, path, i):
    """One version's outline, indented, to look at the output and not only its summary."""
    db = sqlite3.connect(db_path)
    b = H.versions(db, path)[i]
    tree = parse(open(os.path.join(out, path.replace("/", "_"), f"{i:02d}.txt")).read(), len(D.lines_of(b)))

    def walk(nodes, depth):
        for t, s, e, kids in nodes:
            print(f"{'  ' * depth}{t}  [{s}-{e}]")
            walk(kids, depth + 1)
    walk(tree or [], 0)


if __name__ == "__main__":
    if len(sys.argv) == 5:
        show(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]))
    else:
        main(sys.argv[1], sys.argv[2])


# ---- the update protocol: carry the outline forward, re-judge only what changed ----

UPDATE = """Below is a new version of the file {path}, with each line numbered, and the outline of its
previous version, already moved to this version's line numbers. Nodes marked "unchanged" cover only
lines whose text did not change: copy each of them exactly as given, with its title, lines and
children. Nodes marked "changed" contain edited, added or removed lines: outline those again, the way
a reader would understand them, nested as deeply as the content's own structure goes. Lines outside
every node are new: give them nodes too. Do not split or merge anything because of its length.

Answer with only JSON: the whole outline, a list of top-level nodes, each
{{"title": "<a short name for the concept>", "start": <first line>, "end": <last line>, "children": [<nodes>]}}.

Previous outline, moved to this version:
{draft}

File:
{numbered}"""


def move(tree, prev_lines, lines):
    """The previous outline on the new line numbers, each node marked changed if any of its
    lines changed. A node whose ends fall in changed text keeps the nearest mapped line."""
    import difflib
    sm = difflib.SequenceMatcher(None, prev_lines, lines, autojunk=False)
    where, changed_old = {}, set()
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        for k in range(i1, i2):
            if tag == "equal":
                where[k + 1] = j1 + (k - i1) + 1
            else:
                changed_old.add(k + 1)
                where[k + 1] = min(j1 + 1, max(1, len(lines)))
    inserted = any(t == "insert" for t, *_ in sm.get_opcodes())

    def walk(nodes):
        out = []
        for t, s, e, kids in nodes:
            ch = any(k in changed_old for k in range(s, e + 1))
            out.append({"title": t, "start": where.get(s, s), "end": where.get(e, e),
                        "status": "changed" if ch else "unchanged", "children": walk(kids)})
        return out
    return walk(tree), inserted


def update(path, prev_b, prev_tree, b, out_file, empty_dir):
    if os.path.exists(out_file):
        return open(out_file).read()
    pl, cl = D.lines_of(prev_b), D.lines_of(b)
    draft, _ = move(prev_tree, pl, cl)
    numbered = "".join(f"{i+1}\t{ln.decode('utf-8', 'replace')}" for i, ln in enumerate(cl))
    r = subprocess.run(["claude", "-p", "--model", H.MODEL, "--tools", "", "--no-session-persistence",
                        "--output-format", "text"],
                       input=UPDATE.format(path=path, draft=json.dumps(draft, indent=1), numbered=numbered),
                       capture_output=True, text=True, cwd=empty_dir, timeout=900)
    with open(out_file, "w") as f:
        f.write(r.stdout)
    if r.returncode:
        with open(out_file + ".err", "w") as f:
            f.write(r.stderr)
    return r.stdout


def run_file(db_path, out, path):
    db = sqlite3.connect(db_path)
    vs = H.versions(db, path)
    d = os.path.join(out, path.replace("/", "_"))
    u = os.path.join(out, "update", path.replace("/", "_"))
    os.makedirs(u, exist_ok=True)
    empty = os.path.join(out, "empty")
    os.makedirs(empty, exist_ok=True)
    trees = [parse(open(os.path.join(d, "00.txt")).read(), len(D.lines_of(vs[0]))) or []]
    for i in range(1, len(vs)):
        ans = update(path, vs[i - 1], trees[-1], vs[i], os.path.join(u, f"{i:02d}.txt"), empty)
        t = parse(ans, len(D.lines_of(vs[i])))
        trees.append(t if t is not None else trees[-1])   # an unparseable answer keeps the last outline
    return vs, trees


def measure(vs, trees):
    seen, refs, prev_titles = {}, 0, None
    kept_num = kept_den = reuse_num = reuse_den = 0
    for i, (b, tree) in enumerate(zip(vs, trees)):
        lines = D.lines_of(b)
        _, rec = tree_nodes(lines, tree, 1, len(lines))
        titles = {t for _, _, _, t, _ in rec if t}
        if prev_titles is not None:
            kept_den += len(titles)
            kept_num += len(titles & prev_titles)
        prev_titles = titles
        for h, nb, nr, t, dpt in rec:
            if i:
                reuse_den += 1
                reuse_num += h in seen
            seen.setdefault(h, (nb, nr))
        refs += 1
    stored = sum(nb + D.REF * nr + D.ROW for nb, nr in seen.values()) + D.REF * refs
    base = sum({hashlib.sha256(b).digest(): len(b) for b in vs}.values())
    return stored, stored / base, reuse_num / max(1, reuse_den), kept_num / max(1, kept_den)


def main_update(db_path, out):
    with ThreadPoolExecutor(3) as ex:
        results = list(ex.map(lambda p: (p, run_file(db_path, out, p)), H.FILES))
    for path, (vs, trees) in results:
        stored, frac, reuse, kept = measure(vs, trees)
        n = sum(len(tree_nodes(D.lines_of(b), t, 1, len(D.lines_of(b)))[1]) for b, t in zip(vs, trees)) / len(vs)
        print(f"{path}: outline carried forward: {stored/1e3:.0f} KB ({frac:.2f} of whole), node reuse {reuse:.2f}, "
              f"concepts kept {kept:.2f}, {n:.0f} nodes per version")


# ---- Warren's protocol (2026-11-10): "give it the previous version and outline and ask it to
# prefer to leave the outline the same, but change it to reflect changes in structure or whole
# cloth edits." The judge sees both versions and maps lines itself. Run with Haiku and Sonnet. ----

PREFER = """Below are two versions of the file {path}, each with numbered lines, and the outline of the
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

MODELS = {"haiku": "claude-haiku-5-5", "sonnet": "claude-sonnet-5-5"}


def as_json(tree):
    return [{"title": t, "start": s, "end": e, "children": as_json(k)} for t, s, e, k in tree]


def numbered(b):
    return "".join(f"{i+1}\t{ln.decode('utf-8', 'replace')}" for i, ln in enumerate(D.lines_of(b)))


def call(model, prompt, out_file, empty_dir):
    if os.path.exists(out_file):
        return open(out_file).read()
    r = subprocess.run(["claude", "-p", "--model", model, "--tools", "", "--no-session-persistence",
                        "--output-format", "text"], input=prompt, capture_output=True, text=True,
                       cwd=empty_dir, timeout=1200)
    with open(out_file, "w") as f:
        f.write(r.stdout)
    if r.returncode:
        with open(out_file + ".err", "w") as f:
            f.write(r.stderr)
    return r.stdout


def run_prefer(db_path, out, who, path):
    db = sqlite3.connect(db_path)
    vs = H.versions(db, path)
    model = MODELS[who]
    d = os.path.join(out, "prefer-" + who, path.replace("/", "_"))
    os.makedirs(d, exist_ok=True)
    empty = os.path.join(out, "empty")
    os.makedirs(empty, exist_ok=True)
    if who == "haiku":   # the same first outline as every other Haiku arm
        first = open(os.path.join(out, path.replace("/", "_"), "00.txt")).read()
    else:
        first = call(model, PROMPT.format(path=path, numbered=numbered(vs[0])), os.path.join(d, "00.txt"), empty)
    trees, bad = [parse(first, len(D.lines_of(vs[0]))) or []], 0
    for i in range(1, len(vs)):
        prompt = PREFER.format(path=path, outline=json.dumps(as_json(trees[-1]), indent=1),
                               prev=numbered(vs[i - 1]), new=numbered(vs[i]))
        t = parse(call(model, prompt, os.path.join(d, f"{i:02d}.txt"), empty), len(D.lines_of(vs[i])))
        bad += t is None
        trees.append(t if t is not None else trees[-1])
    return vs, trees, bad


def main_prefer(db_path, out):
    jobs = [(w, p) for w in ("haiku", "sonnet") for p in H.FILES]
    with ThreadPoolExecutor(6) as ex:
        results = list(ex.map(lambda j: (j, run_prefer(db_path, out, *j)), jobs))
    for (who, path), (vs, trees, bad) in results:
        stored, frac, reuse, kept = measure(vs, trees)
        n = sum(len(tree_nodes(D.lines_of(b), t, 1, len(D.lines_of(b)))[1]) for b, t in zip(vs, trees)) / len(vs)
        print(f"{who:6s} {path}: {stored/1e3:.0f} KB ({frac:.2f} of whole), node reuse {reuse:.2f}, "
              f"concepts kept {kept:.2f}, {n:.0f} nodes per version, {bad} unparseable")
