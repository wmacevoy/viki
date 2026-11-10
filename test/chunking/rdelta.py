"""rdelta.py DIARY -- reverse deltas: every head whole, every superseded version a delta.

Warren, 2026-11-10: "the storage should be reverse-diffed, yes? the latest is a
plain blob, earlier is recovered from that main one."

A file version that nothing supersedes is a head and is stored whole (a fork has
two). Each other version is stored as a line delta against the version that
supersedes it (the earliest, if several do), so reading it walks the chain from a
head. A delta keeps the bytes of the lines the older version has and its
successor lacks, plus 8 bytes per copy or insert op. Text versions only. Also
prints the longest chain, the cost of reading the oldest version back, and the
same totals compressed with zlib, against git's packed size for scale.

Written before the run, what counts against reverse deltas: storing more than
line-anchored CDC (dedupe.py) on code or markdown.
"""
import difflib
import os
import sqlite3
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dedupe as D  # noqa: E402

OP = 8


def delta(old, new):
    """What it costs to rebuild old from new: old's own lines, and the ops."""
    a, b = D.lines_of(old), D.lines_of(new)
    cost, payload = 0, []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        cost += OP
        if tag in ("replace", "delete", "insert"):
            seg = b"".join(a[i1:i2])
            cost += len(seg)
            payload.append(seg)
    return cost, b"".join(payload)


def main(db_path):
    db = sqlite3.connect(db_path)
    rows = db.execute("SELECT id, CAST(akey AS TEXT), body, seq FROM assertion"
                      " WHERE kind=CAST('file' AS BLOB) AND body IS NOT NULL").fetchall()
    text = {}
    for i, path, body, seq in rows:
        if b"\0" in body:
            continue
        try:
            body.decode("utf-8")
        except UnicodeDecodeError:
            continue
        text[i] = (path, body, seq)
    succ = {}
    for parent, child, cseq in db.execute("SELECT parent, child, child_seq FROM edge WHERE rel=0"):
        if parent in text and child in text:
            if parent not in succ or cseq < succ[parent][1]:
                succ[parent] = (child, cseq)
    depth_memo = {}

    def depth(i):
        n = 0
        while i in succ:
            i = succ[i][0]
            n += 1
        return n
    out = {}
    for i, (path, body, _) in text.items():
        g = D.group(path)
        o = out.setdefault(g, {"raw": 0, "heads": 0, "deltas": 0, "n": 0, "nd": 0, "maxdepth": 0,
                               "zraw": [], "zstore": []})
        o["raw"] += len(body)
        o["n"] += 1
        o["zraw"].append(body)
        if i in succ:
            c, payload = delta(body, text[succ[i][0]][1])
            o["deltas"] += c
            o["nd"] += 1
            o["zstore"].append(payload)
            o["maxdepth"] = max(o["maxdepth"], depth(i))
        else:
            o["heads"] += len(body)
            o["zstore"].append(body)
    cdc = {"code": 6.16, "markdown": 5.19, "other text": 0.93}
    print("| group | versions | as written MB | heads MB | deltas MB | stored MB | vs CDC lines | longest chain | zlib stored MB |")
    print("|---|---|---|---|---|---|---|---|---|")
    for g in ("code", "markdown", "other text"):
        o = out[g]
        stored = o["heads"] + o["deltas"]
        z = len(zlib.compress(b"".join(o["zstore"]), 6))
        print(f"| {g} | {o['n']} | {o['raw']/1e6:.2f} | {o['heads']/1e6:.2f} | {o['deltas']/1e6:.2f} | "
              f"{stored/1e6:.2f} | {stored/1e6/cdc[g]:.2f} | {o['maxdepth']} | {z/1e6:.2f} |")


if __name__ == "__main__":
    main(sys.argv[1])
