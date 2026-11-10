"""dedupe.py DIARY -- how much each way of cutting bodies into chunks would store.

Stored = distinct chunk bytes + 32 bytes per chunk reference per version (a
body is its list of chunk hashes) + 48 bytes per distinct chunk (its row).
Text bodies only: no NUL, valid UTF-8. Grouped by kind of file.

Written before the run, what counts against "structural dedupes code better":
on code, the structural cut stores no less than line-anchored content-defined
chunking, once references are counted.
"""
import hashlib
import os
import sqlite3
import sys

REF, ROW = 32, 48


def whole(b):
    return [b]


def lines_of(b):
    out = b.split(b"\n")
    return [ln + b"\n" for ln in out[:-1]] + ([out[-1]] if out[-1] else [])


def fixed40(b):
    ls = lines_of(b)
    return [b"".join(ls[i:i + 40]) for i in range(0, len(ls), 40)]


def per_line(b):
    return lines_of(b)


def cdc_lines(b, mask=15):
    """Content-defined, on line boundaries: cut after a line whose hash ends in 4 zero bits (avg 16 lines)."""
    out, cur = [], []
    for ln in lines_of(b):
        cur.append(ln)
        if hashlib.blake2b(ln, digest_size=4).digest()[-1] & mask == 0:
            out.append(b"".join(cur))
            cur = []
    if cur:
        out.append(b"".join(cur))
    return out


GEAR = [int.from_bytes(hashlib.blake2b(bytes([i]), digest_size=8).digest(), "big") for i in range(256)]


def cdc_bytes(b, avg_bits=10, lo=256, hi=8192):
    """Gear-hash content-defined chunking on bytes (FastCDC-like), average ~1 KB."""
    out, start, h, mask = [], 0, 0, (1 << avg_bits) - 1
    M = (1 << 64) - 1
    for i, c in enumerate(b):
        h = ((h << 1) + GEAR[c]) & M
        n = i - start + 1
        if (n >= lo and (h >> 40) & mask == 0) or n >= hi:
            out.append(b[start:i + 1])
            start, h = i + 1, 0
    if start < len(b):
        out.append(b[start:])
    return out


def structural(b, md=False):
    """Top-level blocks: cut before a line with no leading space that follows a blank
    line (a C or Python function, a paragraph), and in markdown before every heading."""
    out, cur, prev_blank = [], [], False
    for ln in lines_of(b):
        top = ln[:1] not in (b" ", b"\t", b"\n", b"\r", b"}", b")", b"") and not ln.startswith(b"#include")
        heading = md and ln.startswith(b"#")
        if cur and ((top and prev_blank) or heading):
            out.append(b"".join(cur))
            cur = []
        cur.append(ln)
        prev_blank = ln.strip() == b""
    if cur:
        out.append(b"".join(cur))
    return out


def paragraphs(b, md=False):
    """Finer structure: cut at every blank line (statement groups inside functions too)."""
    out, cur, prev_blank = [], [], False
    for ln in lines_of(b):
        if cur and prev_blank and ln.strip():
            out.append(b"".join(cur))
            cur = []
        cur.append(ln)
        prev_blank = ln.strip() == b""
    if cur:
        out.append(b"".join(cur))
    return out


def capped(b, md=False, cap=40):
    """Structural, with any block longer than cap lines cut content-defined inside."""
    out = []
    for blk in structural(b, md):
        if blk.count(b"\n") > cap:
            out.extend(cdc_lines(blk))
        else:
            out.append(blk)
    return out


def sections(b, md=False, cap=40):
    """Markdown by headings only; any section over cap lines cut content-defined inside.
    Code falls back to capped structural."""
    if not md:
        return capped(b, md)
    out, cur = [], []
    for ln in lines_of(b):
        if cur and ln.startswith(b"#"):
            out.append(b"".join(cur))
            cur = []
        cur.append(ln)
    if cur:
        out.append(b"".join(cur))
    res = []
    for blk in out:
        res.extend(cdc_lines(blk) if blk.count(b"\n") > cap else [blk])
    return res


CODE = (".c", ".h", ".py", ".sh", ".js", ".ts", ".cpp", ".tcl", ".ps1", ".cmake", ".txt")


def group(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".md":
        return "markdown"
    if ext in CODE[:-1] or path.endswith("CMakeLists.txt"):
        return "code"
    return "other text"


def main(db_path):
    db = sqlite3.connect(db_path)
    rows = db.execute("SELECT CAST(akey AS TEXT), body FROM assertion"
                      " WHERE kind=CAST('file' AS BLOB) AND body IS NOT NULL").fetchall()
    groups = {}
    for path, body in rows:
        if b"\0" in body:
            continue
        try:
            body.decode("utf-8")
        except UnicodeDecodeError:
            continue
        groups.setdefault(group(path), []).append((path, body))
    methods = [("whole body", whole), ("40-line windows", fixed40), ("CDC bytes ~1KB", cdc_bytes),
               ("CDC lines ~16", cdc_lines), ("structural", structural), ("structural, capped 40", capped),
               ("paragraphs", paragraphs), ("sections, capped 40", sections), ("per line", per_line)]
    for g in ("code", "markdown", "other text"):
        items = groups.get(g, [])
        raw = sum(len(b) for _, b in items)
        print(f"\n## {g}: {len(items)} versions of {len({p for p, _ in items})} files, {raw/1e6:.2f} MB as written")
        print(f"| cut | chunks/version | median lines | p90 lines | distinct MB | refs MB | rows MB | stored MB | vs whole |")
        print(f"|---|---|---|---|---|---|---|---|---|")
        base = None
        for name, fn in methods:
            seen, refs, sizes = {}, 0, []
            for path, b in items:
                md = path.endswith(".md")
                cs = fn(b, md) if fn in (structural, capped, paragraphs, sections) else fn(b)
                refs += len(cs)
                for c in cs:
                    sizes.append(c.count(b"\n"))
                    seen.setdefault(hashlib.sha256(c).digest(), len(c))
            distinct = sum(seen.values())
            stored = distinct + REF * refs + ROW * len(seen) if name != "whole body" else distinct
            base = base or stored
            sizes.sort()
            print(f"| {name} | {refs/len(items):.1f} | {sizes[len(sizes)//2]} | {sizes[len(sizes)*9//10]} | {distinct/1e6:.2f} | {REF*refs/1e6:.2f} | "
                  f"{ROW*len(seen)/1e6:.2f} | {stored/1e6:.2f} | {stored/base:.2f} |")


if __name__ == "__main__":
    main(sys.argv[1])
