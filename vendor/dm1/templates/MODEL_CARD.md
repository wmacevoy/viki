# Model card — _model name_, _version_

The model's contract: what it was trained on, what it is for, how well it does by slice, and how it
fails ([DM1/D10](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d10)). After
Mitchell et al., *Model Cards for Model Reporting*. Updated with every retrain.

## Identity

Everything needed to rebuild it ([DM1/D6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d6)):

- **Data version:** _…_ · **Code commit:** _…_ · **Seed(s):** _…_ · **Environment:** _path to the
  recorded package list and hardware_
- **Trained through:** _date — the model is a cache of the world as of this_
  ([DM1/D2](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d2))
- **Owner:** _name_ · **Next check that it is still true:** _date_
  ([DM1/D12](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/05-design.md#d12))
- **This card:** written by _name or agent_ on _date_ · **What would retire it:** _the slice
  result, monitoring signal, or shift that would show the claims below wrong, with its threshold_
  ([SE1/U9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u9))

## Intended use

- **Decision it serves:** _…_ (`QUESTION_vN`)
- **Inputs, units, and envelope:** _ranges and categories seen in training; what happens outside
  them_ ([DM1/U6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u6))
- **Not for:** _populations, conditions, uses_
  ([DM1/R11](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r11))
- **Fallback when it cannot answer:** _…_
  ([DM1/O5](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o5))

## Performance

On _which split_, with intervals, against the baseline, after _N_ configurations tried
([DM1/F6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/03-feedback.md#f6)).

| Slice | *n* | Metric (interval) | Baseline | Notes |
| ----- | --- | ----------------- | -------- | ----- |
| All   |     |                   |          |       |

- **Calibration:** _…_ · **Threshold and the price ratio it came from:** _…_
- **Holdout looks:** _…_

## Bias audit

([DM1/R10](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/04-requirements.md#r10),
[DM1/O9](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o9))

| Group | *n* | Measure (interval) | Calibration | Gap vs. `Q1/BIAS-1` |
| ----- | --- | ------------------ | ----------- | ------------------- |

- **Worst group found by search, and confirmed on fresh data?** _…_
- **Sources of bias not checked, and why:** _…_
- **Reference class** a single prediction describes: _…_
  ([DM1/U8](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/02-unknowing.md#u8))

## What it changes

What this model's use will change about the data the next one is trained on, and what is kept to
measure the world without it ([DM1/O11](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o11)).

## Monitoring

Signals that do not wait for labels, and their thresholds
([DM1/O3](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/07-operating.md#o3)).

## Known failures

- _Examples it gets wrong, and why if known_
