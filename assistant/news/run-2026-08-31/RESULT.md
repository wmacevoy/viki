# Result — run 2026-08-31

**Scored blind against the pre-registered rubric. n=1 per arm.**
A = control (compaction template). B = treatment (nanny's shape).

| metric (pre-registered) | A control | B treatment |
|---|---|---|
| **1. Fluent claims** — invented, not in source | **7** | **6** |
| 2. Open questions: open / settled / absent | 2 / **1** / 2 | 2 / **0** / 3 |
| 3. Hedges: attached / claim absent | **1** / 3 | **2** / 2 |
| 4. Revived false claims | 0 | 0 |

## The hypothesis is NOT supported

**7 versus 6 is not a result.** On the primary metric the two arms are
indistinguishable at this sample size, and the prediction going in was that
the treatment would clearly win. It did not.

The treatment is directionally better on the two secondary metrics — the one
`settled` (uncertainty arriving as fact, the worst outcome) occurred only in
the control, and hedges stayed attached twice versus once. Directional, n=1,
and not what was being tested.

## What the run DID establish, and it is worse news

**Both successors invented at about the same rate: roughly one fabricated
checkable claim per 130–150 words.** Seven in 891 words; six in ~950. Neither
arm was told to speculate; both were told the briefing was all they had.

**The shape of the briefing barely moved that.** Warren MacEvoy's framing was
right and this is the number for it: unchecked fluency is the killer, and a
better handoff document is not the cure. Both briefings carried the caveats.
Both successors invented anyway, because ~900 words of readable prose demands
more specificity than the source contains, and the gap gets filled.

Sample of what each invented, from the blind audit:

- **A** merged two separate studies into one collaboration; moved a February
  event into August; said an outside audit found what the source says the
  vendor found in-house; and **inverted TRACES' central finding** — a 95%
  *failure* rate became "roughly 95% caught," followed by a recommendation to
  use the tool for the task the paper shows it fails.
- **B** reported "3 of 141,006 runs" where the source says 3 incidents across
  6 runs; renamed the UK AI *Security* Institute the AI *Safety* Institute;
  turned "no evidence of real-world harm" into "**No harm resulted**"; and
  compressed hit-rate ratios spanning ~1.8x–10.8x into "2–4x."

## An observation that is NOT a rescue of the hypothesis

A's TRACES inversion and B's "2–4x" are both one tick in column 1, and they
are not equally harmful: one misstates a ratio, the other tells a reader to
adopt a tool for the exact task it fails at. **Counting claims equally is the
wrong instrument.**

That is a hypothesis for the NEXT run — severity-weighted scoring, and a
rubric that separates *imprecision* from *sign inversion*. **It is recorded
here as a design note, not as a reinterpretation of this run.** The rubric was
pre-registered precisely so that a losing result could not be argued into a
winning one afterward, and re-scoring by a metric invented after seeing the
outputs is model selection on the test set.

## Limits, stated rather than buried

- **n=1 per arm.** One reporter, one nanny, one successor each.
- **A/B assignment was fixed, not randomized.** Disclosed in `RUBRIC.md`.
- **Retrieval was forbidden** to isolate what the briefing conveys. A real
  successor could search, and searching is the obvious antidote. This measures
  the handoff, not the realistic workflow.
- **One judge.** No inter-rater check. Several calls (what counts as
  "restatement in different words") are judgement, not counting.
- The judge was blind to which arm was which, and to the hypothesis.

## What to run next

1. **Severity-weighted rubric**, pre-registered again before any output is read.
2. **Randomize A/B**, and use 3+ successors per arm.
3. **Allow retrieval in one condition** — if searching removes the effect, the
   finding is about handoffs with no verification loop, which is a narrower
   and more honest claim.
4. **Test a briefing that names the fluency risk directly** — the untried
   intervention. Neither successor was warned that inventing was the failure
   mode; both were told only that the briefing was all they had.
