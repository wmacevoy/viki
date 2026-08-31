# v3 result — the warned tribe

Three members, one shared store, warned about fluency at birth. 13 propositions:
6 answerable from the store, 4 the store explicitly flags unresolved, and 3
**not in the store at all**.

## Verdicts: 13/13 for all three. No errors. No disagreement.

| member | model | all | tier A (in store) | tier B (flagged) | tier C (ABSENT) |
|---|---|---|---|---|---|
| gee | haiku | 13/13 | 6/6 | 4/4 | **3/3** |
| inch | sonnet | 13/13 | 6/6 | 4/4 | **3/3** |
| loom | sonnet | 13/13 | 6/6 | 4/4 | **3/3** |

**Zero fabrications on the three absent items**, against ~6–7 invented claims
per ~900-word brief in v2.

## That number is CONFOUNDED and must not be read as "the warning worked"

**Two variables changed at once.** The warning, and the output format. v2 asked
for ~900 words of flowing prose; v3 asks for a verdict per item with `UNKNOWN`
available as a first-class answer. **Prose demands filling. A form does not.**

Nothing here separates those. A v4 that warns but demands prose, and a v4 that
forms but does not warn, are the two runs that would.

## Ceiling effect: the tribe question is UNANSWERED

The question this design exists for — *does disagreement predict error?* —
requires errors. There were none, and no splits. **The quiz was too easy once
the members were warned.** That is not a null result; it is a measurement that
failed to reach its own instrument's range.

## The real finding: they agreed on every VERDICT and split on how to MARK UNCERTAINTY

Mean confidence by tier — every verdict correct, so this is pure calibration:

| member | model | A: in store | B: flagged | **C: ABSENT** |
|---|---|---|---|---|
| gee | haiku | 94 | 20 | **12** |
| inch | sonnet | 93 | 84 | **89** |
| loom | sonnet | 91 | 75 | **84** |

And the `k` tag on the three items the store never mentions:

    gee    k0  k0  k0
    inch   k1  k1  k1
    loom   k1  k1  k1

**They were answering different questions with the same field, and both
readings are defensible.** gee read *confidence* as confidence-in-knowing — low,
because it does not know. inch and loom read it as confidence-that-UNKNOWN-is-
correct — high, because they are sure the store is silent. gee tagged `k0`
meaning *I know the store does not address this*; the others tagged `k1`
meaning *I do not know the answer*. Both are true statements about different
objects.

**The ambiguity is mine.** I wrote a field with two readings and got both back.
That is an instrument defect, not a model difference, and the fact that it
split cleanly along model lines is exactly what would have made it easy to
misread as one.

## Which scale is better is not obvious, and that matters for §56

By the literal question asked ("how likely is your verdict correct"), inch and
loom are **right** and gee is badly underconfident: `UNKNOWN` *was* correct, so
12 is far too low.

But gee's numbers carry more information. 94 → 20 → 12 **tracks the tiers**,
so the confidence column alone tells you where the tribe's knowledge actually
sits. inch and loom's 93 → 84 → 89 is flat and tells you nothing about which
items are grounded.

**So the better-calibrated answer is the less useful one**, which is a real
result for polls and not a curiosity:

> A ballot must record **what the voter took the scale to mean**, or a tally
> averages different quantities and reports a number belonging to neither.
> Two members can be perfectly honest, perfectly correct, and still not be
> addable.

## What v4 needs

1. **Harder items** — the disagreement question needs propositions where members
   actually differ. Candidates: items requiring inference across two store
   findings, items where the store is internally in tension, items where a
   plausible reading and the correct reading diverge.
2. **Deconfound the warning** — warn+prose, and form-without-warning.
3. **Split the confidence field in two**: *P(my verdict is correct)* and
   *is this grounded in the store, yes/no*. They are different and were being
   collapsed.
4. **Retire-and-memoir has not been tested at all.** This run had no gap in it:
   three members answered once, in parallel, from a static store. The v3 design
   Warren specified — a trace that retires deliberately at a fuel threshold,
   writes a memoir into viki, and a successor that queries rather than inherits
   — is still unbuilt. This measured the *warning*, not the *lifecycle*.
