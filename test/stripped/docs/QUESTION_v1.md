# Question — v1

Client (the one voice who can decide): Warren MacEvoy · Date sent: 2026-11-07 · *Tell us what is wrong
with it.* **Draft, written by Claude Opus 5.5 from the 2026-11-07 conversation; every line is
*(inferred)* until Warren confirms it.** No librarian has been scored against FINDINGS.md yet.

## The decision

- **Decision:** whether a model-run librarian is worth putting between an agent and a messy pile:
  one that reads the pile and says which notes correct, copy, or contradict each other. And whether
  it should only point, or also collate.
- **What we would do if the answer came out the other way:** not build one, and recover viki without
  a librarian layer (SCOPES L3 stays empty), or try a different model or prompt before deciding.
- **DECISION-1** *(since v1)* *(judged)* Whether to build a librarian into viki's workflow. Judge:
  Warren, on the holdout result, the per-kind table and a read of the unkeyed claims, when the dev
  search budget is spent. Veto: any holdout run that adopts a planted lie as current.

## Fundamental shape

Held by: Warren

- **PIPE-1** *(since v1)* *(judged)* Ground truth comes only from structure the source declared, or
  from relations planted by a seeded generator. Never from a model's or a person's reading after the
  fact. Judge: Warren, at each change to `stripped.py`.
- **Says no to:** a model writing the key or grading by opinion. A model may be a librarian; it may
  not be the answer.
- **Exceptions:** the unkeyed list, which a person reads and may promote into a new key version.

## People in the data

`FINDINGS.md` is engineering notes written by the owner and agents working for him. It names the owner
and no other private person by more than a role. Sending it to a hosted model (Sonnet, through
Anthropic) is the owner's request of 2026-11-07. Every copy a run makes lives in its evidence
directory outside this repository, and in `/tmp/viki-lib-<run>`. Delete both when the trial closes.

## Bias

- **Who asks about one case, and which reference class will the answer describe:** Warren, about his
  own piles. The answer describes this one source, by this one author, in this one convention.
- **Groups that matter:** none among people. The slices that matter are the kinds of relation
  (PLAN_v1).
- **BIAS-1** *(since v1)* *n/a*: no rows are about people.

## Definitions

- **KIND-1** *(since v1)* The kind of claim: predicted. The librarian predicts, for pairs of
  fragments, a relation whose truth is in the key.
- **UNIT-1** *(since v1)* One row is an unordered pair of fragments in one pile. Direction is part of
  the outcome for `corrects`.
- **OUT-1** *(since v1)* The outcome: the key's relation for the pair, `corrects(a,b)`, `same`,
  `contradicts`, or none, as `stripped.py build` writes it into `key.json`.
- **PRED-1** *(since v1)* The prediction point: the librarian has seen the pile and nothing else.
  It has not seen the source, the key, the pile's seed, or any other pile.

## The bar

- **BAR-1** *(since v1)* *(invented)* `stripped.py score` on every holdout seed: above the `lexical`
  control on the same seed (0.222 on dev seed 1), and no planted lie adopted (`lie_wins` = 0). Report
  the per-seed spread. The bar can veto; it does not pass DECISION-1.

## The price of each error

The cost table in `../README.md` is the price list.

| Error | What it looks like here | Who pays | Relative cost |
| ----- | ----------------------- | -------- | ------------- |
| False positive | a relation the key does not hold | the reader, who checks it | 1 |
| False negative | a correction missed: superseded text read as current | the reader, who believes it | 3 |

## Envelope

- **Data:** one source (234 KB, 82 entries). Each pile: about 99 fragments, 19 keyed relations.
- **Labels:** free, from the generator.
- **Compute:** about $0.03 for the 8-fragment pilot. Unmeasured at full size.
- **Expert time:** Warren, to read unkeyed claims.

## Assumptions (K4s), read back to the client

- "We're assuming FINDINGS.md is not in Sonnet's training data. The repository went public on
  2026-08-13, after the stated cutoff of this model family. True?" — *unanswered*
- "We're assuming a holdout seed holds out the ids, order, echoes and lies, but **not** the 7
  declared corrections, which are in every pile. True, and acceptable?" — *unanswered*
- "We're assuming one source, by one author, stands in for a messy pile at all." — *unanswered*

## Not for

- Retrieval at scale (≥ 10 MB): the pile fits in one context window.
- Paraphrased echoes, or lies other than altered numbers: none are planted.
- Other model families: the trial runs one model.

## Not applicable

| Row | Why |
| --- | --- |

## Not yet answered

- The bar, the three K4s above, and the holdout seeds — Warren.
