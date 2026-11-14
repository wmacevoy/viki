"""refusal-check.py ROOT: REQ3/E-3, every refusal names its requirement.

A refusal's message is a C string literal that reaches vk_refuse: given to it
directly, or through zBad, which is filled from an assignment, a `return` in a
*_error function, or the azOutlineBad table. Every such literal must begin with
a requirement ID ("D-3: ..."), and vk_refuse may be given nothing but a literal
or zBad, so no message can arrive from anywhere this does not read.
"""
import re
import sys

ID = re.compile(r'^"[A-Z]+-\d+[a-z]?\d?: ')
FILES = ["viki_store.c", "viki_search.c", "viki_delta.c", "viki_vector.c", "viki_regexp.c"]


def literals(src):
    """(line, literal, how) for every literal that can become a refusal's message."""
    out = []
    for n, line in enumerate(src.splitlines(), 1):
        for m in re.finditer(r'vk_refuse\([^,]+,\s*VK_[A-Z]+,\s*([^)]*)\)', line):
            arg = m.group(1).strip()
            if arg.startswith('"'):
                out.append((n, arg, "vk_refuse"))
            elif arg != "zBad":
                out.append((n, arg, "vk_refuse given something other than a literal or zBad"))
        for m in re.finditer(r'(?:\bzBad|\*pzBad)\s*=\s*("(?:[^"\\]|\\.)*")', line):
            out.append((n, m.group(1), "zBad"))
    # returns inside the functions that produce zBad
    for fn in re.finditer(r'static (?:const )?char \*(\w+_error)\([^)]*\)\{(.*?)\n\}', src, re.S):
        start = src[:fn.start()].count("\n") + 1
        for m in re.finditer(r'return ("(?:[^"\\]|\\.)*")', fn.group(2)):
            out.append((start + fn.group(2)[:m.start()].count("\n"), m.group(1), fn.group(1)))
    table = re.search(r'azOutlineBad\[\] = \{(.*?)\};', src, re.S)
    if table:
        for m in re.finditer(r'("(?:[^"\\]|\\.)*")', table.group(1)):
            out.append((0, m.group(1), "azOutlineBad"))
    return out


def main(root):
    bad, n = [], 0
    for f in FILES:
        src = open(f"{root}/core/src/{f}").read()
        for line, lit, how in literals(src):
            n += 1
            if not ID.match(lit):
                bad.append(f"{f}:{line} ({how}) {lit}")
    if n < 40 or bad:   # the count guards the scan itself: a regex that matched nothing would pass
        print(f"FAIL E-3 every refusal names its requirement: {len(bad)} of {n} do not")
        for b in bad:
            print("  " + b)
        return 1
    print(f"PASS E-3 every refusal names its requirement ({n} messages read)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
