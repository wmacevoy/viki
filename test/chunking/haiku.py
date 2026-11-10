"""haiku.py DIARY OUT -- does a judged chunker (claude-haiku-5-5) dedupe file history?

For each file below, its last N versions in commit order. Each version goes to
one Haiku call that sees only that version's numbered lines, so boundaries are
judged independently: the worst case for stability. Raw answers are kept in
OUT. Then the same storage cost as dedupe.py, against the rule-based cuts on
the same versions, and two stability figures:
  reuse    of each version's chunks, the fraction already stored by earlier versions
  stable   of boundaries on lines whose text is unchanged from the previous
           version, the fraction the judge kept

Written before the run, what counts against "a judge dedupes structurally":
its stored bytes on these files exceed CDC-lines', or its reuse is below CDC's.

Run 2026-11-10 against a diary of viki at 31fcae5 and the puck at 34b4cf3
(`vk.py ingest-git` of each into a fresh --plaintext diary). The 36 answers are
kept in test/chunking/haiku-out, and a run reuses an answer it finds, so with
those heads this regenerates with no calls. Later commits move the window of
the last N versions, and then the cached answers no longer match:

  python3 -I test/chunking/haiku.py DIARY test/chunking/haiku-out
"""
import json
import os
import re
import sqlite3
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dedupe as D  # noqa: E402

FILES = ["viki/src/viki.c", "puck/examples/esp/p4-puck/main/ble.c", "puck/docs/HANDOFF.md"]
N = 12
MODEL = "claude-haiku-5-5"
PROMPT = """Below is one version of the file {path}, with each line numbered.

Split it into chunks that a reader would treat as units: in code, a function, a struct, a group of
related declarations, or a header comment; in prose, a section or sub-section. Keep each chunk
between about 5 and 60 lines; split a longer unit at a natural seam inside it.

Answer with only a JSON array of the line numbers where chunks start, in increasing order, beginning
with 1. No other text.

{numbered}"""


def versions(db, path):
    rows = db.execute("SELECT body FROM assertion WHERE kind=CAST('file' AS BLOB) AND akey=CAST(? AS BLOB)"
                      " AND body IS NOT NULL ORDER BY seq", (path,)).fetchall()
    return [r[0] for r in rows][-N:]


def ask(path, body, out_file, empty_dir):
    if os.path.exists(out_file):
        return open(out_file).read()
    lines = D.lines_of(body)
    numbered = "".join(f"{i+1}\t{ln.decode('utf-8', 'replace')}" for i, ln in enumerate(lines))
    r = subprocess.run(["claude", "-p", "--model", MODEL, "--tools", "", "--no-session-persistence",
                        "--output-format", "text"],
                       input=PROMPT.format(path=path, numbered=numbered), capture_output=True, text=True,
                       cwd=empty_dir, timeout=600)
    with open(out_file, "w") as f:
        f.write(r.stdout)
    if r.returncode:
        with open(out_file + ".err", "w") as f:
            f.write(r.stderr)
    return r.stdout


def starts_of(answer, n_lines):
    m = re.search(r"\[[\d,\s]*\]", answer)
    if not m:
        return None
    s = sorted({int(x) for x in json.loads(m.group(0)) if 1 <= int(x) <= n_lines} | {1})
    return s


def cut(body, starts):
    lines = D.lines_of(body)
    edges = [s - 1 for s in starts] + [len(lines)]
    return [b"".join(lines[a:b]) for a, b in zip(edges, edges[1:]) if b > a]


def cost(chunk_lists):
    import hashlib
    seen, refs, reuse_num, reuse_den = {}, 0, 0, 0
    for i, cs in enumerate(chunk_lists):
        refs += len(cs)
        for c in cs:
            h = hashlib.sha256(c).digest()
            if i:
                reuse_den += 1
                reuse_num += h in seen
            seen.setdefault(h, len(c))
    stored = sum(seen.values()) + D.REF * refs + D.ROW * len(seen)
    return stored, reuse_num / max(1, reuse_den), refs / len(chunk_lists)


def stability(bodies, starts_list):
    """Of boundaries on lines whose text also opens a boundary-eligible line in the previous
    version (same line text present), how many the judge kept."""
    kept = tot = 0
    for prev_b, prev_s, b, s in zip(bodies, starts_list, bodies[1:], starts_list[1:]):
        pl, cl = D.lines_of(prev_b), D.lines_of(b)
        prev_open = {pl[i - 1] for i in prev_s if i - 1 < len(pl)}
        cur_lines = set(cl)
        for t in prev_open:
            if t in cur_lines and t.strip():
                tot += 1
                kept += any(cl[i - 1] == t for i in s if i - 1 < len(cl))
    return kept / max(1, tot)


def main(db_path, out):
    os.makedirs(out, exist_ok=True)
    empty = os.path.join(out, "empty")
    os.makedirs(empty, exist_ok=True)
    db = sqlite3.connect(db_path)
    jobs, bodies = [], {}
    for path in FILES:
        vs = versions(db, path)
        bodies[path] = vs
        d = os.path.join(out, path.replace("/", "_"))
        os.makedirs(d, exist_ok=True)
        for i, b in enumerate(vs):
            jobs.append((path, b, os.path.join(d, f"{i:02d}.txt")))
    with ThreadPoolExecutor(4) as ex:
        answers = list(ex.map(lambda j: ask(j[0], j[1], j[2], empty), jobs))
    k = 0
    for path in FILES:
        vs = bodies[path]
        starts_list, bad = [], 0
        for b in vs:
            s = starts_of(answers[k], len(D.lines_of(b)))
            k += 1
            if s is None:
                bad += 1
                s = [1]
            starts_list.append(s)
        md = path.endswith(".md")
        raw = sum(len(b) for b in vs)
        print(f"\n## {path}: {len(vs)} versions, {raw/1e3:.0f} KB as written, {bad} unparseable answers")
        print("| cut | chunks/version | stored KB | vs whole | reuse |")
        print("|---|---|---|---|---|")
        base = sum({__import__('hashlib').sha256(b).digest(): len(b) for b in vs}.values())
        rows = [("whole body", [[b] for b in vs]),
                ("CDC bytes ~1KB", [D.cdc_bytes(b) for b in vs]),
                ("CDC lines ~16", [D.cdc_lines(b) for b in vs]),
                ("sections/structural, capped 40", [D.sections(b, md) for b in vs]),
                ("Haiku, judged", [cut(b, s) for b, s in zip(vs, starts_list)])]
        for name, lists in rows:
            stored, reuse, per = cost(lists)
            if name == "whole body":
                stored = base
            print(f"| {name} | {per:.1f} | {stored/1e3:.0f} | {stored/base:.2f} | {reuse:.2f} |")
        print(f"Haiku boundary stability on unchanged lines: {stability(vs, starts_list):.2f}")




def carry_forward(bodies, starts_list):
    """The judge's boundaries carried through each diff: where text is unchanged the previous
    version's boundaries stand (mapped to new line numbers); only inside changed or inserted
    regions are this version's own judged boundaries used. No new calls: the judged answers are
    the ones already made."""
    import difflib
    out = [starts_list[0]]
    for prev_b, b, judged in zip(bodies, bodies[1:], starts_list[1:]):
        pl, cl = D.lines_of(prev_b), D.lines_of(b)
        prev = set(out[-1])
        new = {1}
        changed = set()
        for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, pl, cl, autojunk=False).get_opcodes():
            if tag == "equal":
                for k in range(i2 - i1):
                    if (i1 + k + 1) in prev:
                        new.add(j1 + k + 1)
            else:
                changed.update(range(j1 + 1, j2 + 1))
                if j2 > j1:
                    new.add(j1 + 1)          # a changed region starts a chunk
                    if j2 < len(cl):
                        new.add(j2 + 1)      # and the unchanged text after it starts another
        new |= {s for s in judged if s in changed}
        out.append(sorted(new))
    return out


def main2(db_path, out):
    db = sqlite3.connect(db_path)
    for path in FILES:
        vs = versions(db, path)
        d = os.path.join(out, path.replace("/", "_"))
        starts_list = [starts_of(open(os.path.join(d, f"{i:02d}.txt")).read(), len(D.lines_of(b))) or [1]
                       for i, b in enumerate(vs)]
        cf = carry_forward(vs, starts_list)
        stored, reuse, per = cost([cut(b, s) for b, s in zip(vs, cf)])
        base = sum({__import__('hashlib').sha256(b).digest(): len(b) for b in vs}.values())
        print(f"{path}: Haiku carried forward: {per:.1f} chunks/version, {stored/1e3:.0f} KB, "
              f"{stored/base:.2f} of whole, reuse {reuse:.2f}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
    print()
    main2(sys.argv[1], sys.argv[2])
