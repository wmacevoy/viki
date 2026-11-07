#!/usr/bin/env python3
"""test/stripped/stripped.py -- the stripped-structure test.

Take a document that DECLARES its structure (FINDINGS.md: newest first,
corrections as labelled block quotes, retractions struck through, pointers
that quote another entry's heading), remove the declarations, and keep them
as an answer key. What is left looks like a pile: fragments in no order,
nothing saying which one replaced which. A librarian reads the pile and says
which fragments correct, echo, or contradict each other; the scorer compares
that with the key.

Echoes and lies are planted on top, because FINDINGS declares only a handful
of corrections and because those are the two harms a pile does most: copies
counted as independent sources, and a confident claim with altered numbers.

    stripped.py build   --out DIR [--seed N] [--findings PATH]
    stripped.py control {null,oracle,reversed,lexical} DIR      > answer.json
    stripped.py score   DIR/key.json answer.json --what W --against A [--why Y] [--json]

See README.md beside this file for the answer format and the cost table.
Every `score` is a look, and appends one line to docs/ledger.jsonl.
Standard library only.
"""
import argparse, hashlib, json, os, random, re, subprocess, sys
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_FINDINGS = os.path.join(HERE, "..", "..", "FINDINGS.md")
LEDGER = os.path.join(HERE, "docs", "ledger.jsonl")
N_ECHOES = 6
N_LIES = 6
# The holdout (docs/PLAN_v1.md): any seed at or above this floor. The seeds
# themselves are chosen and kept by the owner outside the working directory,
# because a seed written here would put its key in every agent's reach. Dev
# seeds are small. Scoring a holdout seed spends it, so it needs a reason.
HOLDOUT_FLOOR = 1_000_000

# ---- parsing ---------------------------------------------------------------

def parse_entries(text):
    """Split FINDINGS-shaped text into entries at '## ' headings.
    The preamble before the first heading is dropped: it describes the
    marking convention, which is exactly the structure being stripped."""
    lines = text.split("\n")
    entries, cur = [], None
    for i, line in enumerate(lines, 1):
        if line.startswith("## "):
            cur = {"line": i, "heading": line[3:].strip(), "body": []}
            entries.append(cur)
        elif cur is not None:
            cur["body"].append((i, line))
    return entries

CORR_START = re.compile(r"^> \*\*(Correction|Superseded)\b")
# The label's date and parenthetical go with it: they say WHEN and WHERE the
# correction attaches, which is the structure. What follows the label is the
# corrected claim itself and stays.
CORR_LABEL = re.compile(
    r"^\*\*(Correction|Superseded),?\s*\d{4}-\d{2}-\d{2}(\s+\d\d:\d\d)?"
    r"(\s*\([^)]*\))?(\s*--)?:?\s*")
STRIKE = re.compile(r"~~(.*?)~~\s*--\s*(superseded\b.*?)(?=\n\s*\n|\Z)", re.S)
# A pointer that quotes another entry's heading: `The entry below ("...")`.
POINTER = re.compile(r"[Tt]he entry (?:below|above) \((.*?)\)", re.S)

def detach_blocks(body):
    """Pull labelled correction block quotes out of an entry body.
    Returns (remaining_lines, [(line_no, text), ...])."""
    keep, blocks, i = [], [], 0
    while i < len(body):
        no, line = body[i]
        if CORR_START.match(line):
            start, buf = no, []
            while i < len(body) and body[i][1].startswith(">"):
                buf.append(re.sub(r"^> ?", "", body[i][1]))
                i += 1
            blocks.append((start, strip_label("\n".join(buf))))
            continue
        keep.append((no, line))
        i += 1
    return keep, blocks

def strip_label(block):
    out = CORR_LABEL.sub("", block, count=1)
    # A label whose bold spans into the claim ("**Correction, ... -- this
    # section is now HISTORY, not current state.**") leaves one '**' behind.
    if out.count("**") % 2 == 1:
        out = out.replace("**", "", 1)
    return out.strip()

def norm(s):
    return " ".join(re.sub(r"[`\"'*]", "", s).lower().split())

def heading_matches(quote, heading):
    """A quoted heading may elide its middle with '...'; every piece must
    appear, in order, in the target heading. A piece after the last '...'
    that starts with a comma ("..., item 4") locates a spot INSIDE the
    entry and is not part of its heading."""
    h, pos = norm(heading), 0
    pieces = norm(quote).split("...")
    if len(pieces) > 1 and pieces[-1].lstrip().startswith(","):
        pieces = pieces[:-1]
    pieces = [p.strip(" ,.") for p in pieces]
    pieces = [p for p in pieces if p]
    if not pieces:
        return False
    for p in pieces:
        j = h.find(p, pos)
        if j < 0:
            return False
        pos = j + len(p)
    return True

# ---- building --------------------------------------------------------------

NUMBER = re.compile(r"(?<![\w.-])(\d+\.\d+|\d+x|\d+(?:\.\d+)?%)(?![\w-])")

def make_lie(body_text):
    """The first sentence carrying a measured-looking number, with that
    number's last two digits rotated. Returns (original, lie) or None."""
    for para in re.split(r"\n\s*\n", body_text):
        if para.lstrip().startswith(("```", "|", "    ", ">")):
            continue
        flat = " ".join(para.split())
        for sent in re.split(r"(?<=[.!?])\s+", flat):
            if not (40 <= len(sent) <= 300):
                continue
            m = NUMBER.search(sent)
            if not m:
                continue
            num = m.group(1)
            digits = [k for k, c in enumerate(num) if c.isdigit()][-2:]
            chars = list(num)
            for k in digits:
                chars[k] = str((int(chars[k]) + 3) % 10)
            lie = sent[:m.start(1)] + "".join(chars) + sent[m.end(1):]
            return sent, lie
    return None

def make_echo(rng, body_text):
    """A contiguous ~60% run of an entry's paragraphs, heading and date
    line dropped: the same source, restated without its name."""
    paras = [p for p in re.split(r"\n\s*\n", body_text)
             if p.strip() and not p.strip().startswith("**Date:**")]
    if len(paras) < 2:
        return None
    n = max(1, (len(paras) * 6 + 9) // 10)
    start = rng.randrange(0, len(paras) - n + 1)
    return "\n\n".join(paras[start:start + n])

def build(findings_text, seed, source_name="FINDINGS.md"):
    """Returns (fragments {id: text}, key dict). Pure: no I/O, no clock."""
    rng = random.Random(seed)
    used_ids = set()

    def new_id():
        while True:
            fid = "f%08x" % rng.getrandbits(32)
            if fid not in used_ids:
                used_ids.add(fid)
                return fid

    entries = parse_entries(findings_text)
    frags, meta, rels = {}, {}, []
    host_ids, entry_ids = {}, []
    for e in entries:
        keep, blocks = detach_blocks(e["body"])
        body = "\n".join(l for _, l in keep)
        strikes = []
        def _strike(m):
            strikes.append(m.group(2).strip())
            return m.group(1)
        body = STRIKE.sub(_strike, body)
        e["text"] = body
        eid = new_id()
        entry_ids.append(eid)
        host_ids[e["line"]] = eid
        meta[eid] = {"role": "entry", "origin": "%s:%d" % (source_name, e["line"]),
                     "heading": e["heading"]}
        for no, btext in blocks:
            bid = new_id()
            frags[bid] = btext
            meta[bid] = {"role": "correction", "origin": "%s:%d" % (source_name, no)}
            rels.append({"kind": "corrects", "a": bid, "b": eid,
                         "evidence": "%s:%d labelled block" % (source_name, no)})
        for stext in strikes:
            sid = new_id()
            frags[sid] = stext
            meta[sid] = {"role": "correction", "origin": "%s:%d" % (source_name, e["line"])}
            rels.append({"kind": "corrects", "a": sid, "b": eid,
                         "evidence": "%s:%d strikethrough" % (source_name, e["line"])})

    # Pointers last, once every entry has an id. A pointer is a CORRECTION
    # only when its paragraph says the other entry was wrong; otherwise it
    # is a citation, which this test does not score.
    for e, eid in zip(entries, entry_ids):
        for para in re.split(r"\n\s*\n", e["text"]):
            for m in POINTER.finditer(para):
                if "wrong" not in para.lower():
                    continue
                for t, tid in zip(entries, entry_ids):
                    if t is not e and heading_matches(m.group(1), t["heading"]):
                        rels.append({"kind": "corrects", "a": eid, "b": tid,
                                     "evidence": "%s:%d heading pointer" % (source_name, e["line"])})
                        break
        e["text"] = POINTER.sub("Another note", e["text"])
        frags[eid] = e["heading"] + "\n\n" + e["text"].strip()

    # Echoes and lies come only from entries no declared relation touches,
    # so a planted relation never overlaps a declared one.
    touched = {r["a"] for r in rels} | {r["b"] for r in rels}
    pool = [(e, eid) for e, eid in zip(entries, entry_ids) if eid not in touched]
    lie_pool = [(e, eid, make_lie(e["text"])) for e, eid in pool]
    lie_pool = [x for x in lie_pool if x[2]]
    lies = rng.sample(lie_pool, min(N_LIES, len(lie_pool)))
    lie_src = {eid for _, eid, _ in lies}
    echo_pool = [(e, eid) for e, eid in pool
                 if eid not in lie_src and make_echo(random.Random(0), e["text"])]
    for e, eid in rng.sample(echo_pool, min(N_ECHOES, len(echo_pool))):
        text = make_echo(rng, e["text"])
        xid = new_id()
        frags[xid] = text
        meta[xid] = {"role": "echo", "origin": meta[eid]["origin"]}
        rels.append({"kind": "same", "a": xid, "b": eid, "evidence": "planted echo"})
    for e, eid, (orig, lie) in lies:
        lid = new_id()
        frags[lid] = lie
        meta[lid] = {"role": "lie", "origin": meta[eid]["origin"], "altered": orig}
        rels.append({"kind": "contradicts", "a": lid, "b": eid,
                     "evidence": "planted lie: altered number"})

    key = {"seed": seed, "source": source_name,
           "source_sha256": hashlib.sha256(findings_text.encode()).hexdigest(),
           "fragments": meta, "relations": rels}
    return frags, key

def write_build(out, frags, key):
    corpus = os.path.join(out, "corpus")
    os.makedirs(corpus, exist_ok=True)
    for fid in sorted(frags):
        with open(os.path.join(corpus, fid + ".md"), "w") as f:
            f.write(frags[fid].rstrip() + "\n")
    with open(os.path.join(out, "key.json"), "w") as f:
        json.dump(key, f, indent=1, sort_keys=True)

def read_corpus(out):
    corpus = os.path.join(out, "corpus")
    frags = {}
    for name in sorted(os.listdir(corpus)):
        with open(os.path.join(corpus, name)) as f:
            frags[name[:-3]] = f.read()
    return frags

# ---- scoring ---------------------------------------------------------------

# Pessimistic on purpose: an error that leaves the reader holding a wrong
# belief costs more than one that leaves them unsure. 3 = superseded or lying
# text read as current; 2 = false consensus or an unseen contradiction;
# 1 = a disagreement seen but not resolved, or a claim the key does not hold.
COST = {
    ("corrects", "exact"): 0, ("corrects", "reversed"): 3,
    ("corrects", "contradicts"): 1, ("corrects", "same"): 2, ("corrects", "missing"): 3,
    ("same", "exact"): 0, ("same", "corrects"): 2, ("same", "contradicts"): 2,
    ("same", "missing"): 2,
    ("contradicts", "exact"): 0, ("contradicts", "lie_wins"): 3,
    ("contradicts", "truth_wins"): 0.5, ("contradicts", "same"): 2,
    ("contradicts", "missing"): 2,
}
UNKEYED = 1
KINDS = ("corrects", "same", "contradicts")

def classify(k, ans):
    """How answer `ans` relates to key relation `k` on the same pair."""
    if ans["kind"] == k["kind"]:
        if k["kind"] != "corrects" or ans["a"] == k["a"]:
            return "exact"
        return "reversed"
    if k["kind"] == "contradicts" and ans["kind"] == "corrects":
        # key a is the lie. The lie correcting the truth hides the truth.
        return "lie_wins" if ans["a"] == k["a"] else "truth_wins"
    return ans["kind"]

def score(key, answer):
    by_pair = {frozenset((r["a"], r["b"])): r for r in key["relations"]}
    worst, unkeyed, brier = {}, [], []
    for ans in answer.get("relations", []):
        if ans.get("kind") not in KINDS:
            raise ValueError("unknown kind %r" % ans.get("kind"))
        k = by_pair.get(frozenset((ans["a"], ans["b"])))
        if k is None:
            unkeyed.append(ans)
            c = UNKEYED
        else:
            outcome = classify(k, ans)
            c = COST[(k["kind"], outcome)]
            pair = frozenset((k["a"], k["b"]))
            # Two answers on one pair: the worse one counts.
            if pair not in worst or c > worst[pair][1]:
                worst[pair] = (outcome, c)
        if "p" in ans:
            brier.append((float(ans["p"]) - (1.0 if c == 0 else 0.0)) ** 2)
    per_kind = {kd: {"n": 0, "exact": 0, "wrong": 0, "missing": 0, "cost": 0.0} for kd in KINDS}
    total, null_cost = 0.0, 0.0
    for r in key["relations"]:
        pk = per_kind[r["kind"]]
        pk["n"] += 1
        miss = COST[(r["kind"], "missing")]
        null_cost += miss
        got = worst.get(frozenset((r["a"], r["b"])))
        if got is None:
            pk["missing"] += 1; c = miss
        else:
            c = got[1]
            pk["exact" if got[0] == "exact" else "wrong"] += 1
        pk["cost"] += c
        total += c
    total += UNKEYED * len(unkeyed)
    return {
        "cost": total, "null_cost": null_cost,
        # 1 is the key, 0 is saying nothing, below 0 is worse than nothing.
        "score": 1 - total / null_cost if null_cost else 0.0,
        "per_kind": per_kind, "unkeyed": unkeyed,
        # PLAN_v1's second metric: planted lies the answer adopted as current.
        "lie_wins": sum(1 for o, _ in worst.values() if o == "lie_wins"),
        "brier": sum(brier) / len(brier) if brier else None, "n_brier": len(brier),
    }

# ---- controls --------------------------------------------------------------

def words(text):
    return {w for w in re.findall(r"[a-z0-9_]+", text.lower()) if len(w) >= 3}

def control(name, key, frags):
    rels = key["relations"]
    if name == "null":
        out = []
    elif name == "oracle":
        out = [{"kind": r["kind"], "a": r["a"], "b": r["b"], "p": 1.0} for r in rels]
    elif name == "reversed":
        out = []
        for r in rels:
            if r["kind"] == "corrects":
                out.append({"kind": "corrects", "a": r["b"], "b": r["a"], "p": 1.0})
            elif r["kind"] == "contradicts":
                out.append({"kind": "corrects", "a": r["a"], "b": r["b"], "p": 1.0})
            else:
                out.append({"kind": r["kind"], "a": r["a"], "b": r["b"], "p": 1.0})
    elif name == "lexical":
        # The naive librarian: high word containment means "same source".
        ws = {fid: words(t) for fid, t in frags.items()}
        ids = sorted(ws)
        out = []
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                small = min(len(ws[a]), len(ws[b]))
                if small >= 5 and len(ws[a] & ws[b]) / small >= 0.8:
                    out.append({"kind": "same", "a": a, "b": b, "p": 0.8})
    else:
        raise ValueError("unknown control %r" % name)
    return {"relations": out}

# ---- CLI -------------------------------------------------------------------

def report(res):
    lines = ["score   %.3f   (1 = the key, 0 = saying nothing, <0 = worse than nothing)" % res["score"],
             "cost    %.1f of %.1f for silence" % (res["cost"], res["null_cost"]), ""]
    lines.append("%-12s %4s %6s %6s %8s %6s" % ("kind", "n", "exact", "wrong", "missing", "cost"))
    for kd in KINDS:
        p = res["per_kind"][kd]
        lines.append("%-12s %4d %6d %6d %8d %6.1f" % (kd, p["n"], p["exact"], p["wrong"], p["missing"], p["cost"]))
    lines.append("")
    lines.append("lie_wins %d  (planted lies adopted as current)" % res["lie_wins"])
    lines.append("unkeyed %d  (claims the key does not hold: wrong, or real and undeclared -- read them)"
                 % len(res["unkeyed"]))
    if res["brier"] is not None:
        lines.append("brier   %.3f over %d answers with p (0 = perfectly calibrated)" % (res["brier"], res["n_brier"]))
    return "\n".join(lines)

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--out", required=True)
    b.add_argument("--seed", type=int, default=1); b.add_argument("--findings", default=DEFAULT_FINDINGS)
    c = sub.add_parser("control"); c.add_argument("name"); c.add_argument("dir")
    s = sub.add_parser("score"); s.add_argument("key"); s.add_argument("answer")
    s.add_argument("--what", required=True, help="what was tried, e.g. 'sonnet, arm p, TASK v1'")
    s.add_argument("--against", required=True, help="the result that would count against the claim")
    s.add_argument("--why", help="required for a holdout seed: the decision this look serves")
    s.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        with open(a.findings) as f:
            frags, key = build(f.read(), a.seed, os.path.basename(a.findings))
        write_build(a.out, frags, key)
        kinds = {}
        for r in key["relations"]:
            kinds[r["kind"]] = kinds.get(r["kind"], 0) + 1
        print("%d fragments in %s/corpus; key at %s/key.json; relations %s"
              % (len(frags), a.out, a.out, json.dumps(kinds, sort_keys=True)))
    elif a.cmd == "control":
        with open(os.path.join(a.dir, "key.json")) as f:
            key = json.load(f)
        json.dump(control(a.name, key, read_corpus(a.dir)), sys.stdout, indent=1)
        print()
    else:
        with open(a.key) as f:
            key = json.load(f)
        split = split_of(key)
        if split == "holdout" and not (a.why and a.why.strip()):
            print("refused: seed %d is a holdout seed (>= %d, PLAN_v1). Scoring it spends it; "
                  "say what decision it serves with --why." % (key["seed"], HOLDOUT_FLOOR), file=sys.stderr)
            return 2
        with open(a.answer, "rb") as f:
            raw = f.read()
        res = score(key, json.loads(raw))
        append_ledger(LEDGER, ledger_line(key, split, res, a, hashlib.sha256(raw).hexdigest(),
                                          datetime.now(timezone.utc), git_commit()))
        print(json.dumps(res, indent=1) if a.json else report(res))
    return 0

def split_of(key):
    if key["source"] != "FINDINGS.md":
        return "pilot"
    return "holdout" if key["seed"] >= HOLDOUT_FLOOR else "dev"

def ledger_line(key, split, res, a, answer_sha, now, commit):
    """One look, as DM1's LEDGER template asks. Pure: clock and commit are passed in."""
    return {
        "when": now.strftime("%Y-%m-%dT%H:%MZ"), "who": os.environ.get("USER", "?"),
        "question": "Q1", "what": a.what, "against": a.against, "split": split,
        "seed": key["seed"], "data": key["source"] + "@" + key["source_sha256"][:16],
        "answer": a.answer, "answer_sha256": answer_sha[:16],
        "environment": "python " + sys.version.split()[0], "commit": commit,
        "metrics": {"score": round(res["score"], 3), "cost": res["cost"],
                    "null_cost": res["null_cost"], "unkeyed": len(res["unkeyed"]), "lie_wins": res["lie_wins"],
                    "brier": None if res["brier"] is None else round(res["brier"], 3),
                    "exact": {k: v["exact"] for k, v in res["per_kind"].items()}},
        "holdout_why": a.why if split == "holdout" else None,
    }

def append_ledger(path, line):
    with open(path, "a") as f:
        f.write(json.dumps(line, sort_keys=True) + "\n")

def git_commit():
    try:
        out = subprocess.run(["git", "-C", HERE, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, timeout=10)
        dirty = subprocess.run(["git", "-C", HERE, "status", "--porcelain", "--", HERE],
                               capture_output=True, text=True, timeout=10).stdout.strip()
        return out.stdout.strip() + ("+dirty" if dirty else "") if out.returncode == 0 else "unknown"
    except OSError:
        return "unknown"

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
