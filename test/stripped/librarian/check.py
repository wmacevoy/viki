"""check.py OUT TRIAL: a second, blind read of the librarian's unkeyed claims.

  OUT    evidence directory for the check, outside this repository
  TRIAL  a librarian trial directory: its UNKEYED-review.md, and r*/build/

For each row of TRIAL/UNKEYED-review.md, one sandboxed session (CHECK.txt, a
smaller model) reads the two notes, in a seeded random order, and nothing
else: not the claim, not how many runs made it, not the rest of the pile. It
picks a relation and quotes its evidence. Then this script re-finds every
quote in the note it names, and writes OUT/CHECKED-review.md beside the
original's empty "real?" column.

The checker's relation is not the key and never becomes it (QUESTION_v1
PIPE-1). It is there to shorten a person's reading. Nothing here scores, so
nothing here is a ledger line.
"""
import json
import os
import random
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
MODEL = "claude-haiku-5-5"
ROW = re.compile(r"^\| (\d+/\d+) \| (\w+) \| (\d+) (.*?) \| (\d+) (.*?) \| *\|$")


def parse_review(text):
    """The rows of an UNKEYED-review.md: runs, kind, and a and b as FINDINGS lines."""
    rows = []
    for line in text.splitlines():
        m = ROW.match(line)
        if m:
            rows.append({"runs": m[1], "kind": m[2], "a": int(m[3]), "a_text": m[4],
                         "b": int(m[5]), "b_text": m[6],
                         "a_role": role_of(m[4]), "b_role": role_of(m[6])})
    return rows


def role_of(text):
    """The review writes a correction block's text as "correction block"; anything else is an entry."""
    return "correction" if text.strip() == "correction block" else "entry"


def norm(s):
    return " ".join(s.split())


def quote_found(quote, note):
    """A quote counts only if it is in the note, up to whitespace. Empty is not found."""
    q = norm(quote)
    return bool(q) and q in norm(note)


def order_of(row):
    """Which of the row's a and b is shown as note-A. Seeded by the pair, so a rerun shows the same order."""
    return "ab" if random.Random(f"{row['a']}-{row['b']}").random() < 0.5 else "ba"


def as_row_relation(relation, order):
    """The checker's relation, rewritten from A/B into the row's a/b: 'a corrects b', 'same', ..."""
    names = {"A": order[0], "B": order[1]}
    m = re.fullmatch(r"([AB]) corrects ([AB])", relation.strip())
    if m:
        return f"{names[m[1]]} corrects {names[m[2]]}"
    return relation.strip()


def fragments_by_line(trial):
    """(FINDINGS line, role) -> fragment text, from every build in the trial. Only entries and
    correction blocks: an echo or a lie shares its source's line but is not the note the row means.
    An entry and its own struck-through correction share a line, so the role is part of the key."""
    texts = {}
    for key in sorted(trial.glob("r*/build/key.json")):
        k = json.loads(key.read_text())
        for fid, f in k["fragments"].items():
            if f["role"] not in ("entry", "correction"):
                continue
            at = (int(f["origin"].split(":")[1]), f["role"])
            text = (key.parent / "corpus" / f"{fid}.md").read_text()
            if texts.setdefault(at, text) != text:
                sys.exit(f"{at}: two builds disagree on its text")
    return texts


def run_one(out, cid):
    e = out / cid
    if (e / "evidence.json").exists():
        return f"{cid} kept"
    p = Path(f"/tmp/viki-chk-{cid}")
    shutil.rmtree(p, ignore_errors=True)
    p.mkdir(parents=True)
    for n in ("note-A.md", "note-B.md"):
        shutil.copy(e / n, p / n)
    claude = os.path.realpath(os.path.expanduser("~/.local/bin/claude"))
    env = dict(os.environ, SANDBOX_CLAUDE_DIR=os.path.expanduser("~/.local/share/claude"),
               SANDBOX_CREDENTIALS=os.path.expanduser("~/.claude/.credentials.json"),
               CLAUDE_CODE_DISABLE_AUTO_MEMORY="1")
    cmd = ["timeout", "900", str(REPO / "vendor/dm1-self-improvement/sandbox/bwrap-run.sh"), str(p),
           claude, "-p", (HERE / "CHECK.txt").read_text(), "--model", MODEL,
           "--setting-sources", "project", "--strict-mcp-config", "--no-session-persistence",
           "--disable-slash-commands", "--permission-mode", "dontAsk",
           "--allowedTools", "Read,Write,Edit,Bash", "--disallowedTools", "Agent,WebFetch,WebSearch,Skill,Glob,Grep",
           "--output-format", "stream-json", "--verbose"]
    with open(e / "transcript.jsonl", "w") as t, open(e / "stderr.txt", "w") as err:
        rc = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=t, stderr=err, env=env).returncode
    if (p / "evidence.json").exists():
        shutil.copy(p / "evidence.json", e / "evidence.json")
        return f"{cid} rc={rc}"
    return f"{cid} rc={rc}, no evidence.json"


def cost(transcript):
    for line in transcript.read_text().splitlines():
        try:
            o = json.loads(line)
        except ValueError:
            continue
        if o.get("type") == "result":
            return o.get("total_cost_usd") or 0.0
    return 0.0


def report(out, rows):
    lines = ["# Unkeyed claims, round 1, with a blind second read -- for Warren to judge", "",
             f"Checker: `{MODEL}`, `CHECK.txt` v1, one sandboxed session per row, shown the two notes only, in a",
             "seeded random order. Its relation is mapped back to the row's a and b. **found** counts its quotes",
             "that a script re-found verbatim in the note named; a quote not found is listed as *not found* and",
             "should not be believed. The checker is not the key (PIPE-1): only the real? column is.", ""]
    total = found = 0
    dollars = 0.0
    for i, row in enumerate(rows, 1):
        cid = f"c{i:02d}"
        e = out / cid
        order = (e / "order.txt").read_text().strip()
        notes = {"A": (e / "note-A.md").read_text(), "B": (e / "note-B.md").read_text()}
        name = {"A": order[0], "B": order[1]}
        if (e / "transcript.jsonl").exists():
            dollars += cost(e / "transcript.jsonl")
        claim = f"a corrects b" if row["kind"] == "corrects" else row["kind"]
        lines += [f"## {cid}: {row['runs']} runs claimed {row['kind']}, line {row['a']} -> {row['b']}", "",
                  f"- **a**, line {row['a']}: {row['a_text']}", f"- **b**, line {row['b']}: {row['b_text']}"]
        try:
            ev = json.loads((e / "evidence.json").read_text())
        except (OSError, ValueError) as x:
            lines += [f"- **checker:** no usable evidence.json ({type(x).__name__})", "- **real?** ", ""]
            continue
        said = as_row_relation(str(ev.get("relation", "")), order)
        verdict = "agrees" if said == claim else "differs"
        lines.append(f"- **checker:** {said} ({verdict} with the claim `{claim}`)")
        for s in ev.get("support", []):
            n = s.get("note")
            ok = n in notes and quote_found(s.get("quote", ""), notes[n])
            total += 1
            found += ok
            lines.append(f"  - for, from {name.get(n, '?')}{'' if ok else ' *(not found)*'}: \"{norm(s.get('quote', ''))}\"")
        for d in ev.get("disagreements", []):
            oka = quote_found(d.get("A", ""), notes["A"])
            okb = quote_found(d.get("B", ""), notes["B"])
            total += 2
            found += oka + okb
            lines.append(f"  - disagree: {d.get('what', '')}")
            lines.append(f"    - {name['A']}{'' if oka else ' *(not found)*'}: \"{norm(d.get('A', ''))}\"")
            lines.append(f"    - {name['B']}{'' if okb else ' *(not found)*'}: \"{norm(d.get('B', ''))}\"")
        if not ev.get("disagreements"):
            lines.append("  - disagree: none listed")
        lines += [f"- **doubts:** {norm(str(ev.get('doubts', '')))}", "- **real?** ", ""]
    lines[6:6] = [f"Quotes re-found: {found} of {total}. Cost: ${dollars:.2f}.", ""]
    (out / "CHECKED-review.md").write_text("\n".join(lines) + "\n")
    return found, total, dollars


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    out = Path(argv[1]).expanduser().resolve()
    trial = Path(argv[2]).expanduser().resolve()
    if out == REPO or REPO in out.parents:
        sys.exit(f"OUT must be outside the repository: {out}")
    rows = parse_review((trial / "UNKEYED-review.md").read_text())
    texts = fragments_by_line(trial)
    for i, row in enumerate(rows, 1):
        e = out / f"c{i:02d}"
        e.mkdir(parents=True, exist_ok=True)
        order = order_of(row)
        (e / "order.txt").write_text(order + "\n")
        for name, which in zip(("note-A.md", "note-B.md"), order):
            (e / name).write_text(texts[(row[which], row[which + "_role"])])
    with ThreadPoolExecutor(3) as pool:
        for msg in pool.map(lambda i: run_one(out, f"c{i:02d}"), range(1, len(rows) + 1)):
            print(msg, flush=True)
    found, total, dollars = report(out, rows)
    print(f"{len(rows)} rows; quotes re-found {found} of {total}; ${dollars:.2f}; {out / 'CHECKED-review.md'}")


if __name__ == "__main__":
    main(sys.argv)
