#!/usr/bin/env python3
"""The gate. Run from this directory: python3 check.py

WHY THIS EXISTS. v1 offered `grep -c NotImplementedError` as proof the suite was
a specification. It printed the test count whether or not any test reached its
own assertions -- and 81% of them died constructing a Store, never reaching the
function they named (FINDINGS.md C-1). A check that passes by construction is
the most dangerous artifact in engineering: it spends the alarm and leaves the
danger in place.

So this asserts three things a counting grep cannot:

  1. NOTHING PASSES. The skeleton is unimplemented; a green test is a test
     asserting nothing.
  2. NOTHING DIES IN SETUP. A failure raised from tests/support.py or from a
     Store constructor is a test that specifies nothing, whatever its name says.
  3. EVERY FAILURE IS A NotImplementedError. Anything else -- a typo, a bad
     import, a wrong signature -- is a test that stays red after the code is
     written, so it can never go green and never means anything.

Exit 0 when the suite is a valid unimplemented specification.
"""

import collections
import io
import pathlib
import re
import sys
import unittest


def requirement_coverage():
    """Every 'must' in v2 must be named by at least one test, so `grep W-2`
    walks from a red test to the sentence that demanded it.

    The Goodhart risk is explicit and worth naming: once this is a gate, it is
    possible to write tests that name IDs and assert nothing. That is what the
    controls throughout the suite are for, and it is not something a script can
    check.
    """
    req = pathlib.Path("REQUIREMENTS_v2.md").read_text()
    ids = set(re.findall(r'^- \*\*([A-Z]-[0-9]+[a-z]?)\*\*', req, re.M))
    named = set()
    for path in pathlib.Path("tests").glob("test_*.py"):
        for head, tail in re.findall(r'def test_([A-Z])([0-9]+[a-z]?)_',
                                     path.read_text()):
            named.add(head + "-" + tail)
    return ids, sorted(ids - named)


def raising_frame(traceback_text):
    frames = [l for l in traceback_text.splitlines()
              if l.strip().startswith('File "')]
    if not frames:
        return "?"
    return frames[-1].split('"')[1].split("/")[-1]


def main():
    suite = unittest.defaultTestLoader.discover(".")
    result = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0).run(suite)

    total = result.testsRun
    problems = result.errors + result.failures
    passed = total - len(problems)

    where = collections.Counter()
    wrong_kind = []
    in_setup = []
    for test, tb in problems:
        module = raising_frame(tb)
        where[module] += 1
        last = [l for l in tb.strip().splitlines() if l and not l.startswith(" ")]
        if last and "NotImplementedError" not in last[-1]:
            wrong_kind.append((str(test).split()[0], last[-1][:100]))
        if module in ("support.py", "store.py"):
            in_setup.append(str(test).split()[0])

    print(f"tests   : {total}")
    print(f"passed  : {passed}")
    print(f"red     : {len(problems)}")
    print()
    print("failure raised in:")
    for module, n in where.most_common():
        print(f"  {n:4d}  {module}  ({100 * n / max(total, 1):.0f}%)")

    # Which behavior modules no test currently reaches. Not a failure while the
    # whole skeleton is unimplemented -- a test that must write before it reads
    # legitimately stops at writer.py -- but it is the honest limit of what this
    # gate proves, and it becomes a real signal as modules get implemented.
    behavior = {"ids.py", "diary.py", "writer.py", "reader.py", "refs.py",
                "derive.py", "merger.py", "withdrawal.py", "schema.py"}
    unreached = sorted(behavior - set(where))
    if unreached:
        print("\nnot reached by any test (no test gets past an earlier dependency):")
        for module in unreached:
            print("   ", module)

    ids, uncovered = requirement_coverage()
    print(f"\nrequirements: {len(ids)}  covered: {len(ids) - len(uncovered)}"
          f"  uncovered: {len(uncovered)}")

    ok = True
    if uncovered:
        ok = False
        print("\nFAIL: requirements no test names:")
        print("       ", " ".join(uncovered))
    if passed:
        ok = False
        print(f"\nFAIL: {passed} test(s) passed. The skeleton is unimplemented,")
        print("      so a green test is a test asserting nothing.")
    if in_setup:
        ok = False
        print(f"\nFAIL: {len(in_setup)} test(s) died in setup, not in what they name:")
        for name in in_setup[:10]:
            print("       ", name)
    if wrong_kind:
        ok = False
        print(f"\nFAIL: {len(wrong_kind)} test(s) failed for the wrong reason.")
        print("      These stay red after the code is written:")
        for name, line in wrong_kind[:10]:
            print(f"        {name} -> {line}")

    print("\nOK: the suite is a valid unimplemented specification."
          if ok else "\nNOT OK.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
