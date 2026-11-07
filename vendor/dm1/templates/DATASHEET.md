# Datasheet — _source name_

Where this data came from and what it means, so it can be checked later without asking anyone
([DM1/D13](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d13)). One per
source. After Gebru et al., *Datasheets for Datasets*, cut to what the guide's principles ask for.

## Origin

- **Source:** _URL, system, or person_ · **License / terms:** _…_
- **Collected by, of whom, for what purpose:** _…_
- **Pulled:** _how (command), how often, and where the raw copy lives_
- **Revisions:** _can the source change values after the fact? (provisional data, corrections)_
- **Excluded before it reached us:** _what, by whom, why — usually the most important line here_
- **This sheet:** written by _name or agent_ on _date_; each line above is measured, told by _whom_,
  or *(inferred)*. **What would show it wrong:** _the query or assertion that fails if a Meaning,
  Units, or Knowable at entry below is wrong, and when it last ran_
  ([SE1/U9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u9))

## People

- **Is any row about a person, or linkable to one?** _…_
- **Consent and permitted uses:** _…_ · **Retention:** _…_
  ([DM1/R9](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r9))

## Columns

*Knowable at* is when the value became available to someone who could act on it — the column that
prevents leakage ([DM1/R6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r6)).

| Column | Meaning | Units / clock | Sentinels and defaults | Knowable at | Absent / late / wrong / lying |
| ------ | ------- | ------------- | ---------------------- | ----------- | ----------------------------- |
| _ts_ | _reading time_ | _UTC_ | _none_ | _ts + upload lag (up to …)_ | _…_ |

## Documents

For a source made of documents, in place of *Columns*: one row per kind of document in it. What each file holds, how each page was made, and what might be printed on it
([DM1/U7](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u7),
[DM1/O8](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o8)).

| Part | How each page was made | Text layer? | Identifiers printed | Knowable at | Absent / late / wrong / lying |
| ---- | ---------------------- | ----------- | ------------------- | ----------- | ----------------------------- |
| _a letter_ | _scanned_ | _none: recognition_ | _a signature_ | _its date_ | _…_ |

## Labels

If this source supplies a target ([DM1/R7](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r7)):

- **Applied by, under which instructions (version):** _…_
- **Agreement between two labelers, on a sample:** _…_
- **A proxy for:** _…_

## Known problems

- _yyyy-mm-dd_ — _what, the rows affected, and what the pipeline does about it_
