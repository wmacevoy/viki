"""gate.py REQUIREMENTS RESULTS: REQ3/FIN-3's gate, seeded.

Prints every requirement ID in the 1.0 scope as green, red, or not reached,
and exits nonzero on any red. A range like "A-1 to A-8" expands to its
numbered IDs; a lettered ID (R-4a) is counted once a test or the scope names it.
"""
import re
import sys

ID = r"[A-Z]+-\d+[a-z]?\d?"


def scope_ids(text):
    """The IDs of the "In" tables of the 1.0 scope section, in order."""
    section = text.split("## 1.0 scope", 1)[1].split("\n## ", 1)[0]
    section = section.split("**Out of 1.0**", 1)[0]
    ids = []
    for m in re.finditer(rf"({ID})(?: to ({ID}))?", section):
        first, last = m[1], m[2]
        if last and first.split("-")[0] == last.split("-")[0]:
            series = first.split("-")[0]
            lo = int(re.match(r"\d+", first.split("-")[1])[0])
            hi = int(re.match(r"\d+", last.split("-")[1])[0])
            ids += [f"{series}-{i}" for i in range(lo, hi + 1)]
            if not last.split("-")[1].isdigit():
                ids.append(last)
        else:
            ids.append(first)
    # Mission, shape and application spaces are judged or cited, not tested.
    ids = [i for i in ids if i.split("-")[0] not in ("MISSION", "SHAPE", "AS")]
    return list(dict.fromkeys(ids))


def results(text):
    """ID -> True if every test naming it passed. Unnamed checks are under '-'."""
    out = {}
    for m in re.finditer(r"^(PASS|FAIL) (\S+) ", text, re.M):
        out[m[2]] = out.get(m[2], True) and m[1] == "PASS"
    return out


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    ids = scope_ids(open(argv[1]).read())
    got = results(open(argv[2]).read())
    for extra in got:
        if extra != "-" and extra not in ids:
            ids.append(extra)
    red = [i for i in ids if got.get(i) is False]
    green = [i for i in ids if got.get(i) is True]
    missing = [i for i in ids if i not in got]
    print(f"gate: {len(green)} green, {len(red)} red, {len(missing)} not reached, of {len(ids)} in scope")
    print("green:", " ".join(green))
    if red:
        print("RED:", " ".join(red))
    print("not reached:", " ".join(missing))
    return 1 if red or got.get("-") is False else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
