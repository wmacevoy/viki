# Contributing

How to work on this repository — for a new teammate and for an agent, who both arrive knowing
nothing ([SE1/A3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a3)).
Keep it short and true: a change that makes a line here untrue updates it in the same change.

## Run it

- **Prerequisites:** _tools and versions, and how to get them_
- **Raw to report:** `_command_` — _every reported number, from raw data; about how long it takes at
  full size_ ([DM1/D6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d6),
  [DM1/O10](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o10))
- **Main product:** `_path_` — _what this work delivers; the command above makes it, and if it does
  not, say what does_ ([DM1/W7](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/08-work.md#w7))
- **Fetch / refresh raw data:** `_command_` — _what it contacts, what it records about the pull_
- **Reset derived data:** `_command_` — _what it deletes; raw is never deleted_

## Check it

- **The full check:** `_command_` — the fixture through every stage, the assertions, the property
  tests, and the golden-output diff; the same command CI runs
  ([DM1/O2](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o2))
- **Success prints:** `_the line that means it passed_`
- **The doctor:** `./dm --doctor` — SE1's rows and this guide's: both agreements loaded, the
  question's one-way doors, the holdout's reach, every column's knowable-at, raw read-only, the
  ledger's writer; *none yet* is a true answer. One check reads one question's directory: in a repository of several,
  `./dm --doctor --repo <directory>`, with that directory's own `CONTRIBUTING.md` and `docs/`
  ([DM1/A2](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/10-agents.md#a2))
- **Golden output:** `_path_` — regenerate with `_command_` and review the diff
  ([DM1/T7](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/06-testing.md#t7))

## Data rules for this repository

- **Raw data:** lives in `_path_`, or the pattern naming it (`inputs/*.zip`), read-only; never
  hand-edited ([DM1/D6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d6)).
- **The ledger:** `_path_` — _what each line records: configurations scored, or searches run and pages
  read_ ([DM1/F6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/03-feedback.md#f6)).
- **The holdout:** lives in `_path_`; _who_ may read it; spent only at a decision, and logged in the
  ledger ([DM1/F5](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/03-feedback.md#f5)).
- **Data about people:** _what is collected, what may leave this machine, and what agents may see_
  ([DM1/A8](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/10-agents.md#a8)).
- **Clock:** _UTC inside; local time at the edges; a "day" is …_
  ([DM1/D5](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d5)).

## Where things are

- **Working set:** `docs/`, or `<directory>/docs/` in a repository of several questions or one whose
  `docs/` is a published site — `QUESTION_vN`, `PLAN_vN`, datasheets, model cards, the ledger, `QUEUE`,
  `FINDINGS` ([DM1/W6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/08-work.md#w6))
- **Agents:** read `AGENTS-DM.md` (copied from the [data mining guide](https://github.com/wmacevoy/data-mining/blob/v1.3/AGENTS.md))
  and the SE1 `AGENTS.md` it extends, both imported from `CLAUDE.md`, and this file, before acting.
  `dm`, `dm.ps1`, and `dm.py` sit beside SE1's `se.py`.
