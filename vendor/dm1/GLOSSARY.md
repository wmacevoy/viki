# Glossary

The terms this guide uses precisely, beyond those in
[SE1's glossary](https://github.com/wmacevoy/software-engineering/blob/v1.7/GLOSSARY.md) — K0–K4,
K3 factory, cache, contract, blast radius, and the rest mean the same here. One thing, one word
([DM1/D9](guide/05-design.md#d9)): if you find the guide using one of these loosely, that is a bug.

| Term                    | Meaning                                                                                              |
| ----------------------- | ---------------------------------------------------------------------------------------------------- |
| **Abstention**          | A model returning *no answer* instead of a low-confidence one ([DM1/U6](guide/02-unknowing.md#u6)). |
| **As-of join** (point-in-time join) | A join that gives each row the latest value that was *available* at that row's prediction point, not the value known today ([DM1/D3](guide/05-design.md#d3)). |
| **Aggregation bias**    | Error that comes from one model pooling groups whose relationship differs; small groups get answers shaped by large ones ([DM1/U8](guide/02-unknowing.md#u8)). |
| **Bias**                | A systematic gap between what the data estimates and what is true for the people or cases it is used on — measured, or present unmeasured ([DM1/R10](guide/04-requirements.md#r10)). Not the statistical sense of an estimator's expected error alone, though that is one source. |
| **Baseline**            | The dumbest reasonable method — the mean, the majority class, "same as last time" — scored on the same split as every model ([DM1/T1](guide/06-testing.md#t1)). |
| **Calibration**         | Agreement between stated confidence and observed frequency: of predictions made at 0.8, about 80% are right. |
| **Claim**               | A statement about the world that someone might act on — as opposed to a score, which is a statement about a dataset ([DM1/P2](guide/01-purpose.md#p2)). |
| **Concept shift**       | A change in the relationship between inputs and outcome. Invisible without labels ([DM1/O4](guide/07-operating.md#o4)). |
| **Covariate shift**     | A change in the distribution of the inputs ([DM1/O4](guide/07-operating.md#o4)).                     |
| **Cross-validation**    | Repeatedly fitting on part of the development data and scoring on the rest. A development tool, not a substitute for a holdout. |
| **Data version**        | An identifier — a hash, a dated snapshot, a tag — that names exactly which data a result was computed from ([DM1/O7](guide/07-operating.md#o7)). |
| **Datasheet**           | The provenance record for one data source ([template](templates/DATASHEET.md)).                      |
| **Essential K1**        | A known unknown no amount of looking settles, because the answer is a judgment someone makes on meeting the thing ([DM1/U11](guide/02-unknowing.md#u11)). |
| **Event (the unit)**    | In the running example, one game or concert. Often the number that limits you, as opposed to rows ([DM1/R8](guide/04-requirements.md#r8)). |
| **Feedback loop**       | A deployed model changing the data its successors are trained on ([DM1/O11](guide/07-operating.md#o11)). |
| **Experiment**          | One run of the work: one data point for one question, with what would count against the claim said first ([DM1/T13](guide/06-testing.md#t13)). |
| **Fence**               | Something proved at the end of a time box — a baseline scored, an expert's objection, a cold run from raw — as opposed to something attended ([DM1/W4](guide/08-work.md#w4)). |
| **Fixture**             | A small dataset committed with the repository, with expected outputs worked out by hand ([DM1/T6](guide/06-testing.md#t6)). |
| **Forking paths**       | The choices an analysis could have made differently; each one taken in response to the data spends evidence, even if only one analysis was run ([DM1/F4](guide/03-feedback.md#f4)). |
| **Fundamental shape**   | The versioned and heavy aesthetic that decides what belongs, through a few guidelines held by one named person; a sign it is working is that someone can guess the next pipeline from the last ([DM1/D9](guide/05-design.md#d9)). |
| **Golden output**       | The fixture's full output, committed, so a change's effect on the numbers is a reviewable diff ([DM1/T7](guide/06-testing.md#t7)). |
| **Holdout**             | Data set aside before exploration and used only at a decision, to estimate how a final choice performs on data it never influenced. Consumed by being looked at *and reacted to* ([DM1/F5](guide/03-feedback.md#f5)). |
| **Interval**            | The range a reported number would plausibly take under resampling, reseeding, or resplitting ([DM1/F7](guide/03-feedback.md#f7)). |
| **Judged**              | Said of a requirement: only a named person can pass it, on a stated occasion; the bar and the tests may veto it ([DM1/U11](guide/02-unknowing.md#u11)). |
| **Kind of claim**       | Predicted, described, explained, or extracted: what decides what a claim can be scored against ([DM1/R12](guide/04-requirements.md#r12)). |
| **Knowable at**         | When a value became available to someone who could have acted on it ([DM1/R6](guide/04-requirements.md#r6)). |
| **Full reading**        | A person or model reading a whole document: the baseline for retrieval and extraction ([DM1/T12](guide/06-testing.md#t12)). |
| **Label**               | The target value a supervised model learns to predict: an opinion with a timestamp ([DM1/R7](guide/04-requirements.md#r7)). |
| **Leakage**             | Information about the outcome reaching the model in training that will not be available at prediction time ([DM1/U3](guide/02-unknowing.md#u3)). |
| **Ledger**              | A machine-appended log of every configuration scored, or every search run and page read ([DM1/F6](guide/03-feedback.md#f6), [template](templates/LEDGER.md)). |
| **Look**                | One examination of an outcome that could influence what you do next. Counted in the ledger. |
| **Membership inference** | An attack that determines whether a particular record was in a model's training data. |
| **Missing at random**   | Missingness unrelated to the missing value itself, given what is observed. Usually a K4 ([DM1/U4](guide/02-unknowing.md#u4)). |
| **Mission**             | The intentions the work is aimed at, stated broadly; in data work, the decision it serves. Only the client can change it ([DM1/D9](guide/05-design.md#d9)). |
| **Model card**          | A model's contract ([DM1/D10](guide/05-design.md#d10), [template](templates/MODEL_CARD.md)).       |
| **Null result**         | A finding that the effect or improvement is not there, at this sample size. A result, not a failure ([DM1/H3](guide/09-people.md#h3)). |
| **Out of envelope**     | An input outside the ranges or categories seen in training ([DM1/U6](guide/02-unknowing.md#u6)).    |
| **Outcome**             | What is being predicted, written so it could be a query ([DM1/R2](guide/04-requirements.md#r2)).     |
| **Prediction point**    | The moment a prediction is made, which decides what may be used to make it ([DM1/R2](guide/04-requirements.md#r2)). |
| **Project**             | Not a term here. It hid three things: the repository (code, commits, CI, agreements), the question (a decision and its evidence), and the experiment (one run). |
| **Prior (label) shift** | A change in how common each outcome is ([DM1/O4](guide/07-operating.md#o4)).                         |
| **Proxy**               | A measured quantity standing in for the one you care about. Detaches when optimized ([DM1/F9](guide/03-feedback.md#f9)). |
| **Raw**                 | Data exactly as it arrived from its source, immutable; the authority everything else is derived from ([DM1/D6](guide/05-design.md#d6)). |
| **Reference class**     | The set of cases a number about one case actually describes: the rows the model cannot tell apart from it ([DM1/U8](guide/02-unknowing.md#u8)). |
| **Re-identification**   | Recovering who a "de-identified" record is about by joining it with other data.                      |
| **Selection bias**      | Who is in the rows differs from who the result will be applied to ([DM1/F8](guide/03-feedback.md#f8)). |
| **Sentinel**            | A value that means "no value" — `-999`, `0`, `1970-01-01` — stored where a measurement would be.      |
| **Shadow mode**         | Running a new model on live inputs and logging its predictions without letting it decide ([DM1/O6](guide/07-operating.md#o6)). |
| **Slice**               | A subgroup the model is evaluated on separately, chosen before results ([DM1/T9](guide/06-testing.md#t9)). |
| **Split**               | The division of rows into training, validation, and holdout. Grouped so related rows stay together; time-ordered when time matters. |
| **Train/serve skew**    | A feature computed differently in training than where predictions are served ([DM1/D9](guide/05-design.md#d9)). |
| **Text layer**          | The machine-readable text in a PDF, which is a summary of the page and may be missing or wrong ([DM1/U7](guide/02-unknowing.md#u7)). |
| **Unit of analysis**    | What one row is ([DM1/R2](guide/04-requirements.md#r2)).                                             |
| **Validation set**      | Development data used to choose among models; reused freely, and so not an unbiased estimate.        |
| **Whitespace**          | Time with no agenda, in SE1/U8's sense. Free when spent on the inputs; a look, counted in the ledger, when spent on the outcome ([DM1/U9](guide/02-unknowing.md#u9)). |
