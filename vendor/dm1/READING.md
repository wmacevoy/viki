# Reading

The longer arguments behind the guide, one line each on what the source actually says, so you can pick
one rather than all. Grouped by the chapter they support most. For software engineering's sources, see
[SE1's READING](https://github.com/wmacevoy/software-engineering/blob/v1.7/READING.md).

**Purpose and unknowing** ([1](guide/01-purpose.md)–[2](guide/02-unknowing.md))

- **John Tukey, "The Future of Data Analysis"** (1962) — the case that data analysis is a science of
  its own rather than a branch of mathematics.
- **George Box, "Science and Statistics"** (1976) — where *all models are wrong* comes from; an essay
  about choosing falsehoods on purpose.
- **Francis Anscombe, "Graphs in Statistical Analysis"** (1973) — four datasets with identical summary
  statistics and nothing else in common.
- **Kaufman, Rosset, Perlich, and Stitelman, "Leakage in Data Mining: Formulation, Detection, and
  Avoidance"** (2012) — leakage defined, catalogued, and traced through real competitions.
- **Sayash Kapoor and Arvind Narayanan, "Leakage and the Reproducibility Crisis in ML-based
  Science"** (2023) — how routinely leakage passes peer review, with a taxonomy.

**Feedback: search, holdouts, intervals** ([3](guide/03-feedback.md))

- **Andrew Gelman and Eric Loken, "The Garden of Forking Paths"** (2013) — the search is part of the
  result even when only one analysis was run.
- **Simmons, Nelson, and Simonsohn, "False-Positive Psychology"** (2011) — undisclosed flexibility
  makes almost anything significant, shown by experiment.
- **Yoav Benjamini and Yosef Hochberg, "Controlling the False Discovery Rate"** (1995) — the standard
  correction when searching many hypotheses at once.
- **Dwork et al., "The Reusable Holdout"** (2015) — why adaptive reuse spends a holdout, and a
  mechanism that slows the spending.
- **Recht, Roelofs, Schmidt, and Shankar, "Do ImageNet Classifiers Generalize to ImageNet?"** (2019) —
  a fresh test set, built the same way, and what years of reuse had cost.
- **Xiao-Li Meng, "Statistical Paradises and Paradoxes in Big Data"** (2018) — why a small selection
  bias beats an enormous sample, with the arithmetic.
- **David Hand, "Classifier Technology and the Illusion of Progress"** (2006) — how much of a
  sophisticated model's gain over a simple one survives contact with real conditions.

**Requirements: questions, labels, people** ([4](guide/04-requirements.md))

- **Chapman et al., *CRISP-DM 1.0*** (2000) — the process model; read the business-understanding phase
  twice.
- **Obermeyer, Powers, Vogeli, and Mullainathan, "Dissecting Racial Bias in an Algorithm Used to
  Manage the Health of Populations"** (2019) — a label that was a proxy (cost) for the thing intended
  (need), and who paid.
- **Latanya Sweeney, "Simple Demographics Often Identify People Uniquely"** (2000) — ZIP code, birth
  date, and sex.
- **Shokri, Stronati, Song, and Shmatikov, "Membership Inference Attacks against Machine Learning
  Models"** (2017) — a model can disclose who was in its training data.
- **Harini Suresh and John Guttag, "A Framework for Understanding Sources of Harm throughout the
  Machine Learning Life Cycle"** (2021) — historical, representation, measurement, aggregation,
  evaluation, and deployment bias, each placed at the stage it enters.
- **Kleinberg, Mullainathan, and Raghavan, "Inherent Trade-Offs in the Fair Determination of Risk
  Scores"** (2016), and **Alexandra Chouldechova, "Fair Prediction with Disparate Impact"** (2017) —
  why calibration and equal error rates cannot both hold when base rates differ.
- **Cathy O'Neil, *Weapons of Math Destruction*** (2016) — feedback loops and the people not in the
  room.

**Design and operating** ([5](guide/05-design.md), [7](guide/07-operating.md))

- **D. Sculley et al., "Hidden Technical Debt in Machine Learning Systems"** (2015) — the diagram
  where the ML code is the small box.
- **Gebru et al., "Datasheets for Datasets"** (2021), and **Mitchell et al., "Model Cards for Model
  Reporting"** (2019) — provenance and contracts, made into forms.
- **Quiñonero-Candela, Sugiyama, Schwaighofer, and Lawrence (eds.), *Dataset Shift in Machine
  Learning*** (2009) — the kinds of shift, and what each does to a model.
- **Ensign, Friedler, Neville, Scheidegger, and Venkatasubramanian, "Runaway Feedback Loops in
  Predictive Policing"** (2018) — a model that decides where to look, then learns from what it found.

**Testing** ([6](guide/06-testing.md))

- **Breck, Cai, Nielsen, Salib, and Sculley, "The ML Test Score"** (2017) — a rubric of tests for
  data, models, and production, from practice.
- **Ribeiro, Wu, Guestrin, and Singh, "Beyond Accuracy: Behavioral Testing of NLP Models with
  CheckList"** (2020) — invariance, direction, and minimum-functionality tests.
- **Guo, Pleiss, Sun, and Weinberger, "On Calibration of Modern Neural Networks"** (2017) — modern
  models are often confidently wrong, and a simple fix.
- **Bickel, Hammel, and O'Connell, "Sex Bias in Graduate Admissions: Data from Berkeley"** (1975) —
  Simpson's paradox in the data that made it famous.

**Agents and language models** ([10](guide/10-agents.md))

- **Sainz et al., "NLP Evaluation in Trouble: On the Need to Measure LLM Data Contamination for each
  Benchmark"** (2023) — test sets in the training data.
- **Zheng et al., "Judging LLM-as-a-Judge with MT-Bench and Chatbot Arena"** (2023) — where a model
  judge agrees with people, and its biases where it does not.

**Always**

- **Richard Cook, "How Complex Systems Fail"** — eighteen paragraphs; still the best return per page
  in either guide.
