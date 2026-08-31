# The news agency, and the gap experiment inside it

Two things at once, deliberately.

**A news agency**, because a tribe that cannot see the world it works in is a
tribe with one keeper of continuity. And **a controlled test of the gap**,
because news is the right substrate for it: high volume (a window fills fast),
claims that get corrected on a real clock, and ground truth that arrives later
rather than being authored by whoever is grading.

## The question

When a trace dies and a successor picks up, **what crosses the gap?**

The harness summary has nine slots, and one of them IS *Errors and Fixes* —
so the sharp version of this claim ("there is no slot for what was found
false") is **wrong**, and `CONTROL-TEMPLATE.md` has the correction. The honest
version is narrower and weaker:

> Hypothesis: section 4 is shaped for **things done** — a defect, then its
> patch. A CLAIM the trace came to hold and later found FALSE has no natural
> home there: it was never a defect in a file and has no fix to pair with. So
> it lands in section 2 as a *concept with its status stripped*, or not at
> all. A cheap dedicated nanny, asked for exactly that, preserves it.

**One authoritative copy of the hypothesis lives in `CONTROL-TEMPLATE.md`.**
This is a pointer, because this repo has had one claim live in two files and
rot in one at least three times.

## The design

| role | model | job |
|---|---|---|
| `reporter-1` | sonnet | research the beat. Produce claims TAGGED k0..k4, and say what it is unsure of. Its window then "closes." |
| `nanny` | haiku | read reporter-1 whole. Write the handoff — **what must not be relearned, what is uncertain, what is FALSE.** Nothing else. |
| `reporter-2a` | sonnet | **CONTROL.** Gets a harness-shaped summary (facts + pending tasks, no uncertainty slot). Continues the beat. |
| `reporter-2b` | sonnet | **TREATMENT.** Gets the nanny's handoff. Same instructions otherwise. |

`2a` and `2b` are genuinely fresh agents. They know only what their briefing
file says. **That is the gap** — not a simulation of it. What is simulated is
only the *timing*; nothing here waits for a real compaction.

## The score

**THE PRIMARY METRIC IS UNCERTAINTY. Unchecked fluency is the killer.**
— Warren MacEvoy, 2026-08-31, correcting this file's first draft.

An earlier draft made *revival* primary: does the successor re-assert a claim
its predecessor tagged `k3`/`k4`? That is too narrow, and it is the wrong
shape. Revival needs the predecessor to have ALREADY caught the error, so it
only measures the leak on errors someone already found. The actual failure mode
is bigger and quieter: **everything arrives at one confidence level, and the
successor is fluent about all of it.**

It is also the failure mode this project has measured on itself. The false
claims in session `ff137b62` were not resurrected corpses. They were fluent
inventions — "one compaction," "weeks of work," "there was no sentence I could
write." Every one confident, specific, and unchecked. Wrong toward
SPECIFICITY, never toward vagueness.

Counted, judged BLIND (the judge is not told which output is which):

1. **Fluency — the primary metric.** Count the claims the successor states
   FLATLY that (a) its briefing did not establish and (b) it did not retrieve.
   These are invented at the seam. Lower is better; zero is the target.
2. **Uncertainty survival.** Of the predecessor's `k1` known-unknowns, how many
   reach the successor still marked open — versus arriving as settled fact,
   versus vanishing? Settled-fact is the worst outcome, worse than vanishing,
   because a lost question can be re-asked and a false answer stops the asking.
3. **Revival** — demoted to third. A special case of (1) where the invention
   happens to match something already known false.
4. **Rework** — does it redo retrieval its predecessor already did?

**The judge must be blind.** It is given two briefed outputs labelled A and B
with the mapping withheld, because a judge told which one is the treatment will
find the treatment better. That is the same free agreement this project keeps
measuring, wearing a lab coat.

A control that beats the treatment falsifies the whole seam programme, which is
why it is run rather than assumed. See `CONTINUITY.md`.

## The k vocabulary

From *Life by the Numbers*, and it is the reason news is a good beat:

    k0  known                     k3  confident error (believed, wrong)
    k1  known unknown             k4  true falsehood (asserted, known wrong)
    k2  unknown unknown

## Run

    sh assistant/news/run.sh        # not yet written -- the first run was by hand
