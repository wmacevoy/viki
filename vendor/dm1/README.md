# Data Mining

A guide to producing claims from data that are worth acting on — written for humans and for the AI
agents working beside them. It is a companion to
[Software Engineering](https://github.com/wmacevoy/software-engineering) (`SE1`), and assumes it.

Software engineering asks *does the thing work*. Data mining asks *is the claim true* — and before
that, *would anyone act differently if it were*. Most of the discipline is the same, pointed at a
different noun, so this guide does not repeat SE1. Each principle here either **reframes** an SE1
principle for data (and links to it), or is **new**: something SE1 does not say because software
does not need it. Every principle says which, on the line under its heading. The
[web guidebook](https://wmacevoy.github.io/data-mining/) renders these files.

It is organized as **numbered principles** with versioned IDs (`DM1/F5`, `DM1/D2`, `DM1/R6`, …),
in the same ten chapters, with the same letters, as SE1. Each states one idea and says why it is
true; most end with a question you can ask out loud in a meeting or a review: *"this holdout has
been looked at six times (DM1/F5)"*, *"this model is a cache of last season; how stale? (DM1/D2)"*.

## The argument in ten lines

1. **Data miners produce claims worth acting on.** A model is correct about a dataset; a claim is
   true about a world, and the gap between them is most of the work
   ([DM1/P1](guide/01-purpose.md#p1), [DM1/P2](guide/01-purpose.md#p2)).
2. **People ask about themselves; data answers about people like them.** The gap is bias, it falls
   hardest on whoever is rare in the rows, and like ethics and security it is always a requirement
   ([DM1/P3](guide/01-purpose.md#p3), [DM1/U8](guide/02-unknowing.md#u8), [DM1/R10](guide/04-requirements.md#r10)).
3. **Wrong analysis does not crash; it returns a number.** That makes data work a factory for
   confident error, and leakage is its signature: it feels like a good result
   ([DM1/U2](guide/02-unknowing.md#u2), [DM1/U3](guide/02-unknowing.md#u3)).
4. **The search is part of the result.** Every look at the outcome, every configuration tried, and
   every look at the holdout spends some of the evidence. Count them
   ([DM1/F4](guide/03-feedback.md#f4), [DM1/F5](guide/03-feedback.md#f5), [DM1/F6](guide/03-feedback.md#f6)).
5. **A point estimate is half a result**, and a large biased sample is confidently wrong
   ([DM1/F7](guide/03-feedback.md#f7), [DM1/F8](guide/03-feedback.md#f8)).
6. **Fix the decision, the unit, the outcome, and the moment of prediction first.** They are
   one-way doors that feel cheap in week one, and the errors have prices
   ([DM1/R1](guide/04-requirements.md#r1), [DM1/R2](guide/04-requirements.md#r2), [DM1/R4](guide/04-requirements.md#r4)).
7. **Know when each value became knowable.** Training on data as it looks now is using the future
   ([DM1/R6](guide/04-requirements.md#r6), [DM1/D3](guide/05-design.md#d3)).
8. **A model is a cache of the world at training time**, derived from raw data by one command, with
   its data, code, seed, and environment recorded ([DM1/D2](guide/05-design.md#d2), [DM1/D6](guide/05-design.md#d6)).
9. **Build the evaluation and a dumb baseline before the model; make the pipeline check itself**
   ([DM1/T1](guide/06-testing.md#t1), [DM1/T2](guide/06-testing.md#t2)).
10. **A model in production changes the data it will be trained on next** — the one-way door that
    does not look like one ([DM1/O11](guide/07-operating.md#o11)).

## How to read it

**People:** if you know SE1, read the chapters in order; each is short, because SE1 carries the long
arguments. If you don't, read SE1's first three chapters first (an afternoon), then this, following
the *Reframes* link whenever a principle leans on SE1. After that, use the index below and
[CHECKLISTS.md](CHECKLISTS.md). Terms used precisely are in [GLOSSARY.md](GLOSSARY.md).

**Agents:** read [AGENTS.md](AGENTS.md) — the working agreement for data work — and SE1's
[AGENTS.md](https://github.com/wmacevoy/software-engineering/blob/v1.7/AGENTS.md), which it extends,
before acting in a repository that follows this guide. Then read the chapter that bears on the task
(the question → 4, pipelines and models → 5, evaluation → 3 and 6, production → 7). Cite principles
by ID when you raise a concern or explain a choice.

**Teams adopting it:** follow [ADOPT.md](ADOPT.md), for new work or a repository that already has results.
It protects the evidence first, then vendors both guides: SE1's AGENTS.md as `AGENTS.md`, this guide's beside it
as `AGENTS-DM.md`, both imported from `CLAUDE.md`, with `dm`, `dm.ps1`, and `dm.py` beside SE1's `se`, `se.ps1`,
and `se.py`. `./dm --doctor` says what the data working set lacks, and *none yet* is a true answer.

## Contents

| Chapter                                     | Answers                                                         |
| ------------------------------------------- | --------------------------------------------------------------- |
| [1. Purpose](guide/01-purpose.md)           | what data mining is for, and who is affected without seeing it  |
| [2. Unknowing](guide/02-unknowing.md)       | why data work manufactures confident error: leakage, and the gap between the question and the data |
| [3. Feedback](guide/03-feedback.md)         | what evidence costs, and why searching for it changes it        |
| [4. Requirements](guide/04-requirements.md) | the decision, the unit, the outcome, the prediction point, the price of each error, and bias |
| [5. Design](guide/05-design.md)             | data and models as state and caches; as-of joins, raw as authority, provenance |
| [6. Testing](guide/06-testing.md)           | checks worth trusting, for pipelines and for models             |
| [7. Operating](guide/07-operating.md)       | environments, monitoring without labels, shift, fallbacks, shadow deploys, feedback loops, bias audits |
| [8. Work](guide/08-work.md)                 | CRISP-DM as the six steps, time boxes, the pressure to find something, done |
| [9. People](guide/09-people.md)             | the domain expert, the client, nulls, and who owns a definition |
| [10. Agents](guide/10-agents.md)            | a fast analyst with no stake in whether the number is true      |

| Reference                          | For                                                             |
| ---------------------------------- | --------------------------------------------------------------- |
| [AGENTS.md](AGENTS.md)             | the working agreement for agents doing data work, portable to any repository |
| [CHECKLISTS.md](CHECKLISTS.md)     | every checklist, in the order the work meets them              |
| [GLOSSARY.md](GLOSSARY.md)         | the terms used precisely (SE1's glossary covers the rest)       |
| [READING.md](READING.md)           | the longer arguments, one line each                             |
| [CHANGELOG.md](CHANGELOG.md)       | what changed in each release, by principle ID                   |
| `templates/`                       | starting points: [QUESTION](templates/QUESTION_v1.md), [PLAN](templates/PLAN_v1.md), [DATASHEET](templates/DATASHEET.md), [MODEL_CARD](templates/MODEL_CARD.md), [LEDGER](templates/LEDGER.md), [CONTRIBUTING](templates/CONTRIBUTING.md) — and SE1's [DESIGN](https://github.com/wmacevoy/software-engineering/blob/v1.7/templates/DESIGN_v1.md), [QUEUE](https://github.com/wmacevoy/software-engineering/blob/v1.7/templates/QUEUE.md), and [FINDINGS](https://github.com/wmacevoy/software-engineering/blob/v1.7/templates/FINDINGS.md), unchanged |
| [GAPS.md](GAPS.md)                 | where the guide falls short, and what is not yet tested         |
| `dm`, `dm.py`                      | the doctor for data work: SE1's rows plus this guide's, run in a repository that follows both (`./dm --doctor`); met two real projects, neither with a model ([GAPS 7](GAPS.md#7-the-doctor-has-met-two-repositories)) |

## The running example

SE1's running example is a parking-occupancy service for a stadium's lots: about a hundred lots, one
sensor per lot reporting free spaces every five seconds, a collector that marks each lot's count
`live`, `stale`, or `unreachable`, and an app for drivers and operators. This guide picks it up a few
seasons later. **The collector's history is now the data.**

- **The data.** Years of readings with their status; the event calendar; ticket sales by event; the
  weather; gate scans; attendants' radio logs of lots called full.
- **The client** is the same operations manager, who now asks for "machine learning to find patterns
  in the parking data." Underneath is a decision: two hours before gates open, which lots to staff,
  and which signs to light ([DM1/R1](guide/04-requirements.md#r1)).
- **Three questions** come out of that, one of each kind: *forecast* when each lot fills
  (supervised); *find* the sensors that drift or lie (anomaly detection); and *describe* how lots
  fill across event types (clustering and association — data mining in the narrow sense).
- **Built-in traps.** Final attendance is in the same table as ticket sales, and is not known until
  the event ends ([DM1/R6](guide/04-requirements.md#r6)). Sensors drop off the network most when a lot
  is busiest ([DM1/F8](guide/03-feedback.md#f8)). Two days a year are not 24 hours long
  ([DM1/D5](guide/05-design.md#d5)). The app's forecasts change where drivers park
  ([DM1/O11](guide/07-operating.md#o11)). And a later request to add license-plate reads turns a
  dataset about lots into a dataset about people ([DM1/R9](guide/04-requirements.md#r9)).

Course projects in the [fall 2026 course repository](https://github.com/wmacevoy/data-mining-wmacevoy-fall-2026)
apply these principles to real pipelines — a river-gauge dashboard and a people-counting occupancy
feed — with some violations left in place on purpose.

## Principle index

Every citation carries the guide's major version: `DM1/F5` is principle 5 of *Feedback* in the first
major version, and `SE1/F5` is a different principle in a different guide. Within DM1, a principle
may be reworded, but its ID never moves to a different idea; a retired principle keeps its ID with a
note saying where its content went, and new principles take the next number in their chapter. The
letters are the chapters, as in SE1: **P**urpose, **U**nknowing, **F**eedback, **R**equirements,
**D**esign, **T**esting, **O**perating, **W**ork, **H**umans (people), **A**gents. (Requirement IDs
in your own repository should use a different form, such as `Q2/OUT-1`, so they are never confused with
these.)

**[1. Purpose](guide/01-purpose.md)**

- [**DM1/P1**](guide/01-purpose.md#p1) Data miners produce claims worth acting on, on time, within the data and compute they have, managing discomfort for the team, the client, and the people in the data
- [**DM1/P2**](guide/01-purpose.md#p2) Accurate is not true: a model is correct about a dataset; a claim is true about a world
- [**DM1/P3**](guide/01-purpose.md#p3) The people in the data are not in the room
- [**DM1/P4**](guide/01-purpose.md#p4) A practice that relieves the fear without reducing the risk is worse than nothing, and data work is full of them
- [**DM1/P5**](guide/01-purpose.md#p5) The deliverable is the claim and the means to re-derive it, not the notebook

**[2. Unknowing](guide/02-unknowing.md)**

- [**DM1/U1**](guide/02-unknowing.md#u1) Mining is discovering, in the right order and at the right price, which category each column and each step belongs to
- [**DM1/U2**](guide/02-unknowing.md#u2) Data work manufactures K3, because the failure is a returned value, not an exception
- [**DM1/U3**](guide/02-unknowing.md#u3) Leakage is the canonical K3: it feels like a good result
- [**DM1/U4**](guide/02-unknowing.md#u4) Every model is a chosen falsehood; name the direction it is wrong
- [**DM1/U5**](guide/02-unknowing.md#u5) Estimate the category: time-box the cleaning, estimate the modeling
- [**DM1/U6**](guide/02-unknowing.md#u6) A model always answers; give it a way to say "I don't know"
- [**DM1/U7**](guide/02-unknowing.md#u7) A summary statistic is a concussion: look at the rows
- [**DM1/U8**](guide/02-unknowing.md#u8) People ask Prob(result \| me); the data holds Prob(result \| the rows it counts as like me)
- [**DM1/U9**](guide/02-unknowing.md#u9) Whitespace is free on the inputs and spent on the outcome
- [**DM1/U10**](guide/02-unknowing.md#u10) "None found" is a claim about the search
- [**DM1/U11**](guide/02-unknowing.md#u11) Whether a result is worth acting on is a judgment: a metric taken for it is K3
- [**DM1/U12**](guide/02-unknowing.md#u12) Trust data about observations over data about reflection: asking why is a K3 factory

**[3. Feedback](guide/03-feedback.md)**

- [**DM1/F1**](guide/03-feedback.md#f1) Tests of code are free; tests of a claim are not
- [**DM1/F2**](guide/03-feedback.md#f2) The cost of checking a result is designed, not given
- [**DM1/F3**](guide/03-feedback.md#f3) Push each class of error down to the cheapest loop that can catch it
- [**DM1/F4**](guide/03-feedback.md#f4) The search is part of the result
- [**DM1/F5**](guide/03-feedback.md#f5) The holdout is a consumable
- [**DM1/F6**](guide/03-feedback.md#f6) Count your looks, and say the number
- [**DM1/F7**](guide/03-feedback.md#f7) A point estimate is half a result
- [**DM1/F8**](guide/03-feedback.md#f8) A large sample does not rescue a biased one; it disguises it
- [**DM1/F9**](guide/03-feedback.md#f9) When a metric becomes a target, it stops measuring — and the search is the hardest optimizer you will meet
- [**DM1/F10**](guide/03-feedback.md#f10) A faithful summary can still lose the finding: check what left, not only what stayed

**[4. Requirements](guide/04-requirements.md)**

- [**DM1/R1**](guide/04-requirements.md#r1) Keep the decision; test the question
- [**DM1/R2**](guide/04-requirements.md#r2) Fix the unit, the outcome, and the prediction point before anything else
- [**DM1/R3**](guide/04-requirements.md#r3) Specific and wrong: name the metric, the baseline, and the bar before you look
- [**DM1/R4**](guide/04-requirements.md#r4) The confusion matrix has a price
- [**DM1/R5**](guide/04-requirements.md#r5) Every column can be absent, late, wrong, or lying — and missingness is data
- [**DM1/R6**](guide/04-requirements.md#r6) Know, per column, when its value became knowable
- [**DM1/R7**](guide/04-requirements.md#r7) A label is an opinion with a timestamp
- [**DM1/R8**](guide/04-requirements.md#r8) The operating envelope includes rows, labels, compute, and expert hours
- [**DM1/R9**](guide/04-requirements.md#r9) Ethics, law, and security are requirements, and the people in the data never filed one
- [**DM1/R10**](guide/04-requirements.md#r10) Bias is always a requirement, alongside ethics, law, and security
- [**DM1/R11**](guide/04-requirements.md#r11) Write down who the model is not for
- [**DM1/R12**](guide/04-requirements.md#r12) Name the kind of claim first: predicted, described, explained, or extracted

**[5. Design](guide/05-design.md)**

- [**DM1/D1**](guide/05-design.md#d1) Data is the state, and a trained model is state too
- [**DM1/D2**](guide/05-design.md#d2) A trained model is a cache of the world at training time
- [**DM1/D3**](guide/05-design.md#d3) There is no single "now" in a training set: join as of the prediction point
- [**DM1/D4**](guide/05-design.md#d4) Make every stage idempotent: re-running it gives the same table
- [**DM1/D5**](guide/05-design.md#d5) Pick one clock and one definition of "day," and write them down
- [**DM1/D6**](guide/05-design.md#d6) Raw is the authority; everything else is derived by one command
- [**DM1/D7**](guide/05-design.md#d7) Pure transforms over frames; the notebook only calls them
- [**DM1/D8**](guide/05-design.md#d8) A model's complexity must be paid for by its margin over the baseline
- [**DM1/D9**](guide/05-design.md#d9) One definition per feature: training and serving compute it with the same code
- [**DM1/D10**](guide/05-design.md#d10) A model's contract says what it was trained on and how it fails
- [**DM1/D11**](guide/05-design.md#d11) A metric someone tracks is a promise
- [**DM1/D12**](guide/05-design.md#d12) A model is capital you carry, with no expiry date printed on it
- [**DM1/D13**](guide/05-design.md#d13) Provenance at every layer: value, dataset, label, result

**[6. Testing](guide/06-testing.md)**

- [**DM1/T1**](guide/06-testing.md#t1) Build the evaluation before the model, and enter a dumb baseline first
- [**DM1/T2**](guide/06-testing.md#t2) A reliable pipeline checks itself: assert what joins and filters break
- [**DM1/T3**](guide/06-testing.md#t3) A check that cannot fail proves nothing — including a fixture too small to exercise the estimator
- [**DM1/T4**](guide/06-testing.md#t4) A number that moves for no reason trains everyone to ignore it
- [**DM1/T5**](guide/06-testing.md#t5) Decide which results must be identical and which must agree within a tolerance
- [**DM1/T6**](guide/06-testing.md#t6) A fixture must fail the way real data fails
- [**DM1/T7**](guide/06-testing.md#t7) Ask for the diff in the numbers, not only the diff in the code
- [**DM1/T8**](guide/06-testing.md#t8) Test a model with properties it must have, not only with a score
- [**DM1/T9**](guide/06-testing.md#t9) Evaluate by slice; the aggregate hides the subgroup that fails
- [**DM1/T10**](guide/06-testing.md#t10) When someone else owns "correct," compare against their implementation
- [**DM1/T11**](guide/06-testing.md#t11) Unsupervised results have no answer key: test their stability
- [**DM1/T12**](guide/06-testing.md#t12) For retrieval and extraction, a full reading of some cases is the baseline
- [**DM1/T13**](guide/06-testing.md#t13) A run is an experiment: say what would count against the claim, start small, and record in proportion to the cost of running it again

**[7. Operating](guide/07-operating.md)**

- [**DM1/O1**](guide/07-operating.md#o1) Record the environment and the hardware with every result; numbers depend on both
- [**DM1/O2**](guide/07-operating.md#o2) CI runs the pipeline from raw on the fixture, every change
- [**DM1/O3**](guide/07-operating.md#o3) A model in production needs signals that do not wait for labels
- [**DM1/O4**](guide/07-operating.md#o4) Name the shift: inputs, base rates, or the relationship itself
- [**DM1/O5**](guide/07-operating.md#o5) Every model has a fallback that is not a model
- [**DM1/O6**](guide/07-operating.md#o6) Run a new model in shadow before it decides anything; a retrain is a deploy
- [**DM1/O7**](guide/07-operating.md#o7) Version datasets, and change a feature in steps that can each be undone
- [**DM1/O8**](guide/07-operating.md#o8) Secure the data, the copies, and the model
- [**DM1/O9**](guide/07-operating.md#o9) Unbiased is not a default: audit for bias like an attacker, before and after deploy
- [**DM1/O10**](guide/07-operating.md#o10) Test at the size you will have: a sample hides what the full table does
- [**DM1/O11**](guide/07-operating.md#o11) A model in production changes the data it will be trained on next

**[8. Work](guide/08-work.md)**

- [**DM1/W1**](guide/08-work.md#w1) CRISP-DM is the six steps, with the loop back to the question drawn in
- [**DM1/W2**](guide/08-work.md#w2) Slow down around the one-way decisions: the question, the unit, the outcome, the split, the labels
- [**DM1/W3**](guide/08-work.md#w3) Build a thin end-to-end slice first: one source, one feature, a baseline, the real chart
- [**DM1/W4**](guide/08-work.md#w4) Time-box exploration and cleaning, and end each box on a fence
- [**DM1/W5**](guide/08-work.md#w5) The deadline pathology here is finding something; change the terrain, not the willpower
- [**DM1/W6**](guide/08-work.md#w6) Write down what you tried that did not work
- [**DM1/W7**](guide/08-work.md#w7) Done means someone else regenerated the number from raw and could argue with it

**[9. People](guide/09-people.md)**

- [**DM1/H1**](guide/09-people.md#h1) The domain expert is a feedback loop no test replaces; show them rows, not summaries
- [**DM1/H2**](guide/09-people.md#h2) Read your assumptions to the client, in their words
- [**DM1/H3**](guide/09-people.md#h3) Report a null the same week you find it, in the same tone as a win
- [**DM1/H4**](guide/09-people.md#h4) One owner per definition: two people cleaning the same dataset produce three datasets
- [**DM1/H5**](guide/09-people.md#h5) Whoever owns the definition owns the conclusion
- [**DM1/H6**](guide/09-people.md#h6) Present uncertainty in the client's units and decisions, not the model's
- [**DM1/H7**](guide/09-people.md#h7) A model demo is a test: choose the examples before the meeting, and include failures
- [**DM1/H8**](guide/09-people.md#h8) Say out loud what you do not believe about the number; here, disbelief is data

**[10. Agents](guide/10-agents.md)**

- [**DM1/A1**](guide/10-agents.md#a1) Producing an analysis got cheaper faster than knowing it is right
- [**DM1/A2**](guide/10-agents.md#a2) Code that throws is safe; code that returns a number is not
- [**DM1/A3**](guide/10-agents.md#a3) An agent's tries count as your looks
- [**DM1/A4**](guide/10-agents.md#a4) Keep the holdout out of the agent's reach
- [**DM1/A5**](guide/10-agents.md#a5) Spend the speed on checks: fixtures, assertions, the boring second case
- [**DM1/A6**](guide/10-agents.md#a6) Point review at the data handling
- [**DM1/A7**](guide/10-agents.md#a7) You present it, so you own it
- [**DM1/A8**](guide/10-agents.md#a8) Data sent to an agent has left the room
- [**DM1/A9**](guide/10-agents.md#a9) A language model in the pipeline is a model: version it, sample it, and check its test set was not in its training
- [**DM1/A10**](guide/10-agents.md#a10) When reading the data is the task, the agent must read it: say what then has to be true

## Reading SE1 as a data miner

Each SE1 principle below is reframed for data by the DM1 principles it points to. **SE1 principles
not listed apply to data work unchanged** — the pipeline is software, and everything SE1 says about
software applies to it.

- [SE1/P1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p1) → [DM1/P1](guide/01-purpose.md#p1)
- [SE1/P2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p2) → [DM1/P2](guide/01-purpose.md#p2)
- [SE1/P3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p3) → [DM1/P3](guide/01-purpose.md#p3)
- [SE1/P4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p4) → [DM1/P4](guide/01-purpose.md#p4)
- [SE1/P5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p5) → [DM1/P5](guide/01-purpose.md#p5)
- [SE1/U2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u2) → [DM1/U1](guide/02-unknowing.md#u1)
- [SE1/U3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u3) → [DM1/U2](guide/02-unknowing.md#u2)
- [SE1/U4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u4) → [DM1/U4](guide/02-unknowing.md#u4)
- [SE1/U5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u5) → [DM1/U5](guide/02-unknowing.md#u5)
- [SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7) → [DM1/U2](guide/02-unknowing.md#u2), [DM1/U6](guide/02-unknowing.md#u6)
- [SE1/U8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u8) → [DM1/U9](guide/02-unknowing.md#u9)
- [SE1/U10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u10) → [DM1/U11](guide/02-unknowing.md#u11)
- [SE1/F1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f1) → [DM1/F1](guide/03-feedback.md#f1)
- [SE1/F2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f2) → [DM1/F2](guide/03-feedback.md#f2)
- [SE1/F3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f3) → [DM1/F3](guide/03-feedback.md#f3)
- [SE1/F5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f5) → [DM1/F9](guide/03-feedback.md#f9)
- [SE1/R1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r1) → [DM1/R1](guide/04-requirements.md#r1)
- [SE1/R2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r2) → [DM1/R3](guide/04-requirements.md#r3)
- [SE1/R4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r4) → [DM1/R4](guide/04-requirements.md#r4)
- [SE1/R6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r6) → [DM1/R5](guide/04-requirements.md#r5)
- [SE1/R7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r7) → [DM1/R8](guide/04-requirements.md#r8)
- [SE1/R8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r8) → [DM1/R9](guide/04-requirements.md#r9), [DM1/R10](guide/04-requirements.md#r10)
- [SE1/R9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r9) → [DM1/R11](guide/04-requirements.md#r11)
- [SE1/D1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d1) → [DM1/D1](guide/05-design.md#d1)
- [SE1/D2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d2) → [DM1/D2](guide/05-design.md#d2)
- [SE1/D3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d3) → [DM1/D3](guide/05-design.md#d3)
- [SE1/D4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d4) → [DM1/D4](guide/05-design.md#d4)
- [SE1/D5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d5) → [DM1/D5](guide/05-design.md#d5)
- [SE1/D6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d6) → [DM1/D6](guide/05-design.md#d6)
- [SE1/D7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d7) → [DM1/D7](guide/05-design.md#d7)
- [SE1/D8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d8) → [DM1/D8](guide/05-design.md#d8)
- [SE1/D9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d9) → [DM1/D9](guide/05-design.md#d9)
- [SE1/D10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d10) → [DM1/D10](guide/05-design.md#d10)
- [SE1/D11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d11) → [DM1/D11](guide/05-design.md#d11)
- [SE1/D12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d12) → [DM1/D12](guide/05-design.md#d12)
- [SE1/D13](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d13) → [DM1/D13](guide/05-design.md#d13)
- [SE1/T2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t2) → [DM1/T2](guide/06-testing.md#t2)
- [SE1/T3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t3) → [DM1/U10](guide/02-unknowing.md#u10), [DM1/T3](guide/06-testing.md#t3)
- [SE1/T4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t4) → [DM1/T4](guide/06-testing.md#t4)
- [SE1/T5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t5) → [DM1/T5](guide/06-testing.md#t5)
- [SE1/T6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t6) → [DM1/T6](guide/06-testing.md#t6)
- [SE1/T8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t8) → [DM1/T1](guide/06-testing.md#t1), [DM1/T12](guide/06-testing.md#t12)
- [SE1/T9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t9) → [DM1/T5](guide/06-testing.md#t5)
- [SE1/T10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t10) → [DM1/T8](guide/06-testing.md#t8)
- [SE1/T11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t11) → [DM1/T10](guide/06-testing.md#t10)
- [SE1/O1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o1) → [DM1/O1](guide/07-operating.md#o1)
- [SE1/O2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o2) → [DM1/O1](guide/07-operating.md#o1)
- [SE1/O3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o3) → [DM1/O2](guide/07-operating.md#o2)
- [SE1/O4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o4) → [DM1/O3](guide/07-operating.md#o3)
- [SE1/O5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o5) → [DM1/O5](guide/07-operating.md#o5)
- [SE1/O6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o6) → [DM1/O6](guide/07-operating.md#o6)
- [SE1/O7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o7) → [DM1/O7](guide/07-operating.md#o7)
- [SE1/O8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o8) → [DM1/O8](guide/07-operating.md#o8), [DM1/O9](guide/07-operating.md#o9)
- [SE1/O10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o10) → [DM1/O10](guide/07-operating.md#o10)
- [SE1/O11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o11) → [DM1/O11](guide/07-operating.md#o11)
- [SE1/W1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w1) → [DM1/W1](guide/08-work.md#w1)
- [SE1/W4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w4) → [DM1/W2](guide/08-work.md#w2)
- [SE1/W6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w6) → [DM1/W3](guide/08-work.md#w3)
- [SE1/W7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w7) → [DM1/W5](guide/08-work.md#w5)
- [SE1/W8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w8) → [DM1/W4](guide/08-work.md#w4)
- [SE1/W9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w9) → [DM1/W4](guide/08-work.md#w4)
- [SE1/W10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w10) → [DM1/W6](guide/08-work.md#w6)
- [SE1/W11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w11) → [DM1/W7](guide/08-work.md#w7)
- [SE1/W13](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w13) → [DM1/W6](guide/08-work.md#w6)
- [SE1/H1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h1) → [DM1/H5](guide/09-people.md#h5)
- [SE1/H4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h4) → [DM1/H4](guide/09-people.md#h4), [DM1/H8](guide/09-people.md#h8)
- [SE1/H5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h5) → [DM1/H1](guide/09-people.md#h1)
- [SE1/H6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h6) → [DM1/H2](guide/09-people.md#h2)
- [SE1/H8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h8) → [DM1/H7](guide/09-people.md#h7)
- [SE1/H9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h9) → [DM1/H3](guide/09-people.md#h3)
- [SE1/A1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a1) → [DM1/A1](guide/10-agents.md#a1)
- [SE1/A2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a2) → [DM1/A2](guide/10-agents.md#a2)
- [SE1/A5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a5) → [DM1/A5](guide/10-agents.md#a5)
- [SE1/A6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a6) → [DM1/A6](guide/10-agents.md#a6)
- [SE1/A7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a7) → [DM1/A7](guide/10-agents.md#a7)
- [SE1/A9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a9) → [DM1/A3](guide/10-agents.md#a3)
- [SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11) → [DM1/U7](guide/02-unknowing.md#u7)

## Twelve questions

If you remember nothing else, each of these can be asked out loud, and each has an answer.

| Ask                                                                   | Checks |
| --------------------------------------------------------------------- | ------ |
| "Which decision does this change — and what if the answer were the opposite?" | [DM1/R1](guide/04-requirements.md#r1) |
| "What is one row, what exactly is the outcome, and when is the prediction made?" | [DM1/R2](guide/04-requirements.md#r2) |
| "Could the model have known this at the moment it would have to predict?" | [DM1/U3](guide/02-unknowing.md#u3) |
| "Which mistake is worse, by about how much, and who pays for each?"   | [DM1/R4](guide/04-requirements.md#r4) |
| "Would this result have survived if it were the only thing we tried?" | [DM1/F4](guide/03-feedback.md#f4) |
| "How many times has this holdout been looked at?"                     | [DM1/F5](guide/03-feedback.md#f5) |
| "What is the interval — and does the difference survive a different seed?" | [DM1/F7](guide/03-feedback.md#f7) |
| "For whom is this number systematically wrong — and who decided that was acceptable?" | [DM1/R10](guide/04-requirements.md#r10) |
| "What does the dumbest reasonable method score on this?"              | [DM1/T1](guide/06-testing.md#t1) |
| "If this join multiplied the rows, which line would fail?"            | [DM1/T2](guide/06-testing.md#t2) |
| "Where did this number come from, and could I regenerate it without asking anyone?" | [DM1/D13](guide/05-design.md#d13) |
| "What will this model change about the data we train the next one on?" | [DM1/O11](guide/07-operating.md#o11) |

## Changing this guide

The guide follows its own advice. Run `python3 -I tools/check.py` before committing: it fails if a link or
anchor is broken, a principle has no *New* or *Reframes* line, an SE1 citation points at the wrong
place, or the index or the SE1 map above no longer matches the chapters. It also checks that every SE1 ID and
anchor exists in `vendor/se1`, that `vendor/se1` is the release this guide is pinned to, and that the copies of SE1's
files here equal it. `python3 -I tools/check.py --print` prints the index
and the SE1 map as they should read, to paste in after adding or rewording a principle. Reword
freely; never reuse an ID for a different idea. `python3 -I tools/test_check.py` plants a breakage per rule in
a copy of this repository, and `python3 -I tools/test_dm.py` runs the doctor's tests, every row seen red on a
fixture built to fail it. CI runs all three on three platforms and two Pythons. Python 3.9 or later and git
are all any of it needs.

**This guide is pinned to one SE1 release**, named once, by `SE_RELEASE` in `tools/check.py`, and
every SE1 link, llms.txt, and CI name it: a reader lands on the text that was read, not on whatever
SE1's `main` says now. Bumping it is that one edit, a rewrite of the links, a fresh copy of `vendor/se1`, a walk through SE1's
CHANGELOG for every ID this guide reframes, and a CHANGELOG entry here saying what was reframed,
declined, or confirmed, by SE1 ID. A new SE1 principle that data work sharpens is a new DM1 principle
in the matching chapter; one that applies to data unchanged is left off the SE1 map on purpose.

## Why to trust this, and why to doubt it

Who made it, when, and what would show it wrong, as [SE1/U9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u9)
asks of any rule.

- **Who, and when.** One author, teaching from it, with Claude models drafting: the course document
  in August 2026 (Claude Opus 5), the refactor into this guide on 2026-10-27 (a Claude model; which
  one was not recorded), the review against SE1 v1.3 on 2026-10-28 (Claude Fable 5.1), the review against SE1 v1.4 on
  2026-11-03 (Claude Opus 5.5), the review against SE1 v1.5 on 2026-11-04, and the reviews against SE1 v1.6 and v1.7 on 2026-11-05 (all the same model). Every
  commit since the repository was initialized that day carries the agent's trailer, and AGENTS.md
  says so in its header.
- **What it rests on.** A course document, the author's teaching, SE1's trials and field reports by
  reference, and two field reports of its own ([GAPS.md](GAPS.md)). The first, one private document
  review with nothing trained and nothing predicted, went through the 93 principles of that date: by
  one model's judgment, 26 held, 20 needed extending or changing, 20 fit by argument only, and 27
  didn't reach it (a review since incorporated and removed, and redacted for confidentiality: [GAPS.md](GAPS.md)). Five principles came out of it, and rest on that one
  project. The second is the course's migration, still under way, with one of its five projects done. [A trial of 2026-11-05](https://github.com/wmacevoy/data-mining-self-improvement/blob/5032133b6e/trials/2026-11-05-quickref/RESULTS.md) ran Claude agents through one synthetic task with parts of this guide loaded, and trained models on it. Nothing has tested a principle on a real project's trained model, spent holdout, or production, and no person and no
  other model family has reviewed them. The reframings were written against one SE1 commit and re-read
  against v1.3, v1.4, v1.5, and v1.6 only where SE1 changed; v1.7 changed no principle's meaning; the rest were not re-read.
- **What has been seen to fail.** `tools/check.py`, on a breakage planted per rule
  (`tools/test_check.py`), including SE1 pinned wrongly, a copy of SE1 with no tag to read, and an AGENTS.md
  citing an ID its release does not define. A check never seen red proves nothing
  ([SE1/T3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t3)), so these were.
  `dm --doctor`, on each real repository it has met: each found bugs in it that its fixtures had not, one
  of them a fix for such a bug that repeated it ([GAPS.md](GAPS.md#7-the-doctor-has-met-two-repositories)).
- **What has not.** Whether a reader who has not read SE1 can follow a *Reframes* link and come away
  with the idea. Whether an agent given AGENTS.md here works differently from one given SE1's
  alone. Most checklists, the model card, and most *Asks*. The question, plan, and datasheet templates
  have been filled in on one descriptive question and found to fit badly, which R12 answers; that
  answer is itself untried.
- **What would show it wrong.** A course project where following a principle here made the analysis
  worse, or cost more than it caught. A holdout an agent reached despite
  [DM1/A4](guide/10-agents.md#a4). A reframing that contradicts its SE1 source about software, which
  by the section below is a bug here. An agent of another family for which the working agreement
  makes the work worse.
- **How to check it yourself.** `python3 -I tools/check.py`, which reads SE1 from `vendor/se1`; the SE1
  map above, read against SE1's text at v1.7; and [CHANGELOG.md](CHANGELOG.md), which says by ID
  what was reframed, declined, confirmed, and not reviewed.

## Where this came from

Refactored from the course document `DATA-SCIENCE.md` in the
[fall 2026 data mining course](https://github.com/wmacevoy/data-mining-wmacevoy-fall-2026), itself a
rewrite of an early draft of SE1, and reorganized to follow SE1's chapters and principle IDs. See
[READING.md](READING.md) for the sources the arguments lean on.

**This repository is the authority for these principles** ([SE1/D6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d6)).
**SE1 is the authority for everything this guide reframes**: where a DM1 principle and the SE1
principle it reframes seem to disagree about software, SE1 wins, and the DM1 principle has a bug.

Licensed under the [MIT License](LICENSE).
