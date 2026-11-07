# The stripped-structure test

Does a librarian help with a pile of information that does not declare its own structure? This
test makes such a pile with a known answer. It takes a document that **does** declare its
structure, `FINDINGS.md`, strips the declarations out, and keeps them as the key.

Most piles a librarian would face are messy: a person's life, a live feed, a repository before a
refactor. A messy pile rarely comes with ground truth. A tidy one can be made messy on purpose,
and then the answer is known.

```sh
python3 test/stripped/stripped.py build --out DIR [--seed N]     # DIR/corpus/*.md + DIR/key.json
python3 test/stripped/stripped.py control oracle DIR > a.json    # or null, reversed, lexical
python3 test/stripped/stripped.py score DIR/key.json a.json      # --json for the raw result
python3 -m unittest discover -s test/stripped -v                 # the harness's own tests
```

Standard library only. `build` is a pure function of the source text and the seed.

## What is stripped

FINDINGS declares four kinds of structure, and all four are removed:

| Declared as | In the corpus | In the key |
| --- | --- | --- |
| `> **Correction, DATE:**` / `> **Superseded DATE:**` block inside an entry | its own fragment, label, date and `>` removed | `corrects(block, host entry)` |
| `~~claim~~ -- superseded by ...` | claim kept as plain text in the host; the tail becomes its own fragment | `corrects(tail, host entry)` |
| `The entry below ("<quoted heading>")`, in a paragraph saying it was wrong | replaced by `Another note` | `corrects(this entry, quoted entry)` |
| newest-first order, and the preamble describing the convention | fragments under random seeded ids; preamble dropped | — |

A pointer whose paragraph does not say *wrong* is a citation, not a correction, and is not
scored. Ordinary words like "wrong" or "superseded" stay in the text. They are content, and a
real pile has them too. What is removed is anything that says *which* fragment they are about.

On top of that, **planted** relations, drawn only from entries no declared relation touches:

- **Echoes** (`same`): a contiguous ~60% run of an entry's paragraphs, heading and date dropped.
  The same source, restated without its name.
- **Lies** (`contradicts`): one sentence from an entry that carries a measured-looking number,
  with the number's last two digits rotated. It is a confident claim with no repro, the shape a
  feed is full of. Structurally a lie looks exactly like a later re-measurement, which is the point.

## The answer a librarian gives

The librarian reads `DIR/corpus/` and nothing else, never `key.json`. It writes:

```json
{"relations": [
  {"kind": "corrects",    "a": "f1a2b3c4", "b": "f5d6e7f8", "p": 0.9},
  {"kind": "same",        "a": "...",      "b": "..."},
  {"kind": "contradicts", "a": "...",      "b": "...",      "p": 0.4}
]}
```

- `corrects(a, b)` means *a* replaces *b*, so *b* is no longer current. Direction matters.
- `same` means one origin. `contradicts` means a disagreement it cannot resolve. Both are undirected.
- `p` is optional: the librarian's probability that the claim is right. It feeds a Brier score,
  which measures **calibration**, not accuracy.

## The cost table, and why it is pessimistic

| Key says | Answer | Cost | Why |
| --- | --- | --- | --- |
| corrects | missing, or reversed | 3 | the superseded fragment reads as current |
| corrects | `same` | 2 | |
| corrects | `contradicts` | 1 | saw the disagreement, did not resolve it: honest |
| same | missing, or any other kind | 2 | copies counted as independent sources: false consensus |
| contradicts | `corrects(lie, truth)` | 3 | the lie adopted, the truth hidden |
| contradicts | `same`, or missing | 2 | the contradiction never surfaced |
| contradicts | `corrects(truth, lie)` | 0.5 | right side, overclaimed |
| no relation | any claim | 1 | *unkeyed*: listed for a human to read |

`score = 1 - cost / cost_of_silence`. 1 is the key, 0 is saying nothing, and **below 0 is worse
than saying nothing**. A librarian can earn a negative score, and that is the outcome this test
exists to detect. When two answers name one pair, the worse one counts.

## The controls, measured

Seed 1, `FINDINGS.md` sha256 `9077b3cd842a7684…`. Re-run before quoting: these move whenever
FINDINGS does.

| Control | Score | What it shows |
| --- | --- | --- |
| `oracle` (the key) | 1.000 | the scorer can say yes |
| `null` (nothing) | 0.000 | the scale's zero |
| `reversed` (every direction flipped) | 0.133 | direction is scored, and lies adopted cost the most |
| `lexical` (word containment ≥ 0.8 → `same`) | 0.222 | finds **all 6 echoes**, calls **all 6 lies echoes**, finds no corrections |

The `lexical` row is the result worth reading. Overlap finds copies, and it reads an altered copy
as one more source agreeing. That's the false-consensus harm, and STRIP1/6 asserts the corpus
keeps posing it.

## Requirements *(inferred, 2026-11-07, from the request that built this)*

Nobody has confirmed these. They are what the tests check.

- **STRIP1/1** The corpus carries none of the declared structure, and the text the markers
  carried is kept.
- **STRIP1/2** Every relation the source declares reaches the key, citing the line that declared it.
- **STRIP1/3** The same seed and source give byte-identical output.
- **STRIP1/4** The key never lands in the corpus, and fragment ids carry no source order.
- **STRIP1/5** The scorer gives the key 1 and silence 0, punishes reversed direction, costs an
  adopted lie above a seen one, and counts the worse of two answers.
- **STRIP1/6** A word-overlap librarian finds every echo and mistakes every lie for one.

Each test was seen failing against a deliberate break of the code it guards. One was vacuous on
first writing: it passed with "last answer wins" because it put the worse answer last. It now
checks both orders.

## Limits

- **n is small.** FINDINGS declares 7 corrections; the other 12 relations are planted. Read a
  difference of one or two corrections as noise.
- **It fits in a context window.** About 234 KB, roughly 60k tokens. This tests *judgment* over
  a pile, not retrieval at the scale (≥ 10 MB) where a store like viki starts to matter. A larger
  run needs a larger tidy source, or several stripped together.
- **Echoes are lexical.** Overlap finds them trivially. Paraphrased echoes need a model to write
  them, and then the build is no longer deterministic.
- **The key is a lower bound.** FINDINGS repeats itself in places it never declares: the lexical
  control's 2 unkeyed pairs are real overlaps between a planted lie and a second entry sharing its
  source sentence. An unkeyed claim may be right, and a person should read the list before
  calling it wrong.
- **Lies are numeric only.** A negated claim, a changed name, or a wrong date is not planted yet.

## One finding the build turned up

The `Superseded` block at FINDINGS line 3798 points to "the entry at the top of this file". That
entry (line 2599) stopped being at the top many entries ago. In a newest-first file, a positional
pointer rots as soon as anything is added above it. Only the back-pointer at 2599, which quotes
the target's heading, still resolves the relation.
