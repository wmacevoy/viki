# Question — v1

The decision this work serves, and exactly what is being predicted, described, or found. The data
equivalent of requirements: changes only with the client, and a change in meaning is a new version
(`QUESTION_v2.md`); a spelling or lint fix is not, and is named in the commit
([SE1/R10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r10)). See
[DM1/R1–R12](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md).

Client (the one voice who can decide): _name_ · Date sent: _yyyy-mm-dd_ · *Tell us what is wrong
with it.*

## The decision

Who decides what, when, with what in front of them today — and what it costs when they get it wrong
([DM1/R1](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r1)).

- **Decision:** _…_
- **What we would do if the answer came out the other way:** _…_ (If "nothing," stop here.)
- **DECISION-1** *(since v1)* *(judged)* _The decision this work serves, stated broadly._ Judge: _who_,
  on _what_, at _when_. Veto: _what would stop the work_
  ([DM1/U11](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u11)).

## Fundamental shape

The versioned and heavy aesthetic that decides what belongs here, through a few guidelines; a sign it is
working is that someone can guess the next pipeline from the last
([DM1/D9](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d9)). Mechanisms go in the plan, citing these. Unstated, it iterates toward finished.

Held by: _one named person_

- **PIPE-1** *(since v1)* *(judged)* _A guideline that holds across the pipelines._ Judge: _the holder_, at _when_.
- **Says no to:** _good ideas this refuses, and why_
- **Exceptions:** _each with its reason_

## People in the data

Whose rows these are, what they were collected for, and whether this use is that
([DM1/P3](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/01-purpose.md#p3),
[DM1/R9](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r9)).

## Bias

A requirement like any other, decided with the client
([DM1/R10](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r10)).

- **Who asks about one case, and which reference class will the answer describe:** _…_
  ([DM1/U8](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u8))
- **Groups that matter:** _…_ — _who said so_
- **BIAS-1** *(since v1)* The measure (error rate, calibration, or selection rate, per group), the
  largest acceptable gap, and who chose that measure over the others: _…_

| Source | Present here? How we will check |
| ------ | ------------------------------- |
| Historical | |
| Selection, representation | |
| Measurement | |
| Aggregation | |
| Evaluation | |
| Deployment | |
| Unequal treatment | |

## Definitions

One-way doors: each costs a re-analysis to change
([DM1/R2](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r2)). Prefix
by area (`OUT-1`, `UNIT-1`, …), mark each with the version that last changed it, and cite them as
`Q1/OUT-1`.

- **KIND-1** *(since v1)* The kind of claim, one of predicted, described, explained, extracted: _…_
  ([DM1/R12](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r12))
- **UNIT-1** *(since v1)* One row is: _…_
- **OUT-1** *(since v1)* The outcome, written so it could be a query: _…_ (A predicted or explained
  claim has one. Otherwise, what the claim is checked against, or delete this line.)
- **PRED-1** *(since v1)* The prediction point — when the prediction is made, and so what may be
  known: _…_ (A predicted claim has one. Otherwise, what stands in for it, or delete this line.)

## The bar

Specific enough to be rejected, written before results are seen
([DM1/R3](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r3)). Mark
guesses *(invented)*.

- **BAR-1** *(since v1)* Metric _…_, on _which split / period_, at least _…_, against the baseline
  _…_ which scores _…_, with the interval reported. The bar can veto; it does not pass DECISION-1
  ([DM1/U11](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u11)).

## The price of each error

([DM1/R4](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r4))

| Error | What it looks like here | Who pays | Relative cost |
| ----- | ----------------------- | -------- | ------------- |
| False positive | | | |
| False negative | | | |

## Envelope

([DM1/R8](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r8))

- **Data:** _rows, periods, and — the binding number — events_
- **Labels:** _how many, at what cost, by when; agreement rate if known_
- **Compute:** _per run, and runs budgeted_
- **Expert time:** _whose, how many hours_
- **Inference:** _latency and cost per prediction_
- **Freshness:** _how stale the model may get, and who decides to retrain_

## Assumptions (K4s), read back to the client

In their words, not ours ([DM1/H2](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/09-people.md#h2)).

- "We're assuming _…_. True?" — _answer, date_

## Not for

Populations, conditions, and uses this work will still produce an answer for, and that we will not
stand behind ([DM1/R11](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r11)).

- _…_ — _reason_

## Not applicable

What in this guide does not bind this kind of claim, and why, rather than a template filled in with
something invented ([DM1/R12](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r12)).
`./dm --doctor` reads this table. *Row* is one of its rows: `plan`, `holdout`, `ledger`, or
`model card`; a predicted claim sets none aside, and an explained claim only the holdout and the model
card. Nothing sets aside a datasheet, or a row that is failing.

| Row | Why |
| --- | --- |
| _row_ | _why, in a sentence_ |

## Not yet answered

- _Question_ — _who can answer, by when_
